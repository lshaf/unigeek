#include "BLEAnalyzerScreen.h"
#include "BLEFoxHuntScreen.h"
#include "ui/actions/InputSelectAction.h"
#include "core/ScreenManager.h"
#include "core/Device.h"
#include "core/AchievementManager.h"
#include "screens/ble/BLEMenuScreen.h"
#include "ui/actions/ShowStatusAction.h"

static String _toHex(const std::string& data)
{
  String result;
  for (uint8_t b : data) {
    if (b < 0x10) result += '0';
    result += String(b, HEX);
  }
  return result;
}

static String _vendorFromCID(uint16_t cid)
{
  switch (cid) {
    case 0x004C: return "Apple";
    case 0x0006: return "Microsoft";
    case 0x0075: return "Samsung";
    case 0x00E0: return "Google";
    case 0x0087: return "Garmin";
    case 0x009E: return "Bose";
    case 0x012D: return "Sony";
    case 0x0157: return "Huami/Mi";
    case 0x0171: return "Amazon";
    case 0x023D: return "Tile";
    case 0x038F: return "Xiaomi";
    case 0x046D: return "Logitech";
    case 0x06D7: return "Huawei";
    case 0x059A: return "DJI";
    case 0x05A7: return "Sonos";
    case 0x0759: return "JBL";
    case 0x0822: return "Anker";
    case 0x0001: return "Ericsson";
    case 0x0002: return "Intel";
    case 0x000F: return "Broadcom";
    case 0x0010: return "Qualcomm";
    case 0x0059: return "Nordic";
    case 0x00B7: return "Polar";
    default:     return "";
  }
}

static String _appleSubtype(uint8_t st)
{
  switch (st) {
    case 0x02: return "iBeacon";
    case 0x05: return "AirDrop";
    case 0x06: return "HomeKit";
    case 0x07: return "Proximity Pair";
    case 0x08: return "Hey Siri";
    case 0x09: return "AirPlay Tgt";
    case 0x0A: return "AirPlay Src";
    case 0x0B: return "MagicSwitch";
    case 0x0C: return "Handoff";
    case 0x0D: return "Tethering Src";
    case 0x0E: return "Tethering Tgt";
    case 0x0F: return "Nearby Action";
    case 0x10: return "Nearby Info";
    case 0x12: return "Find My";
    case 0x16: return "Custom";
    default:   return "";
  }
}

static String _vendorString(NimBLEAdvertisedDevice& dev)
{
  for (size_t i = 0; i < dev.getManufacturerDataCount(); i++) {
    const std::string& m = dev.getManufacturerData(i);
    if (m.size() < 2) continue;
    uint16_t cid = (uint8_t)m[0] | ((uint8_t)m[1] << 8);
    if (cid == 0x004C && m.size() >= 3) {
      String sub = _appleSubtype((uint8_t)m[2]);
      if (sub.length() > 0) return "Apple " + sub;
    }
    String v = _vendorFromCID(cid);
    if (v.length() > 0) return v;
    char buf[16];
    snprintf(buf, sizeof(buf), "CID 0x%04X", cid);
    return String(buf);
  }
  return "";
}

static String _resolveName(NimBLEAdvertisedDevice& dev)
{
  std::string n = dev.getName();
  if (!n.empty()) return String(n.c_str());
  return _vendorString(dev);
}

static String _serviceUUIDName(uint16_t uuid)
{
  switch (uuid) {
    case 0x180A: return "Device Info";
    case 0x180D: return "Heart Rate";
    case 0x180F: return "Battery";
    case 0x1810: return "Blood Pressure";
    case 0x1812: return "HID";
    case 0x1816: return "CSC";
    case 0x1818: return "Cycling Power";
    case 0x181A: return "Environmental";
    case 0x181C: return "User Data";
    case 0x181D: return "Weight Scale";
    case 0x181F: return "CGM";
    case 0x1822: return "Pulse Ox";
    case 0x1826: return "Fitness";
    case 0xFD6F: return "Exposure Notification";
    case 0xFE03: return "Amazon";
    case 0xFE2C: return "Google FastPair";
    case 0xFE9F: return "Google";
    case 0xFEAA: return "Eddystone";
    case 0xFEED: return "Tile";
    default:     return "";
  }
}

static String _serviceUUIDLabel(NimBLEUUID uuid)
{
  String full = String(uuid.toString().c_str());
  if (full.startsWith("0x") && full.length() <= 6) {
    uint16_t v = (uint16_t)strtoul(full.c_str() + 2, nullptr, 16);
    String name = _serviceUUIDName(v);
    if (name.length() > 0) return name + " (" + full + ")";
  }
  return full;
}

static String _appearanceName(uint16_t app)
{
  switch (app >> 6) {
    case 0x000: return "Generic";
    case 0x001: return "Phone";
    case 0x002: return "Computer";
    case 0x003: return "Watch";
    case 0x004: return "Clock";
    case 0x005: return "Display";
    case 0x006: return "Remote Ctrl";
    case 0x007: return "Glasses";
    case 0x008: return "Tag";
    case 0x009: return "Keyring";
    case 0x00A: return "Media Player";
    case 0x00B: return "Barcode";
    case 0x00C: return "Thermometer";
    case 0x00D: return "Heart Rate";
    case 0x00E: return "Blood Pressure";
    case 0x00F: return "HID";
    case 0x010: return "Glucose";
    case 0x011: return "Running";
    case 0x012: return "Cycling";
    case 0x015: return "Sensor";
    case 0x021: return "Audio Sink";
    case 0x022: return "Audio Source";
    case 0x025: return "Earbuds";
    default:    return "";
  }
}

static String _advTypeName(uint8_t t)
{
  switch (t) {
    case 0: return "ADV_IND";
    case 1: return "DIRECT_IND";
    case 2: return "SCAN_IND";
    case 3: return "NONCONN_IND";
    case 4: return "SCAN_RSP";
    default: return String(t);
  }
}

static String _advFlagsString(uint8_t f)
{
  String s;
  if (f & 0x01) s += "LE-Lim ";
  if (f & 0x02) s += "LE-Gen ";
  if (f & 0x04) s += "BR-NS ";
  if (f & 0x08) s += "Sim-C ";
  if (f & 0x10) s += "Sim-H ";
  s.trim();
  if (s.length() == 0) {
    char buf[8];
    snprintf(buf, sizeof(buf), "0x%02X", f);
    return String(buf);
  }
  return s;
}

static String _distanceEstimate(int rssi, int8_t txPower, bool haveTx)
{
  if (!haveTx || txPower == 0 || rssi == 0) return String();
  float diff = (float)txPower - (float)rssi;
  float d = pow(10.0f, diff / 20.0f);
  char buf[16];
  if (d < 1.0f)        snprintf(buf, sizeof(buf), "~%d cm", (int)(d * 100));
  else if (d < 100.0f) snprintf(buf, sizeof(buf), "~%.1f m", d);
  else                 snprintf(buf, sizeof(buf), ">100 m");
  return String(buf);
}

static String _formatUUID16Bytes(const uint8_t* b)
{
  char buf[40];
  snprintf(buf, sizeof(buf),
           "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x",
           b[0],b[1],b[2],b[3], b[4],b[5], b[6],b[7],
           b[8],b[9], b[10],b[11],b[12],b[13],b[14],b[15]);
  return String(buf);
}

static String _buildServiceUUIDText(NimBLEAdvertisedDevice& dev)
{
  String s;
  int n = dev.getServiceUUIDCount();
  for (int i = 0; i < n; i++) {
    if (i > 0) s += "\n";
    s += _serviceUUIDLabel(dev.getServiceUUID(i));
  }
  return s;
}

static String _buildMfrDataText(NimBLEAdvertisedDevice& dev)
{
  String s;
  int n = dev.getManufacturerDataCount();
  for (int i = 0; i < n; i++) {
    if (i > 0) s += "\n\n";
    const std::string m = dev.getManufacturerData(i);
    if (m.size() < 2) continue;
    uint16_t cid = (uint8_t)m[0] | ((uint8_t)m[1] << 8);

    String header = _vendorFromCID(cid);
    if (cid == 0x004C && m.size() >= 3) {
      String sub = _appleSubtype((uint8_t)m[2]);
      if (sub.length() > 0) header = "Apple " + sub;
    }
    if (header.length() == 0) {
      char buf[16];
      snprintf(buf, sizeof(buf), "CID 0x%04X", cid);
      header = buf;
    }
    s += header;
    s += "\n";

    bool isIBeacon = (m.size() >= 25 &&
                      (uint8_t)m[0] == 0x4C && (uint8_t)m[1] == 0x00 &&
                      (uint8_t)m[2] == 0x02 && (uint8_t)m[3] == 0x15);
    if (isIBeacon) {
      s += "UUID ";
      s += _formatUUID16Bytes((const uint8_t*)m.data() + 4);
      s += "\n";
      uint16_t major = ((uint8_t)m[20] << 8) | (uint8_t)m[21];
      uint16_t minor = ((uint8_t)m[22] << 8) | (uint8_t)m[23];
      int8_t   power = (int8_t)m[24];
      char buf[64];
      snprintf(buf, sizeof(buf), "Mj %u  Mn %u  Pwr %d dBm",
               major, minor, power);
      s += buf;
    } else {
      s += _toHex(m);
    }
  }
  return s;
}

static String _buildServiceDataText(NimBLEAdvertisedDevice& dev)
{
  String s;
  int n = dev.getServiceDataCount();
  for (int i = 0; i < n; i++) {
    if (i > 0) s += "\n\n";
    s += _serviceUUIDLabel(dev.getServiceDataUUID(i));
    s += "\n";
    s += _toHex(dev.getServiceData(i));
  }
  return s;
}

static String _buildPayloadText(NimBLEAdvertisedDevice& dev)
{
  String s;
  uint8_t* p = dev.getPayload();
  size_t   n = dev.getPayloadLength();
  for (size_t i = 0; i < n; i++) {
    char buf[4];
    snprintf(buf, sizeof(buf), "%02x ", p[i]);
    s += buf;
  }
  s.trim();
  return s;
}

static NimBLEAddress _rssiTarget;
static volatile int  _selRssi = 0;

class RssiWatcher : public NimBLEAdvertisedDeviceCallbacks {
public:
  void onResult(NimBLEAdvertisedDevice* dev) override
  {
    if (dev->getAddress() == _rssiTarget) _selRssi = dev->getRSSI();
  }
};
static RssiWatcher _rssiWatcher;

BLEAnalyzerScreen::~BLEAnalyzerScreen()
{
  if (_bleScan != nullptr) {
    _stopRssiWatch();
    _bleScan->stop();
    if (!_externalTarget) NimBLEDevice::deinit(true);
    _bleScan = nullptr;
  }
}

void BLEAnalyzerScreen::configureTarget(NimBLEAdvertisedDevice& device)
{
  _externalTarget = true;
  _selDev = device;
  _devices[0] = device;
  _devCount = 1;
  _selectedDeviceIdx = 0;
}

void BLEAnalyzerScreen::onInit()
{
  NimBLEDevice::init("");
  _bleScan = NimBLEDevice::getScan();
  if (_externalTarget) {
    _showInfo();
    _startRssiWatch();
  } else {
    _doScan();
  }
}

void BLEAnalyzerScreen::onItemSelected(uint8_t index)
{
  if (_state == STATE_LIST) {
    if (index == 0) {
      _doScan();
      return;
    }
    const int devIndex = (int)index - 1;
    if (devIndex >= 0 && devIndex < _devCount) {
      _selectedDeviceIdx = devIndex;
      _selDev            = _devices[devIndex];
      _showOptions();
    }
  } else if (_state == STATE_INFO) {
    if (index == 11 && _selDev.getServiceUUIDCount() > 0)
      _showDetail("Service UUIDs", _buildServiceUUIDText(_selDev));
    else if (index == 12 && _selDev.getManufacturerDataCount() > 0)
      _showDetail("Mfr Data", _buildMfrDataText(_selDev));
    else if (index == 13 && _selDev.getServiceDataCount() > 0)
      _showDetail("Service Data", _buildServiceDataText(_selDev));
    else if (index == 15 && _selDev.getPayloadLength() > 0)
      _showDetail("Payload", _buildPayloadText(_selDev));
  }
}

void BLEAnalyzerScreen::onBack()
{
  if (_state == STATE_DETAIL) {
    _showInfo();
  } else if (_state == STATE_INFO) {
    _stopRssiWatch();
    if (_externalTarget) { Screen.goBack(); return; }
    uint8_t listIdx = (_selectedDeviceIdx >= 0) ? (_selectedDeviceIdx + 1) : 0;
    _showList(listIdx);
  } else {
    _bleScan->stop();
    Screen.goBack();
  }
}

void BLEAnalyzerScreen::onUpdate()
{
  if (_state != STATE_DETAIL) {
    ListScreen::onUpdate();

    if (_state == STATE_INFO && millis() - _lastRssiRefresh >= 500) {
      _lastRssiRefresh = millis();
      _refreshRssiRow();
    }
    return;
  }
  if (!Uni.Nav->wasPressed()) return;
  auto dir = Uni.Nav->readDirection();
  if (dir == INavigation::DIR_BACK || dir == INavigation::DIR_PRESS) {
    onBack();
    return;
  }
  if (_textView.onNav(dir) && Uni.Speaker) Uni.Speaker->beep();
}

void BLEAnalyzerScreen::onRender()
{
  if (_state != STATE_DETAIL) {
    ListScreen::onRender();
    return;
  }
  _textView.render(bodyX(), bodyY(), bodyW(), bodyH());
}

void BLEAnalyzerScreen::_showDetail(const char* titleText, const String& content)
{
  _detailTitle = titleText;
  _textView.setContent(content);
  _state = STATE_DETAIL;
  render();
}

void BLEAnalyzerScreen::_doScan()
{
  _state             = STATE_SCAN;
  _selectedDeviceIdx = -1;
  render();
  ShowStatusAction::show("Scanning...", 0);

  _bleScan->setAdvertisedDeviceCallbacks(nullptr, false);
  _bleScan->setMaxResults(0xFF);
  _bleScan->clearResults();

  _scanResults = _bleScan->start(kScanSeconds, false);

  _devCount = min((int)_scanResults.getCount(), (int)kMaxDevices);
  for (int i = 0; i < _devCount; i++) _devices[i] = _scanResults.getDevice(i);

  for (int i = 1; i < _devCount; ++i) {
    NimBLEAdvertisedDevice dev = _devices[i];
    int j = i;
    while (j > 0 && _devices[j - 1].getRSSI() < dev.getRSSI()) {
      _devices[j] = _devices[j - 1];
      --j;
    }
    _devices[j] = dev;
  }

  if (_devCount == 0) ShowStatusAction::show("No devices found");

  int ns = Achievement.inc("ble_analyzer_scan");
  if (ns == 1) Achievement.unlock("ble_analyzer_scan");
  if (_devCount >= 20) {
    int n20 = Achievement.inc("ble_analyzer_20");
    if (n20 == 1) Achievement.unlock("ble_analyzer_20");
  }

  _showList();
}

void BLEAnalyzerScreen::_startRssiWatch()
{
  _rssiTarget      = _selDev.getAddress();
  _selRssi         = 0;
  _lastRssiShown   = 1;
  _lastRssiRefresh = millis();

  _bleScan->setMaxResults(0);
  _bleScan->setAdvertisedDeviceCallbacks(&_rssiWatcher, true);
  _bleScan->start(0, nullptr, false);
  _rssiWatching = true;
}

void BLEAnalyzerScreen::_stopRssiWatch()
{
  if (!_rssiWatching) return;
  _bleScan->stop();
  _bleScan->setAdvertisedDeviceCallbacks(nullptr, false);
  _bleScan->setMaxResults(0xFF);
  _rssiWatching = false;
}

void BLEAnalyzerScreen::_refreshRssiRow()
{
  const int rssi = _selRssi;
  if (rssi == 0 || rssi == _lastRssiShown) return;
  _lastRssiShown = rssi;

  _infoVal[5]   = String(rssi) + " dBm";
  _infoItems[5] = {"RSSI", _infoVal[5].c_str()};

  bool   haveTx = _selDev.haveTXPower();
  int8_t tx     = haveTx ? _selDev.getTXPower() : 0;
  _infoVal[7]   = _distanceEstimate(rssi, tx, haveTx);
  _infoItems[7] = {"Distance",
                   _infoVal[7].length() > 0 ? _infoVal[7].c_str() : nullptr};

  render();
}

void BLEAnalyzerScreen::_rebuildDevItems()
{
  _devItems[0] = {"Rescan"};
  for (int i = 0; i < _devCount; i++) {
    NimBLEAdvertisedDevice& dev = _devices[i];
    String name = _resolveName(dev);
    if (name.length() > 0) {
      _devLabel[i] = name;
      _devSub[i]   = dev.getAddress().toString().c_str();
    } else {
      _devLabel[i] = dev.getAddress().toString().c_str();
      _devSub[i]   = "";
    }
    _devItems[i + 1]         = {_devLabel[i].c_str(),
                                 _devSub[i].length() > 0 ? _devSub[i].c_str() : nullptr};
    _devItems[i + 1].rssi    = (int16_t)dev.getRSSI();
    _devItems[i + 1].hasRssi = true;
    _devItems[i + 1].sublabelMarquee = _devSub[i].length() > 0;
  }
}

void BLEAnalyzerScreen::_showOptions()
{
  static const InputSelectAction::Option options[] = {
    {"Details", "details", nullptr, 0, false},
    {"Fox Hunt", "fox", nullptr, 0, false},
  };
  const char* choice = InputSelectAction::popup("Options", options, 2);
  if (!choice) return;
  if (strcmp(choice, "details") == 0) {
    _showInfo();
    _startRssiWatch();
  } else if (strcmp(choice, "fox") == 0) {
    _stopRssiWatch();
    auto* fox = new BLEFoxHuntScreen();
    fox->configureTarget(_selDev);
    Screen.push(fox);
  }
}

void BLEAnalyzerScreen::_showList(uint8_t selectedIndex)
{
  _state = STATE_LIST;
  _rebuildDevItems();
  setItems(_devItems, _devCount + 1, selectedIndex);
}

void BLEAnalyzerScreen::_showInfo()
{
  if (_selectedDeviceIdx < 0) return;
  _state = STATE_INFO;

  int nd = Achievement.inc("ble_analyzer_detail");
  if (nd == 1) Achievement.unlock("ble_analyzer_detail");

  NimBLEAdvertisedDevice& dev = _selDev;

  const char* addrType;
  switch (dev.getAddressType()) {
    case BLE_ADDR_PUBLIC:    addrType = "Public";    break;
    case BLE_ADDR_RANDOM:    addrType = "Random";    break;
    case BLE_ADDR_PUBLIC_ID: addrType = "Public ID"; break;
    case BLE_ADDR_RANDOM_ID: addrType = "Random ID"; break;
    default:                 addrType = "Unknown";   break;
  }

  std::string rawName = dev.getName();
  String vendor = _vendorString(dev);
  if (!rawName.empty()) {
    _infoVal[0] = rawName.c_str();
  } else {
    _infoVal[0] = vendor.length() > 0 ? "(" + vendor + ")" : String();
  }
  _infoVal[1] = vendor;
  _infoVal[2] = dev.getAddress().toString().c_str();
  _infoVal[3] = addrType;

  if (dev.haveAppearance()) {
    uint16_t app  = dev.getAppearance();
    String   name = _appearanceName(app);
    char buf[20];
    snprintf(buf, sizeof(buf), "0x%04X", app);
    _infoVal[4] = name.length() > 0 ? name + " (" + buf + ")" : String(buf);
  } else {
    _infoVal[4] = "";
  }

  const int rssi = (_selRssi != 0) ? (int)_selRssi : (int)dev.getRSSI();
  _lastRssiShown = _selRssi;
  _infoVal[5]    = String(rssi) + " dBm";

  bool haveTx = dev.haveTXPower();
  int8_t tx   = haveTx ? dev.getTXPower() : 0;
  _infoVal[6] = haveTx ? (String((int)tx) + " dBm") : String();
  _infoVal[7] = _distanceEstimate(rssi, tx, haveTx);

  _infoVal[8] = _advTypeName(dev.getAdvType());
  _infoVal[9] = _advFlagsString(dev.getAdvFlags());

  _infoVal[10] = dev.isConnectable() ? "Yes" : "No";
  _infoVal[11] = String(dev.getServiceUUIDCount());
  _infoVal[12] = String(dev.getManufacturerDataCount());
  _infoVal[13] = String(dev.getServiceDataCount());
  _infoVal[14] = dev.getURI().c_str();
  _infoVal[15] = String((unsigned)dev.getPayloadLength()) + " B";

  static constexpr const char* kLabels[] = {
    "Name", "Vendor", "Address", "Addr Type",
    "Appearance",
    "RSSI", "TX Power", "Distance",
    "Adv Type", "Adv Flags",
    "Connectable",
    "Svc UUIDs", "Mfr Data", "Svc Data",
    "URI", "Payload",
  };
  for (int i = 0; i < kInfoRows; i++) {
    _infoItems[i] = {kLabels[i],
                     _infoVal[i].length() > 0 ? _infoVal[i].c_str() : nullptr};
  }
  setItems(_infoItems, kInfoRows);
}