#include "PrinterPrankScreen.h"
#include <WiFi.h>
#include "core/ScreenManager.h"
#include "core/AchievementManager.h"
#include "ui/actions/InputTextAction.h"
#include "ui/actions/ShowStatusAction.h"
#include "ui/views/ProgressView.h"

const PrinterPrankScreen::MessagePreset PrinterPrankScreen::PRESETS[] = {
  {"Affirmation",
   "BEEP BOOP\r\n\r\n"
   "This is your printer's daily\r\n"
   "affirmation. You are doing\r\n"
   "great. Keep going.\r\n\r\n"
   "  -- The Network"},
  {"Hydrate",
   "REMINDER\r\n\r\n"
   "Hydrate. The void cares\r\n"
   "about you.\r\n\r\n"
   "  -- Your Friendly Printer"},
  {"Compliment",
   "Anonymous Compliment Service\r\n"
   "----------------------------\r\n\r\n"
   "Whoever you are, today: you\r\n"
   "are doing better than you\r\n"
   "think. Have a great one.\r\n"},
  {"Update Required",
   "*** PRINTER UPDATE REQUIRED ***\r\n\r\n"
   "Please ignore this message.\r\n"
   "It is an exercise in irony.\r\n\r\n"
   "Thank you for your patience."},
  {"Hello Void",
   "Hello from the void.\r\n\r\n"
   "Have a nice day."},
};
const uint8_t PrinterPrankScreen::PRESET_COUNT =
  sizeof(PRESETS) / sizeof(PRESETS[0]);

PrinterPrankScreen::PrinterPrankScreen() {
  memset(_devices,     0, sizeof(_devices));
  memset(_devItems,    0, sizeof(_devItems));
  memset(_configItems, 0, sizeof(_configItems));
}

void PrinterPrankScreen::onInit() {
  if (WiFi.status() != WL_CONNECTED) {
    ShowStatusAction::show("Not connected", 1500);
    Screen.goBack();
    return;
  }
  _showConfig();
}

void PrinterPrankScreen::onBack() {
  if (_state == STATE_DEVICES) { _showConfig(); return; }
  Screen.goBack();
}

void PrinterPrankScreen::onItemSelected(uint8_t index) {
  if (_state == STATE_CONFIG) {
    switch (index) {
      case 0: _pickPreset(); break;
      case 1: _setCustom();  break;
      case 2: _discover();   break;
    }
  } else if (_state == STATE_DEVICES) {
    _print(index);
  }
}

const char* PrinterPrankScreen::_currentBody() const {
  if (!_customMsg.isEmpty()) return _customMsg.c_str();
  return PRESETS[_presetIdx].body;
}

const char* PrinterPrankScreen::_currentLabel() const {
  if (!_customMsg.isEmpty()) return "Custom";
  return PRESETS[_presetIdx].label;
}

void PrinterPrankScreen::_showConfig() {
  _state = STATE_CONFIG;
  _msgSub = _currentLabel();
  _configItems[0] = {"Message",         _msgSub.c_str()};
  _configItems[1] = {"Custom Message"};
  _configItems[2] = {"Discover & Print"};
  setItems(_configItems, 3);
}

void PrinterPrankScreen::_pickPreset() {
  _presetIdx = (_presetIdx + 1) % PRESET_COUNT;
  _customMsg = "";
  _showConfig();
}

void PrinterPrankScreen::_setCustom() {
  String prev = _customMsg.isEmpty() ? String("Hello!") : _customMsg;
  String entered = InputTextAction::popup("Custom Message", prev);
  if (!entered.isEmpty()) _customMsg = entered;
  _showConfig();
}

void PrinterPrankScreen::_discover() {
  _state    = STATE_DISCOVERING;
  _devCount = 0;
  memset(_devices, 0, sizeof(_devices));

  int n1 = Achievement.inc("wifi_printer_prank_first");
  if (n1 == 1) Achievement.unlock("wifi_printer_prank_first");

  ProgressView::init();
  _devCount = PrinterPrankUtil::discover(
    _devices, PrinterPrankUtil::MAX_DEVICES,
    [](uint8_t pct) { ProgressView::progress("Searching printers...", pct); }
  );
  ProgressView::finish();

  _showDevices();
}

void PrinterPrankScreen::_showDevices() {
  _state = STATE_DEVICES;

  if (_devCount == 0) {
    ShowStatusAction::show("No printers found", 1500);
    _showConfig();
    return;
  }

  for (uint8_t i = 0; i < _devCount; i++) {
    _devSubs[i]  = _devices[i].ip;
    _devItems[i] = { _devices[i].name, _devSubs[i].c_str() };
  }
  uint8_t total = _devCount;
  if (_devCount > 1) {
    _devItems[_devCount] = { "Print to All" };
    total = _devCount + 1;
  }
  setItems(_devItems, total);
}

void PrinterPrankScreen::_print(uint8_t index) {
  _state = STATE_PRINTING;

  const char* body = _currentBody();
  if (!body || !*body) {
    ShowStatusAction::show("Message not set");
    _showDevices();
    return;
  }

  bool anyHit = false;

  if (index < _devCount) {
    String msg = "Printing on ";
    msg += _devices[index].name;
    msg += "...";
    ShowStatusAction::show(msg.c_str(), 0);
    bool ok = PrinterPrankUtil::printText(_devices[index], body);
    ShowStatusAction::show(ok ? "Sent" : "Failed", 1200);
    anyHit = ok;
  } else {
    uint8_t hits = 0;
    for (uint8_t i = 0; i < _devCount; i++) {
      String msg = "Printing (";
      msg += String(i + 1);
      msg += "/";
      msg += String(_devCount);
      msg += ")...";
      ShowStatusAction::show(msg.c_str(), 0);
      if (PrinterPrankUtil::printText(_devices[i], body)) hits++;
    }
    String done = "Sent ";
    done += String(hits);
    done += "/";
    done += String(_devCount);
    done += " jobs";
    ShowStatusAction::show(done.c_str(), 1500);
    anyHit = hits > 0;
  }

  if (anyHit) {
    int n2 = Achievement.inc("wifi_printer_prank_hit");
    if (n2 == 1) Achievement.unlock("wifi_printer_prank_hit");
  }

  _showDevices();
}
