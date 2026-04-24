#include "BuddyNus.h"
#include <NimBLEDevice.h>

#define NUS_SVC_UUID "6e400001-b5a3-f393-e0a9-e50e24dcca9e"
#define NUS_RX_UUID  "6e400002-b5a3-f393-e0a9-e50e24dcca9e"
#define NUS_TX_UUID  "6e400003-b5a3-f393-e0a9-e50e24dcca9e"

static uint8_t           _rxBuf[512];
static volatile uint16_t _rxHead = 0, _rxTail = 0;
static NimBLECharacteristic* _txChar    = nullptr;
static NimBLEServer*         _server    = nullptr;
static bool                  _connected = false;
static bool                  _inited    = false;

class _SrvCb : public NimBLEServerCallbacks {
  void onConnect(NimBLEServer*) override {
    _connected = true;
  }
  void onDisconnect(NimBLEServer* s) override {
    _connected = false;
    s->getAdvertising()->start();
  }
};

class _RxCb : public NimBLECharacteristicCallbacks {
  void onWrite(NimBLECharacteristic* c) override {
    std::string v = c->getValue();
    for (uint8_t ch : v) {
      uint16_t next = (_rxHead + 1) % sizeof(_rxBuf);
      if (next != _rxTail) { _rxBuf[_rxHead] = ch; _rxHead = next; }
    }
  }
};

static _SrvCb _srvCb;
static _RxCb  _rxCb;

void buddyNusInit(const char* name) {
  if (_inited) return;
  NimBLEDevice::init(name);
  _server = NimBLEDevice::createServer();
  _server->setCallbacks(&_srvCb);

  NimBLEService* svc = _server->createService(NUS_SVC_UUID);

  NimBLECharacteristic* rxChar = svc->createCharacteristic(
    NUS_RX_UUID, NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR);
  rxChar->setCallbacks(&_rxCb);

  _txChar = svc->createCharacteristic(NUS_TX_UUID, NIMBLE_PROPERTY::NOTIFY);

  svc->start();

  NimBLEAdvertising* adv = _server->getAdvertising();
  adv->addServiceUUID(NUS_SVC_UUID);
  adv->setScanResponse(true);
  adv->start();

  _rxHead = _rxTail = 0;
  _connected = false;
  _inited = true;
}

void buddyNusDeinit() {
  if (!_inited) return;
  if (_server) _server->getAdvertising()->stop();
  NimBLEDevice::deinit(false);
  _server    = nullptr;
  _txChar    = nullptr;
  _connected = false;
  _rxHead = _rxTail = 0;
  _inited = false;
}

bool buddyNusConnected() { return _connected; }

size_t buddyNusAvailable() {
  return (_rxHead - _rxTail + sizeof(_rxBuf)) % sizeof(_rxBuf);
}

int buddyNusRead() {
  if (_rxHead == _rxTail) return -1;
  uint8_t b = _rxBuf[_rxTail];
  _rxTail = (_rxTail + 1) % sizeof(_rxBuf);
  return b;
}

void buddyNusWrite(const uint8_t* data, size_t len) {
  if (!_connected || !_txChar) return;
  _txChar->setValue(data, len);
  _txChar->notify();
}
