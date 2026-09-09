#include "utils/uart/BleFileManager.h"
#include <Arduino.h>
#include <NimBLEDevice.h>
#include <esp_heap_caps.h>
#include "utils/ScratchBuffer.h"

BleFileManager BleFM;

// Nordic UART Service (NUS) UUIDs. Standard 128-bit values supported by
// most BLE serial libraries, web-bluetooth examples, and nRF Connect.
static const char* NUS_SVC_UUID = "6e400001-b5a3-f393-e0a9-e50e24dcca9e";
static const char* NUS_RX_UUID  = "6e400002-b5a3-f393-e0a9-e50e24dcca9e"; // host -> device
static const char* NUS_TX_UUID  = "6e400003-b5a3-f393-e0a9-e50e24dcca9e"; // device -> host (notify)

// RX ring buffer. Large enough for several MTU-sized inbound packets so the
// BLE callback never has to drop bytes between FileManagerCore drains.
static constexpr size_t RX_RING = 4096;
static uint8_t*          _rxBuf     = nullptr;
static volatile uint16_t _rxHead    = 0;
static volatile uint16_t _rxTail    = 0;
static NimBLECharacteristic* _txChar = nullptr;
static NimBLEServer*         _server = nullptr;
static bool                  _connected = false;
static bool                  _inited    = false;

// TX flow control. Each notify() consumes one NimBLE mbuf; the host releases
// it via SUCCESS_NOTIFY in onStatus. Without throttling, pump() runs at the
// main-loop rate (~1 kHz) while BLE drains at ~100 Hz on a 15 ms interval
// → mbuf pool (≈12 buffers) exhausts in ~12 ms and subsequent notifies drop
// silently.
static volatile int       _txPending     = 0;
static constexpr int      TX_MAX_PENDING = 4;

// Outbound TX ring. _sendBytes() enqueues without blocking (it runs on the main
// loop AND on whatever task drew a mirrored region); a dedicated task (_txTaskFn)
// notifies them out off the main loop. This replaces the old per-chunk delay()s
// that stalled the device's main loop — the _txPending cap (4 < ~12 mbufs) plus the
// task's light pacing is the flow control. Sized big (in PSRAM where available) so
// a full-screen render burst
// fits and drains over time instead of overflowing and dropping frames — the BLE
// link is far slower than the render. TX_RING is the actual allocated size, set in
// begin(); freed on end() (zero RAM when BLE is off).
static size_t             TX_RING  = 0;
static uint8_t*           _txBuf   = nullptr;
static volatile size_t    _txHeadW = 0;     // producer (enqueue, under TX lock)
static volatile size_t    _txTailW = 0;     // consumer (drain, main loop)
static uint32_t           _fbRemain = 0;    // bytes left in the current frame
static bool               _fbDrop   = false; // whole current frame is being dropped
static TaskHandle_t       _txTask    = nullptr; // dedicated BLE-notify drain task
static volatile bool      _txTaskRun = false;

static inline size_t _txUsed() { return TX_RING ? (_txHeadW - _txTailW + TX_RING) % TX_RING : 0; }
static inline size_t _txFree() { return TX_RING ? (TX_RING - 1 - _txUsed()) : 0; }

static inline void _ringPut(const uint8_t* d, size_t n) {
  size_t head = _txHeadW;
  for (size_t i = 0; i < n; i++) { _txBuf[head] = d[i]; head = (head + 1) % TX_RING; }
  _txHeadW = head; // publish after the bytes are staged
}

class _BleFmSrvCb : public NimBLEServerCallbacks {
  void onConnect(NimBLEServer*) override {
    _connected = true;
    _txPending = 0;
  }
  void onDisconnect(NimBLEServer* s) override {
    _connected = false;
    _txPending = 0;
    s->getAdvertising()->start();
  }
};

class _BleFmRxCb : public NimBLECharacteristicCallbacks {
  void onWrite(NimBLECharacteristic* c) override {
    std::string v = c->getValue();
    for (uint8_t ch : v) {
      uint16_t next = (uint16_t)((_rxHead + 1) % RX_RING);
      if (next != _rxTail) {
        _rxBuf[_rxHead] = ch;
        _rxHead = next;
      }
      // else: overflow — drop. Frame CRC will fail; host will time out.
    }
  }
};

class _BleFmTxCb : public NimBLECharacteristicCallbacks {
  void onStatus(NimBLECharacteristic*, Status s, int) override {
    // Fires from the NimBLE host task after every notify, win or lose.
    // Decrement in both paths — otherwise the counter sticks at the cap and
    // update() stops pumping.
    if (s == Status::SUCCESS_NOTIFY ||
        s == Status::ERROR_GATT     ||
        s == Status::ERROR_NO_CLIENT) {
      if (_txPending > 0) _txPending--;
    }
  }
};

static _BleFmSrvCb _srvCb;
static _BleFmRxCb  _rxCb;
static _BleFmTxCb  _txCb;

// Enqueue a frame's bytes WITHOUT blocking the caller. sendFrame() always emits a
// frame as header(9) → payload → crc(4) under the shared TX lock, so we recognise
// the 9-byte SOF header, read its declared payload length, and reserve the WHOLE
// frame up front. If it won't fit we drop the entire frame (header included) — a
// partial frame would desync the host parser and eat following frames. FM frames
// are produced only when update() sees ring room, so they never drop; only
// oversized mirror frames drop under congestion (the next render supersedes them).
void BleFileManager::_sendBytes(const uint8_t* data, size_t len) {
  if (!_connected || !_txChar || !_txBuf || len == 0) return;

  if (_fbRemain == 0) {
    // Expect a frame header: A5 5A | ctx type seq | len[4 LE].
    if (len == 9 && data[0] == 0xA5 && data[1] == 0x5A) {
      uint32_t pl = (uint32_t)data[5] | ((uint32_t)data[6] << 8)
                  | ((uint32_t)data[7] << 16) | ((uint32_t)data[8] << 24);
      _fbRemain = pl + 4;                      // payload + crc still to come
      _fbDrop   = ((uint32_t)(9 + pl + 4) > _txFree());
      if (!_fbDrop) _ringPut(data, len);       // stage the header
    }
    return;
  }

  // Mid-frame: payload then crc. Append, or skip if this frame is being dropped.
  _fbRemain = (len >= _fbRemain) ? 0 : (_fbRemain - (uint32_t)len);
  if (!_fbDrop) _ringPut(data, len);
}

// Dedicated task that drains the TX ring to the radio. Runs OFF the main loop, so
// the device stays responsive while the (much slower) BLE link catches up — the
// producers (_sendBytes) only enqueue. notify() returns void in NimBLE 1.4, so we
// can't detect a failed send; instead we pace one notification per few ms with the
// _txPending mbuf backpressure, so the controller's TX buffer never overflows. (An
// overflow silently dropped notifications mid-frame — that was the partial render.)
static void _txTaskFn(void*) {
  uint8_t tmp[240];
  uint32_t stalled = 0;
  while (_txTaskRun) {
    if (!_connected || !_txChar || !_txBuf || _txHeadW == _txTailW) {
      stalled = 0;
      vTaskDelay(pdMS_TO_TICKS(3));
      continue;
    }
    if (_txPending >= TX_MAX_PENDING) {
      // Waiting for onStatus() to free an mbuf. If the counter ever wedges (a
      // missed/raced decrement) while bytes still wait, reset it so the drain
      // can't stall forever — the old blocking code had the same safety valve.
      vTaskDelay(pdMS_TO_TICKS(3));
      if ((stalled += 3) > 500) { _txPending = 0; stalled = 0; }
      continue;
    }
    stalled = 0;
    size_t n = 0, tail = _txTailW;
    while (n < sizeof(tmp) && tail != _txHeadW) {
      tmp[n++] = _txBuf[tail];
      tail = (tail + 1) % TX_RING;
    }
    _txTailW = tail;
    _txChar->setValue(tmp, n);
    _txPending++;
    _txChar->notify();
    vTaskDelay(pdMS_TO_TICKS(5)); // ~radio-rate pacing; own task ⇒ no UI stall
  }
  _txTask = nullptr;
  vTaskDelete(nullptr);
}

void BleFileManager::begin(const char* deviceName) {
  if (_inited) return;
  // Must succeed before the RX callback below is registered.
  if (!scratchAlloc(_rxBuf, RX_RING)) return;
  NimBLEDevice::init(deviceName);
  // init() is idempotent — if another subsystem already brought NimBLE up
  // with a different name (e.g. Claude Buddy), the deviceName passed above
  // is silently dropped. setDeviceName() forces it through.
  NimBLEDevice::setDeviceName(deviceName);
  // Request the maximum ATT MTU. Without this, the default of 23 means each
  // notification can only carry 20 bytes of payload — calling notify() with
  // 180 bytes silently truncates and the client receives garbage after the
  // first frame. Web Bluetooth on Chrome desktop happily negotiates 247.
  NimBLEDevice::setMTU(247);
  _server = NimBLEDevice::createServer();
  _server->setCallbacks(&_srvCb);

  NimBLEService* svc = _server->createService(NUS_SVC_UUID);

  NimBLECharacteristic* rxChar = svc->createCharacteristic(
    NUS_RX_UUID, NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR);
  rxChar->setCallbacks(&_rxCb);

  _txChar = svc->createCharacteristic(NUS_TX_UUID, NIMBLE_PROPERTY::NOTIFY);
  _txChar->setCallbacks(&_txCb); // for SUCCESS_NOTIFY flow control

  svc->start();

  NimBLEAdvertising* adv = _server->getAdvertising();
  adv->addServiceUUID(NUS_SVC_UUID);
  adv->setScanResponse(true);
  adv->start();

  _rxHead = _rxTail = 0;
  if (!_txBuf) {
    // A full-screen render bursts tens of KB of region frames at once; hold it in
    // PSRAM so nothing drops, then drain over the much slower BLE link. Falls back
    // to a small internal-RAM ring on boards without PSRAM (some drops on big
    // screens, but far better than an 8 KB ring that dropped the central sprite).
    TX_RING = 192 * 1024;
    _txBuf  = (uint8_t*)heap_caps_malloc(TX_RING, MALLOC_CAP_SPIRAM);
    if (!_txBuf) { TX_RING = 16 * 1024; _txBuf = (uint8_t*)malloc(TX_RING); }
    if (!_txBuf) TX_RING = 0;
  }
  _txHeadW = _txTailW = 0;
  _fbRemain = 0; _fbDrop = false;
  _txPending = 0;
  _txTaskRun = true;
  if (!_txTask) xTaskCreatePinnedToCore(_txTaskFn, "bleTx", 4096, nullptr, 1, &_txTask, 0);
  _connected = false;
  _inited = true;
  _active = true;
  _wasConnected = false;
  // Both codecs share the one BLE sender and parse the same byte stream in
  // parallel, each acting only on its own context (the other's frames are
  // dropped: FM frames exceed the screen codec's tiny RX cap, and the FM codec
  // ignores ctx 'S').
  _core.setSender(_sendBytes);
  _scr.setSender(_sendBytes);
  // Keep each GET frame within a single ATT notification (MTU-3 = 244 max
  // after MTU negotiation; aim for 220 to leave overhead for our 13-byte
  // frame header). One notify per frame avoids burst-exhausting the NimBLE
  // mbuf pool (typically 12 buffers).
  _core.setGetChunkSize(220);
  _core.reset();
  _scr.resetParser();
}

void BleFileManager::end() {
  if (!_inited) return;
  // Stop the drain task and wait for it to exit before tearing down NimBLE / freeing
  // the ring, so it can never touch a freed _txChar / _txBuf.
  _txTaskRun = false;
  while (_txTask) vTaskDelay(pdMS_TO_TICKS(2));
  if (_server) _server->getAdvertising()->stop();
  NimBLEDevice::deinit(false);
  _server    = nullptr;
  _txChar    = nullptr;
  _connected = false;
  _rxHead = _rxTail = 0;
  if (_txBuf) { free(_txBuf); _txBuf = nullptr; }
  TX_RING = 0;
  _txHeadW = _txTailW = 0;
  _fbRemain = 0; _fbDrop = false;
  _inited = false;
  _active = false;
  _wasConnected = false;
  _core.reset(); // close any half-finished PUT
  _scr.stop();   // tear down any active screen-mirror stream
  _scr.resetParser();
}

bool BleFileManager::isAdvertising() const { return _active && !_connected; }
bool BleFileManager::isConnected()   const { return _active &&  _connected; }

void BleFileManager::update() {
  if (!_active) return;
  // Host dropped the link: close any half-finished transfer and stop streaming
  // so a stale GET/PUT or live mirror doesn't dangle until the next connect.
  if (_wasConnected && !_connected) {
    _core.reset();
    _scr.stop();
    _scr.resetParser();
    _txHeadW = _txTailW = 0; _fbRemain = 0; _fbDrop = false; // drop any half-sent frame
  }
  _wasConnected = _connected;
  while (_rxHead != _rxTail) {
    uint8_t b = _rxBuf[_rxTail];
    _rxTail = (uint16_t)((_rxTail + 1) % RX_RING);
    _core.onByte(b);  // ctx 'F'
    _scr.onByte(b);   // ctx 'S' (each codec ignores the other's frames)
  }
  // Invite more output only when the ring has room for a full FM frame, so a GET
  // chunk is produced (and thus never dropped) only when it will fit.
  if (_txFree() > 256) {
    _core.pump();
    _scr.pump(); // flush mirror dirty region (no-op in region mode / when idle)
  }
  // TX draining runs on its own task (_txTaskFn), so nothing to flush here.
}
