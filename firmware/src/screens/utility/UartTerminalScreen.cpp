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
#include <time.h>
#include <driver/gpio.h>

// ── Lifecycle ─────────────────────────────────────────────────────────────────

UartTerminalScreen::~UartTerminalScreen() {
  if (_taskHandle) { vTaskDelete(_taskHandle); _taskHandle = nullptr; }
  if (_mutex)      { vSemaphoreDelete(_mutex); _mutex = nullptr; }
  if (_serialRunning) _serial.end();
}

const char* UartTerminalScreen::title() {
  return (_state == STATE_RUNNING) ? "UART Terminal" : "UART Setup";
}

void UartTerminalScreen::onInit() {
  _rxPin = PinConfig.getInt(PIN_CONFIG_EXT_SCL, PIN_CONFIG_EXT_SCL_DEFAULT);
  _txPin = PinConfig.getInt(PIN_CONFIG_EXT_SDA, PIN_CONFIG_EXT_SDA_DEFAULT);
  _updateLabels();

  _configItems[0] = {"Baud Rate",  _baudLabel};
  _configItems[1] = {"RX GPIO",    _rxLabel};
  _configItems[2] = {"TX GPIO",    _txLabel};
  _configItems[3] = {"Save File",  _saveLabel};
  _configItems[4] = {"Start",      nullptr};
  setItems(_configItems, 5);
}

void UartTerminalScreen::onUpdate() {
  if (_state == STATE_CONFIG) {
    ListScreen::onUpdate();
    return;
  }

  // STATE_RUNNING
  _drainShared();

  if (millis() - _lastDrawMs >= 200) {
    _lastDrawMs = millis();
    render();
  }

  if (!Uni.Nav->wasPressed()) return;
  auto dir = Uni.Nav->readDirection();
  if (dir == INavigation::DIR_BACK) {
    if (_saveFilename.length() > 0 && _saveBuffer.length() > 0) _saveLog();
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
    case 3: _configSaveFile(); break;
    case 4: _startTerminal();  break;
    default:                   break;
  }
}

// ── Config helpers ────────────────────────────────────────────────────────────

void UartTerminalScreen::_updateLabels() {
  snprintf(_baudLabel, sizeof(_baudLabel), "%d", _baud);
  if (_rxPin < 0) snprintf(_rxLabel, sizeof(_rxLabel), "none");
  else            snprintf(_rxLabel, sizeof(_rxLabel), "%d",  _rxPin);
  if (_txPin < 0) snprintf(_txLabel, sizeof(_txLabel), "none");
  else            snprintf(_txLabel, sizeof(_txLabel), "%d",  _txPin);
  if (_saveFilename.length() == 0) snprintf(_saveLabel, sizeof(_saveLabel), "Off");
  else _saveFilename.toCharArray(_saveLabel, sizeof(_saveLabel));
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

void UartTerminalScreen::_configSaveFile() {
  if (_saveFilename.length() > 0) {
    _saveFilename = "";
  } else {
    char def[28] = "uart_log";
    time_t now = time(nullptr);
    if (now > 1577836800L) {
      struct tm* t = localtime(&now);
      snprintf(def, sizeof(def), "%04d%02d%02d_%02d%02d%02d",
               t->tm_year + 1900, t->tm_mon + 1, t->tm_mday,
               t->tm_hour, t->tm_min, t->tm_sec);
    }
    String v = InputTextAction::popup("Log filename", def);
    if (!InputTextAction::wasCancelled() && v.length() > 0) _saveFilename = v;
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
  if (_saveFilename.length() > 0) {
    char buf[40];
    snprintf(buf, sizeof(buf), "Log -> %s.log", _saveFilename.c_str());
    _log.addLine(buf, TFT_DARKGREY);
  }

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
      if (_saveLineCount < MAX_SAVE_LINES) {
        _saveBuffer += _rxLineBuf + "\n";
        _saveLineCount++;
      } else if (_saveLineCount == MAX_SAVE_LINES) {
        _saveBuffer += "...(truncated)\n";
        _saveLineCount++;
      }
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
    if (_saveLineCount < MAX_SAVE_LINES) {
      _saveBuffer += echo;
      _saveBuffer += "\n";
      _saveLineCount++;
    }
    int n = Achievement.inc("uart_send_command");
    if (n == 1) Achievement.unlock("uart_send_command");
  }
  render();
}

void UartTerminalScreen::_saveLog() {
  Uni.Storage->makeDir("/unigeek/utility/uart");
  String path = "/unigeek/utility/uart/" + _saveFilename + ".log";
  fs::File f  = Uni.Storage->open(path.c_str(), "w");
  if (f) {
    f.print(_saveBuffer);
    f.close();
    int n = Achievement.inc("uart_log_saved");
    if (n == 1) Achievement.unlock("uart_log_saved");
    ShowStatusAction::show("Log saved!", 1000);
  } else {
    ShowStatusAction::show("Save failed", 1000);
  }
}

void UartTerminalScreen::_statusBarCb(Sprite& sp, int barY, int w, void* user) {
  auto* s = static_cast<UartTerminalScreen*>(user);
  char buf[48];
  snprintf(buf, sizeof(buf), "%d bd  RX:%d TX:%d", s->_baud, s->_rxPin, s->_txPin);
  sp.setTextSize(1);
  sp.setTextDatum(TL_DATUM);
  sp.setTextColor(TFT_DARKGREY);
  sp.drawString(buf, 2, barY);
  sp.setTextColor(s->_hexMode ? TFT_YELLOW : TFT_DARKGREY);
  sp.setTextDatum(TR_DATUM);
  sp.drawString(s->_hexMode ? "HEX" : "STR", w - 2, barY);
}

