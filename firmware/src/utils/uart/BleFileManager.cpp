#include "utils/uart/BleFileManager.h"
#include <Arduino.h>
#include <NimBLEDevice.h>

BleFileManager BleFM;

// Nordic UART Service (NUS) UUIDs. Standard 128-bit values supported by
// most BLE serial libraries, web-bluetooth examples, and nRF Connect.
static const char* NUS_SVC_UUID = "6e400001-b5a3-f393-e0a9-e50e24dcca9e";
static const char* NUS_RX_UUID  = "6e400002-b5a3-f393-e0a9-e50e24dcca9e"; // host -> device
static const char* NUS_TX_UUID  = "6e400003-b5a3-f393-e0a9-e50e24dcca9e"; // device -> host (notify)

// RX ring buffer. Large enough for several MTU-sized inbound packets so the
// BLE callback never has to drop bytes between FileManagerCore drains.
static constexpr size_t RX_RING = 4096;
static uint8_t           _rxBuf[RX_RING];
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

void BleFileManager::_sendBytes(const uint8_t* data, size_t len) {
  if (!_connected || !_txChar) return;
  // With MTU 247 negotiated, max ATT payload is 244. The core sizes GET
  // frames to fit in one notify (220 + 13 header = 233 bytes) so no
  // fragmentation under normal load — single notify, single LL packet,
  // single mbuf consumed per frame.
  constexpr size_t MAX_NOTIFY = 240;
  size_t off = 0;
  while (off < len) {
    // Soft backpressure: if the host has queued frames the radio hasn't
    // confirmed yet, wait briefly. _txPending is decremented in onStatus
    // for SUCCESS_NOTIFY *and* ERROR_GATT so the counter can't wedge.
    unsigned long waitStart = millis();
    while (_txPending >= TX_MAX_PENDING) {
      delay(2);
      if (!_connected) return;
      if (millis() - waitStart > 2000) {  // safety: never wedge forever
        _txPending = 0;
        break;
      }
    }
    size_t n = len - off;
    if (n > MAX_NOTIFY) n = MAX_NOTIFY;
    _txChar->setValue(data + off, n);
    _txPending++;
    _txChar->notify();
    off += n;
    // Flat pacing — one notify per ~one BLE connection interval. Without
    // this we submit faster than the radio can drain even with backpressure
    // and the mbuf pool ENOMEMs. 15 ms matches Chrome's default 15-30 ms
    // connection interval.
    delay(15);
  }
}

void BleFileManager::begin(const char* deviceName) {
  if (_inited) return;
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
  if (_server) _server->getAdvertising()->stop();
  NimBLEDevice::deinit(false);
  _server    = nullptr;
  _txChar    = nullptr;
  _connected = false;
  _rxHead = _rxTail = 0;
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
  }
  _wasConnected = _connected;
  while (_rxHead != _rxTail) {
    uint8_t b = _rxBuf[_rxTail];
    _rxTail = (uint16_t)((_rxTail + 1) % RX_RING);
    _core.onByte(b);  // ctx 'F'
    _scr.onByte(b);   // ctx 'S' (each codec ignores the other's frames)
  }
  // Only invite the core to emit another GET chunk if the TX queue has
  // room. Without this gate, pump() runs at main-loop rate and pumps a new
  // frame every ~1 ms — far faster than the BLE host can drain — and
  // notifications start dropping.
  if (_txPending < TX_MAX_PENDING) {
    _core.pump();
    _scr.pump(); // flush mirror dirty region (no-op in region mode / when idle)
  }
}
