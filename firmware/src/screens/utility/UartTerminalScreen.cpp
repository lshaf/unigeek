#include "UartTerminalScreen.h"
#include "core/Device.h"
#include "core/INavigation.h"
#include "core/ScreenManager.h"
#include "core/AchievementManager.h"
#include "core/PinConfigManager.h"
#include "screens/utility/UtilityMenuScreen.h"
#include "ui/actions/InputSelectAction.h"
#include "ui/actions/InputTextAction.h"
#include "ui/actions/InputNumberAction.h"
#include "ui/actions/ShowStatusAction.h"
#include "utils/network/WifiUtility.h"
#include <time.h>
#include <driver/gpio.h>

// ── Lifecycle ─────────────────────────────────────────────────────────────────

UartTerminalScreen::~UartTerminalScreen() {
  if (_taskHandle) { vTaskDelete(_taskHandle); _taskHandle = nullptr; }
  if (_mutex)      { vSemaphoreDelete(_mutex); _mutex = nullptr; }
  _closeLog();
  _stopWifiStream();
  if (_serialRunning) _serial.end();
}

const char* UartTerminalScreen::title() {
  return (_state == STATE_RUNNING) ? "UART Terminal" : "UART Setup";
}

void UartTerminalScreen::onInit() {
  _rxPin = PinConfig.getInt(PIN_CONFIG_EXT_SCL, PIN_CONFIG_EXT_SCL_DEFAULT);
  _txPin = PinConfig.getInt(PIN_CONFIG_EXT_SDA, PIN_CONFIG_EXT_SDA_DEFAULT);
  _updateLabels();
  _rebuildItems();
}

void UartTerminalScreen::onUpdate() {
  if (_state == STATE_CONFIG) {
    ListScreen::onUpdate();
    return;
  }

  // STATE_RUNNING
  _drainShared();
  _acceptClients();

  if (millis() - _lastDrawMs >= 200) {
    _lastDrawMs = millis();
    render();
  }

  if (!Uni.Nav->wasPressed()) return;
  auto dir = Uni.Nav->readDirection();
  if (dir == INavigation::DIR_BACK) {
    _closeLog();
    _stopWifiStream();
    Screen.goBack();
  } else if (dir == INavigation::DIR_PRESS) {
    _sendCommand();
  } else if (dir == INavigation::DIR_UP || dir == INavigation::DIR_DOWN) {
    _hexMode = !_hexMode;
    render();
  }
}

void UartTerminalScreen::onRender() {
  if (_state == STATE_CONFIG) {
    ListScreen::onRender();
    return;
  }
  _log.draw(Uni.Lcd, bodyX(), bodyY(), bodyW(), bodyH(), _statusBarCb, this);
}

void UartTerminalScreen::onBack() {
  Screen.goBack();
}

void UartTerminalScreen::onItemSelected(uint8_t index) {
  switch (index) {
    case 0: _configBaud();     break;
    case 1: _configRx();       break;
    case 2: _configTx();       break;
    case 3: _configSaveMode(); break;
    case 4:
      if      (_saveMode == SAVE_NO)         _startTerminal();
      else if (_saveMode == SAVE_FILE)       _configSaveFile();
      else if (_saveMode == SAVE_STREAM_AP)  _configApName();
      else                                   _configNetwork();
      break;
    case 5: _startTerminal(); break;
    default:                  break;
  }
}

// ── Config helpers ────────────────────────────────────────────────────────────

void UartTerminalScreen::_updateLabels() {
  snprintf(_baudLabel, sizeof(_baudLabel), "%d", _baud);
  if (_rxPin < 0) snprintf(_rxLabel, sizeof(_rxLabel), "none");
  else            snprintf(_rxLabel, sizeof(_rxLabel), "%d", _rxPin);
  if (_txPin < 0) snprintf(_txLabel, sizeof(_txLabel), "none");
  else            snprintf(_txLabel, sizeof(_txLabel), "%d", _txPin);

  switch (_saveMode) {
    case SAVE_FILE:       snprintf(_saveModeLabel, sizeof(_saveModeLabel), "File");          break;
    case SAVE_STREAM_AP:  snprintf(_saveModeLabel, sizeof(_saveModeLabel), "Stream AP");     break;
    case SAVE_STREAM_NET: snprintf(_saveModeLabel, sizeof(_saveModeLabel), "Stream Network"); break;
    default:              snprintf(_saveModeLabel, sizeof(_saveModeLabel), "None");          break;
  }

  if (_saveMode == SAVE_FILE) {
    if (_saveFilename.length() == 0) snprintf(_saveLabel, sizeof(_saveLabel), "tap to set");
    else _saveFilename.toCharArray(_saveLabel, sizeof(_saveLabel));
  } else if (_saveMode == SAVE_STREAM_AP) {
    _apName.toCharArray(_saveLabel, sizeof(_saveLabel));
  } else if (_saveMode == SAVE_STREAM_NET) {
    if (_networkSSID.length() == 0) snprintf(_saveLabel, sizeof(_saveLabel), "tap to scan");
    else _networkSSID.toCharArray(_saveLabel, sizeof(_saveLabel));
  }
}

void UartTerminalScreen::_rebuildItems() {
  uint8_t sel = _selectedIndex;
  _configItems[0] = {"Baud Rate", _baudLabel};
  _configItems[1] = {"RX GPIO",   _rxLabel};
  _configItems[2] = {"TX GPIO",   _txLabel};
  _configItems[3] = {"Log Mode",  _saveModeLabel};
  if (_saveMode == SAVE_NO) {
    _configItems[4] = {"Start", nullptr};
    setItems(_configItems, 5, sel);
  } else {
    const char* label = nullptr;
    if      (_saveMode == SAVE_FILE)       label = "Filename";
    else if (_saveMode == SAVE_STREAM_AP)  label = "AP Name";
    else if (_saveMode == SAVE_STREAM_NET) label = "Network";
    _configItems[4] = {label, _saveLabel};
    _configItems[5] = {"Start", nullptr};
    setItems(_configItems, 6, sel);
  }
}

void UartTerminalScreen::_configBaud() {
  static constexpr InputSelectAction::Option bauds[] = {
    {"9600",   "9600"},  {"19200",  "19200"}, {"38400",  "38400"},
    {"57600",  "57600"}, {"115200", "115200"}, {"230400", "230400"},
  };
  char cur[8];
  snprintf(cur, sizeof(cur), "%d", _baud);
  const char* v = InputSelectAction::popup("Baud Rate", bauds, 6, cur);
  if (v) { _baud = atoi(v); _updateLabels(); }
  render();
}

void UartTerminalScreen::_configRx() {
  int pin = InputNumberAction::popup("RX GPIO (0=none)", 0, 48, _rxPin >= 0 ? _rxPin : 0);
  if (!InputNumberAction::wasCancelled()) {
    _rxPin = (pin == 0) ? -1 : pin;
    _updateLabels();
  }
  render();
}

void UartTerminalScreen::_configTx() {
  int pin = InputNumberAction::popup("TX GPIO (0=none)", 0, 48, _txPin >= 0 ? _txPin : 0);
  if (!InputNumberAction::wasCancelled()) {
    _txPin = (pin == 0) ? -1 : pin;
    _updateLabels();
  }
  render();
}

void UartTerminalScreen::_configSaveMode() {
  static constexpr InputSelectAction::Option modes[] = {
    {"None",           "no"},
    {"File",           "file"},
    {"Stream AP",      "ap"},
    {"Stream Network", "net"},
  };
  const char* cur = "no";
  if      (_saveMode == SAVE_FILE)       cur = "file";
  else if (_saveMode == SAVE_STREAM_AP)  cur = "ap";
  else if (_saveMode == SAVE_STREAM_NET) cur = "net";
  const char* v = InputSelectAction::popup("Log Mode", modes, 4, cur);
  if (v) {
    if      (strcmp(v, "file") == 0) _saveMode = SAVE_FILE;
    else if (strcmp(v, "ap")   == 0) _saveMode = SAVE_STREAM_AP;
    else if (strcmp(v, "net")  == 0) _saveMode = SAVE_STREAM_NET;
    else                             _saveMode = SAVE_NO;
  }
  _updateLabels();
  _rebuildItems();
  render();
}

void UartTerminalScreen::_configSaveFile() {
  char def[28] = "uart_log";
  time_t now = time(nullptr);
  if (now > 1577836800L) {
    struct tm* t = localtime(&now);
    snprintf(def, sizeof(def), "%04d%02d%02d_%02d%02d%02d",
             t->tm_year + 1900, t->tm_mon + 1, t->tm_mday,
             t->tm_hour, t->tm_min, t->tm_sec);
  }
  const char* prompt = _saveFilename.length() > 0 ? _saveFilename.c_str() : def;
  String v = InputTextAction::popup("Log filename", prompt);
  if (!InputTextAction::wasCancelled() && v.length() > 0) _saveFilename = v;
  _updateLabels();
  render();
}

void UartTerminalScreen::_configApName() {
  String v = InputTextAction::popup("AP Name", _apName.c_str());
  if (!InputTextAction::wasCancelled() && v.length() > 0) _apName = v;
  _updateLabels();
  render();
}

void UartTerminalScreen::_configNetwork() {
  WifiUtility::ScannedWifi nets[WifiUtility::MAX_WIFI];
  ShowStatusAction::show("Scanning...", 0);
  int n = WifiUtility::scan(nets, WifiUtility::MAX_WIFI);
  if (n == 0) { ShowStatusAction::show("No networks found", 1500); render(); return; }

  InputSelectAction::Option opts[WifiUtility::MAX_WIFI];
  for (int i = 0; i < n; i++) {
    opts[i] = {nets[i].ssid, nets[i].ssid, nets[i].bssid, nets[i].rssi, true};
  }
  const char* sel = InputSelectAction::popup("Network", opts, n, _networkSSID.c_str());
  if (!sel) { render(); return; }

  const char* bssid = "";
  for (int i = 0; i < n; i++) {
    if (strcmp(nets[i].ssid, sel) == 0) { bssid = nets[i].bssid; break; }
  }

  auto res = WifiUtility::connectWithPrompt(bssid, sel);
  if (res == WifiUtility::CONNECT_OK) {
    _networkSSID  = sel;
    _networkBssid = bssid;
    ShowStatusAction::show(("Connected to " + String(sel)).c_str(), 1500);
  } else if (res == WifiUtility::CONNECT_FAILED) {
    ShowStatusAction::show("Connection failed", 1500);
  }
  _updateLabels();
  render();
}

// ── Terminal ──────────────────────────────────────────────────────────────────

void UartTerminalScreen::_startTerminal() {
  if (_rxPin >= 0) {
    gpio_reset_pin((gpio_num_t)_rxPin);
    gpio_set_direction((gpio_num_t)_rxPin, GPIO_MODE_INPUT);
  }
  if (_txPin >= 0) gpio_reset_pin((gpio_num_t)_txPin);

  _serial.begin(_baud, SERIAL_8N1, _rxPin, _txPin);
  _serialRunning = true;
  _state = STATE_RUNNING;

  _mutex = xSemaphoreCreateMutex();
  xTaskCreatePinnedToCore(_serialTask, "uart_rx", 4096, this, 1, &_taskHandle, 0);

  _log.addLine("UART ready", TFT_GREEN);
  char info[48];
  snprintf(info, sizeof(info), "RX:%d TX:%d  %d baud", _rxPin, _txPin, _baud);
  _log.addLine(info, TFT_DARKGREY);

  {
    const char* modeName;
    switch (_saveMode) {
      case SAVE_FILE:       modeName = "File";           break;
      case SAVE_STREAM_AP:  modeName = "Stream AP";      break;
      case SAVE_STREAM_NET: modeName = "Stream Network"; break;
      default:              modeName = "None";           break;
    }
    char modeInfo[32];
    snprintf(modeInfo, sizeof(modeInfo), "Log Mode: %s", modeName);
    _log.addLine(modeInfo, TFT_DARKGREY);
  }

  if      (_saveMode == SAVE_FILE)   _openLog();
  else if (_saveMode != SAVE_NO)     _startWifiStream();

  int n = Achievement.inc("uart_first_connect");
  if (n == 1) Achievement.unlock("uart_first_connect");

  render();
}

void UartTerminalScreen::_serialTask(void* arg) {
  auto* self = static_cast<UartTerminalScreen*>(arg);
  while (true) {
    if (self->_serial.available()) {
      xSemaphoreTake(self->_mutex, portMAX_DELAY);
      while (self->_serial.available()) self->_rxSharedBuf += (char)self->_serial.read();
      xSemaphoreGive(self->_mutex);
    }
    vTaskDelay(pdMS_TO_TICKS(10));
  }
}

void UartTerminalScreen::_drainShared() {
  if (!_mutex) return;
  if (xSemaphoreTake(_mutex, 0) != pdTRUE) return;
  String chunk = _rxSharedBuf;
  _rxSharedBuf = "";
  xSemaphoreGive(_mutex);
  if (chunk.isEmpty()) return;

  bool gotData = false;
  for (int i = 0; i < (int)chunk.length(); i++) {
    char c = chunk[i];
    if (c == '\r') continue;
    if (c == '\n' || _rxLineBuf.length() >= 58) {
      _log.addLine(_rxLineBuf.c_str(), TFT_WHITE);
      if (_logFile) _logFile.println(_rxLineBuf.c_str());
      _broadcastLine(_rxLineBuf.c_str());
      _rxLineBuf = "";
      gotData = true;
    } else {
      _rxLineBuf += c;
    }
  }
  if (gotData) {
    int n = Achievement.inc("uart_receive_data");
    if (n == 1) Achievement.unlock("uart_receive_data");
  }
}

void UartTerminalScreen::_sendCommand() {
  String cmd = InputTextAction::popup("Send command", "", _hexMode ? InputTextAction::INPUT_HEX : InputTextAction::INPUT_TEXT);
  if (!InputTextAction::wasCancelled() && cmd.length() > 0) {
    if (_hexMode) {
      String hex = cmd;
      hex.replace(" ", "");
      hex.toUpperCase();
      for (int i = 0; i + 1 < (int)hex.length(); i += 2) {
        char hi = hex[i], lo = hex[i + 1];
        if (isxdigit(hi) && isxdigit(lo)) {
          uint8_t b = (uint8_t)(
            (isdigit(hi) ? hi - '0' : hi - 'A' + 10) << 4 |
            (isdigit(lo) ? lo - '0' : lo - 'A' + 10));
          _serial.write(b);
        }
      }
    } else {
      _serial.print(cmd);
      _serial.write('\r');
      _serial.write('\n');
    }
    char echo[68];
    snprintf(echo, sizeof(echo), _hexMode ? "> [H] %s" : "> %s", cmd.c_str());
    _log.addLine(echo, TFT_CYAN);
    if (_logFile) _logFile.println(echo);
    _broadcastLine(echo);
    int n = Achievement.inc("uart_send_command");
    if (n == 1) Achievement.unlock("uart_send_command");
  }
  render();
}

// ── Log file ──────────────────────────────────────────────────────────────────

void UartTerminalScreen::_openLog() {
  Uni.Storage->makeDir("/unigeek/utility/uart");
  String path = "/unigeek/utility/uart/" + _saveFilename + ".log";
  _logFile = Uni.Storage->open(path.c_str(), "w");
  char buf[40];
  if (_logFile) {
    snprintf(buf, sizeof(buf), "Filename: %s.log", _saveFilename.c_str());
    _log.addLine(buf, TFT_DARKGREY);
    int n = Achievement.inc("uart_log_saved");
    if (n == 1) Achievement.unlock("uart_log_saved");
  } else {
    _log.addLine("Log open failed", TFT_RED);
  }
}

void UartTerminalScreen::_closeLog() {
  if (!_logFile) return;
  _logFile.close();
}

// ── WiFi stream ───────────────────────────────────────────────────────────────

void UartTerminalScreen::_startWifiStream() {
  if (_saveMode == SAVE_STREAM_AP) {
    WiFi.softAP(_apName.c_str());
    delay(100);
    _apStarted = true;
    _tcpServer.begin();
    _wifiStarted = true;
    char buf[56];
    snprintf(buf, sizeof(buf), "AP: %s", _apName.c_str());
    _log.addLine(buf, TFT_DARKGREY);
    snprintf(buf, sizeof(buf), "Stream: nc %s:%d", WiFi.softAPIP().toString().c_str(), TCP_PORT);
    _log.addLine(buf, TFT_DARKGREY);
    int na = Achievement.inc("uart_stream_ap");
    if (na == 1) Achievement.unlock("uart_stream_ap");
  } else {
    // SAVE_STREAM_NET — connection already established in _configNetwork
    if (WiFi.status() != WL_CONNECTED) {
      _log.addLine("WiFi not connected", TFT_RED);
      return;
    }
    _tcpServer.begin();
    _wifiStarted = true;
    char buf[56];
    snprintf(buf, sizeof(buf), "Network: %s", _networkSSID.c_str());
    _log.addLine(buf, TFT_DARKGREY);
    snprintf(buf, sizeof(buf), "Stream: nc %s:%d", WiFi.localIP().toString().c_str(), TCP_PORT);
    _log.addLine(buf, TFT_DARKGREY);
    int nn = Achievement.inc("uart_stream_net");
    if (nn == 1) Achievement.unlock("uart_stream_net");
  }
}

void UartTerminalScreen::_stopWifiStream() {
  if (!_wifiStarted) return;
  for (int i = 0; i < MAX_TCP_CLIENTS; i++) {
    if (_tcpClients[i]) _tcpClients[i].stop();
  }
  _tcpServer.stop();
  if (_apStarted) {
    WiFi.softAPdisconnect(true);
    _apStarted = false;
  } else {
    WiFi.disconnect();
  }
  _wifiStarted = false;
}

void UartTerminalScreen::_acceptClients() {
  if (!_wifiStarted) return;
  if (!_tcpServer.hasClient()) return;
  for (int i = 0; i < MAX_TCP_CLIENTS; i++) {
    if (!_tcpClients[i] || !_tcpClients[i].connected()) {
      _tcpClients[i] = _tcpServer.available();
      return;
    }
  }
  _tcpServer.available().stop();  // no free slot
}

void UartTerminalScreen::_broadcastLine(const char* line) {
  if (!_wifiStarted) return;
  for (int i = 0; i < MAX_TCP_CLIENTS; i++) {
    if (_tcpClients[i] && _tcpClients[i].connected())
      _tcpClients[i].println(line);
  }
}

// ── Status bar ────────────────────────────────────────────────────────────────

void UartTerminalScreen::_statusBarCb(Sprite& sp, int barY, int w, void* user) {
  auto* s = static_cast<UartTerminalScreen*>(user);
  sp.setTextSize(1);

  char rx[5], tx[5];
  if (s->_rxPin >= 0) snprintf(rx, sizeof(rx), "%d", s->_rxPin); else strcpy(rx, "-");
  if (s->_txPin >= 0) snprintf(tx, sizeof(tx), "%d", s->_txPin); else strcpy(tx, "-");
  char left[28];
  snprintf(left, sizeof(left), "B%dR%sT%s", s->_baud, rx, tx);

  sp.setTextDatum(TL_DATUM);
  sp.setTextColor(TFT_DARKGREY);
  sp.drawString(left, 2, barY);

  sp.setTextDatum(TR_DATUM);
  sp.setTextColor(s->_hexMode ? TFT_YELLOW : TFT_DARKGREY);
  sp.drawString(s->_hexMode ? "HEX" : "STR", w - 2, barY);
}