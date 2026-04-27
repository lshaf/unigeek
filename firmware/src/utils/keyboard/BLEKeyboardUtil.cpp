#include "BLEKeyboardUtil.h"

static constexpr uint8_t kKeyboardId = 0x01;
static constexpr uint8_t kMouseId    = 0x02;

BLEKeyboardUtil::BLEKeyboardUtil(const char* deviceName, const char* manufacturer, uint8_t batteryLevel)
  : _deviceName(deviceName)
  , _batteryLevel(batteryLevel)
{
  _manufacturer = manufacturer;
  _delayMs = 15;
}

BLEKeyboardUtil::~BLEKeyboardUtil()
{
  NimBLEDevice::deinit(true);
  _initialized = false;
  _connected   = false;
}

void BLEKeyboardUtil::begin()
{
  if (!_initialized) {
    NimBLEDevice::init(_deviceName);
    NimBLEDevice::setSecurityAuth(true, true, true);

    _server = NimBLEDevice::createServer();
    _server->setCallbacks(this);

    _hid = new NimBLEHIDDevice(_server);
    _inputKbd  = _hid->inputReport(kKeyboardId);
    _outputKbd = _hid->outputReport(kKeyboardId);
    _outputKbd->setCallbacks(this);
    _inputMouse = _hid->inputReport(kMouseId);

    _hid->setBatteryLevel(_batteryLevel);
    _hid->manufacturer(_manufacturer);
    _hid->pnp(0x02, _vid, _pid, _version);
    _hid->hidInfo(0x00, 0x01);

    uint8_t desc[sizeof(kHIDReportDescriptor)];
    memcpy(desc, kHIDReportDescriptor, sizeof(kHIDReportDescriptor));
    _hid->reportMap(desc, sizeof(desc));
    _hid->startServices();

    _initialized = true;
  }

  _adv = _server->getAdvertising();
  _adv->setAppearance(0x03C1);  // HID Keyboard
  _adv->addServiceUUID(_hid->hidService()->getUUID());
  _adv->setScanResponse(false);
  _adv->start();
}

void BLEKeyboardUtil::end()
{
  if (_adv) _adv->stop();
}

void BLEKeyboardUtil::sendReport(KeyReport* keys)
{
  if (_connected && _inputKbd) {
    _inputKbd->setValue(reinterpret_cast<uint8_t*>(keys), sizeof(KeyReport));
    _inputKbd->notify();
  }
}

void BLEKeyboardUtil::sendMouseReport(MouseReport* m)
{
  if (_connected && _inputMouse) {
    _inputMouse->setValue(reinterpret_cast<uint8_t*>(m), sizeof(MouseReport));
    _inputMouse->notify();
  }
}

bool BLEKeyboardUtil::isConnected()
{
  return _connected;
}

void BLEKeyboardUtil::setBatteryLevel(uint8_t level)
{
  _batteryLevel = level;
  if (_hid) _hid->setBatteryLevel(level);
}

void BLEKeyboardUtil::resetPair()
{
  NimBLEDevice::deleteAllBonds();
}

void BLEKeyboardUtil::onConnect(NimBLEServer*)
{
  _connected = true;
}

void BLEKeyboardUtil::onDisconnect(NimBLEServer*)
{
  _connected = false;
  if (_adv) _adv->start();
}

void BLEKeyboardUtil::onWrite(NimBLECharacteristic*)
{
  // LED status output — not used
}