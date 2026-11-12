#include "UartTerminalScreen.h"
#include "core/AchievementManager.h"
#include "core/PinConfigManager.h"
#include "ui/actions/InputSelectAction.h"
#include "ui/actions/InputNumberAction.h"
#include "utils/terminal/UartTransport.h"

namespace {
void countAchievement(const char* id) {
  if (Achievement.inc(id) == 1) Achievement.unlock(id);
}
}  // namespace

void UartTerminalScreen::onInit() {
  _rxPin = PinConfig.getInt(PIN_CONFIG_EXT_SCL, PIN_CONFIG_EXT_SCL_DEFAULT);
  _txPin = PinConfig.getInt(PIN_CONFIG_EXT_SDA, PIN_CONFIG_EXT_SDA_DEFAULT);
  TerminalScreen::onInit();
}

// ── Setup rows ────────────────────────────────────────────────────────────────

uint8_t UartTerminalScreen::_transportItems(ListItem* out, uint8_t max) {
  if (max < 3) return 0;

  snprintf(_baudLabel, sizeof(_baudLabel), "%d", _baud);
  if (_rxPin < 0) snprintf(_rxLabel, sizeof(_rxLabel), "none");
  else            snprintf(_rxLabel, sizeof(_rxLabel), "%d", _rxPin);
  if (_txPin < 0) snprintf(_txLabel, sizeof(_txLabel), "none");
  else            snprintf(_txLabel, sizeof(_txLabel), "%d", _txPin);

  out[0] = {"Baud Rate", _baudLabel};
  out[1] = {"RX GPIO",   _rxLabel};
  out[2] = {"TX GPIO",   _txLabel};
  return 3;
}

void UartTerminalScreen::_onTransportItem(uint8_t index) {
  switch (index) {
    case 0: _configBaud(); break;
    case 1: _configRx();   break;
    case 2: _configTx();   break;
    default:               break;
  }
  _refreshItems();
}

void UartTerminalScreen::_configBaud() {
  static constexpr InputSelectAction::Option bauds[] = {
    {"9600",   "9600"},  {"19200",  "19200"}, {"38400",  "38400"},
    {"57600",  "57600"}, {"115200", "115200"}, {"230400", "230400"},
  };
  char cur[8];
  snprintf(cur, sizeof(cur), "%d", _baud);
  const char* v = InputSelectAction::popup("Baud Rate", bauds, 6, cur);
  if (v) _baud = atoi(v);
}

void UartTerminalScreen::_configRx() {
  int pin = InputNumberAction::popup("RX GPIO (0=none)", 0, 48, _rxPin >= 0 ? _rxPin : 0);
  if (!InputNumberAction::wasCancelled()) _rxPin = (pin == 0) ? -1 : pin;
}

void UartTerminalScreen::_configTx() {
  int pin = InputNumberAction::popup("TX GPIO (0=none)", 0, 48, _txPin >= 0 ? _txPin : 0);
  if (!InputNumberAction::wasCancelled()) _txPin = (pin == 0) ? -1 : pin;
}

TerminalTransport* UartTerminalScreen::_createTransport() {
  return new UartTransport(_baud, _rxPin, _txPin);
}

// ── Achievements ──────────────────────────────────────────────────────────────

void UartTerminalScreen::_onSessionStarted() {
  _receivedThisSession = false;
  countAchievement("uart_first_connect");
}

void UartTerminalScreen::_onDataReceived() {
  // Once per session: Achievement.inc() persists to storage on every call.
  if (_receivedThisSession) return;
  _receivedThisSession = true;
  countAchievement("uart_receive_data");
}

void UartTerminalScreen::_onDataSent() {
  countAchievement("uart_send_command");
}

void UartTerminalScreen::_onLogStarted(TerminalLog::Mode mode) {
  if      (mode == TerminalLog::MODE_FILE)       countAchievement("uart_log_saved");
  else if (mode == TerminalLog::MODE_STREAM_AP)  countAchievement("uart_stream_ap");
  else if (mode == TerminalLog::MODE_STREAM_NET) countAchievement("uart_stream_net");
}
