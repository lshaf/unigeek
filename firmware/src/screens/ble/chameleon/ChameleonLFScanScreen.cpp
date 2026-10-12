#include "ChameleonLFScanScreen.h"
#include "core/AchievementManager.h"
#include "core/Device.h"
#include "core/ScreenManager.h"
#include "ui/actions/InputSelectAction.h"
#include "ui/actions/InputTextAction.h"
#include "ui/actions/ShowStatusAction.h"
#include "ui/components/StatusBar.h"
#include "ui/components/TagPrompt.h"
#include "utils/IdentityFile.h"
#include "utils/ble/ChameleonClient.h"

void ChameleonLFScanScreen::_addRow(const char* label, const String& value) {
  if (_rowCount >= kMaxRows) return;
  _rowLabels[_rowCount] = label;
  _rowValues[_rowCount] = value;
  _rows[_rowCount] = {_rowLabels[_rowCount].c_str(), _rowValues[_rowCount]};
  _rowCount++;
}

void ChameleonLFScanScreen::_draw() {
  _needsDraw = false;
  TagPrompt::show("Waiting for tag...", bodyX(), bodyY(), bodyW(), bodyH());
}

void ChameleonLFScanScreen::_buildResult() {
  _rowCount = 0;

  LFCodec::DecodedData decoded;
  if (!LFCodec::decode(_protocol, _data, _dataLen, decoded)) return;

  const LFCodec::FormatInfo* info = LFCodec::format(decoded.protocol);
  if (!info) return;
  LFCodec::Field fields[3];
  const size_t fieldCount = LFCodec::fields(decoded, fields, 3);
  for (size_t i = 0; i < fieldCount; ++i) _addRow(fields[i].label, fields[i].value);

  _addRow("Format", info->name);
  _addRow("Frequency", "125 kHz");
  _addRow("[Press]", "Actions");
  _scrollView.resetScroll();
  _scrollView.setRows(_rows, _rowCount);
}

void ChameleonLFScanScreen::_doScan() {
  _scanning = true;
  render();

  auto& c = ChameleonClient::get();
  uint8_t previousMode = 0;
  const bool restoreMode = c.getMode(&previousMode);
  c.setMode(1);

  _protocol = LFCodec::Protocol::Unknown;
  _dataLen = 0;
  bool found = false;

  if (c.scanEM410X(_data)) {
    _protocol = LFCodec::Protocol::EM410X; _dataLen = 5; found = true;
  } else if (c.scanHIDProx(_data, &_dataLen)) {
    _protocol = LFCodec::Protocol::HIDProx; found = true;
  } else if (c.scanIoProx(_data, &_dataLen)) {
    _protocol = LFCodec::Protocol::IoProx; found = true;
  } else if (c.scanViking(_data, &_dataLen)) {
    _protocol = LFCodec::Protocol::Viking; found = true;
  } else if (c.scanPAC(_data, &_dataLen)) {
    _protocol = LFCodec::Protocol::PACStanley; found = true;
  } else if (c.scanJablotron(_data, &_dataLen)) {
    _protocol = LFCodec::Protocol::Jablotron; found = true;
  }

  if (restoreMode) c.setMode(previousMode);
  _scanning = false;

  if (!found) {
    _state = STATE_IDLE;
    _needsDraw = true;
    render();
    ShowStatusAction::show("Tag not detected", 1200);
    Screen.goBack();
    return;
  }

  _state = STATE_RESULT;
  _buildResult();
  _needsDraw = true;
  render();
}

void ChameleonLFScanScreen::onInit() {
  _state = STATE_IDLE;
  _needsDraw = true;
  _doScan();
}

void ChameleonLFScanScreen::onRender() {
  if (_state == STATE_RESULT) {
    _scrollView.render(bodyX(), bodyY(), bodyW(), bodyH());
    StatusBar::refresh();
    return;
  }
  if (_needsDraw) _draw();
}

void ChameleonLFScanScreen::onUpdate() {
  if (_scanning) return;
  if (!Uni.Nav->wasPressed()) return;

  auto dir = Uni.Nav->readDirection();
  if (dir == INavigation::DIR_BACK) {
    Screen.goBack();
    return;
  }
  if (dir == INavigation::DIR_PRESS) {
    if (_state == STATE_RESULT) _showActions();
    return;
  }
  if (_state == STATE_RESULT) _scrollView.onNav(dir);
}

const char* ChameleonLFScanScreen::_protocolName() const {
  const LFCodec::FormatInfo* info = LFCodec::format(_protocol);
  return info ? info->name : "LF";
}

void ChameleonLFScanScreen::_loadToSlot() {
  auto& c = ChameleonClient::get();
  bool ok = false;
  if (_protocol == LFCodec::Protocol::EM410X && _dataLen == 5) ok = c.setEM410XSlot(_data);
  else if (_protocol == LFCodec::Protocol::HIDProx) ok = c.setHIDProxSlot(_data, _dataLen);
  else if (_protocol == LFCodec::Protocol::IoProx && _dataLen == 16) ok = c.setIoProxSlot(_data);
  else if (_protocol == LFCodec::Protocol::Viking) ok = c.setVikingSlot(_data, _dataLen);
  else if (_protocol == LFCodec::Protocol::PACStanley && _dataLen == 8) ok = c.setPACSlot(_data);
  else if (_protocol == LFCodec::Protocol::Jablotron && _dataLen == 5) ok = c.setJablotronSlot(_data);
  if (ok) {
    c.setMode(0);
    int n = Achievement.inc("chameleon_clone");
    if (n == 1) Achievement.unlock("chameleon_clone");
    if (n == 3) Achievement.unlock("chameleon_clone_3");
    if (n == 10) Achievement.unlock("chameleon_clone_10");
  }
  render();
  ShowStatusAction::show(ok ? "Loaded to slot" : "Failed", 1200);
  render();
}

void ChameleonLFScanScreen::_saveId() {
  if (!Uni.Storage || !Uni.Storage->isAvailable() || !_dataLen) {
    ShowStatusAction::show("Failed", 1200); render(); return;
  }
  const LFCodec::FormatInfo* info = LFCodec::format(_protocol);
  String suggested = String(info ? info->filePrefix : "LF") + "_";
  char h[3];
  for (uint8_t i = 0; i < _dataLen; ++i) { snprintf(h, sizeof(h), "%02X", _data[i]); suggested += h; }
  String name = InputTextAction::popup("Save ID", suggested.c_str());
  if (InputTextAction::wasCancelled() || name.length() == 0) { render(); return; }
  if (name.endsWith(".id")) name.remove(name.length() - 3);
  const String filename = name + ".id";
  Uni.Storage->makeDir("/unigeek"); Uni.Storage->makeDir("/unigeek/rfid");
  Uni.Storage->makeDir("/unigeek/rfid/ids");
  const bool ok = IdentityFile::saveLfId(String("/unigeek/rfid/ids/") + filename,
                                          _protocol, _data, _dataLen);
  render();
  ShowStatusAction::show(ok ? (String("Saved: ") + filename).c_str() : "Failed", 1600);
  render();
}

void ChameleonLFScanScreen::_saveToFile() {
  if (!Uni.Storage || !Uni.Storage->isAvailable() || !_dataLen) {
    ShowStatusAction::show("Failed", 1200); render(); return;
  }
  const LFCodec::FormatInfo* info = LFCodec::format(_protocol);
  String suggested = String(info ? info->filePrefix : "LF") + "_";
  char h[3];
  for (uint8_t i = 0; i < _dataLen; ++i) { snprintf(h, sizeof(h), "%02X", _data[i]); suggested += h; }
  String name = InputTextAction::popup("Save LF Data", suggested.c_str());
  if (InputTextAction::wasCancelled() || name.length() == 0) { render(); return; }
  render();
  if (name.endsWith(".bin")) name.remove(name.length() - 4);
  String filename = name + ".bin";
  Uni.Storage->makeDir("/unigeek"); Uni.Storage->makeDir("/unigeek/rfid"); Uni.Storage->makeDir("/unigeek/rfid/data");
  String path = String("/unigeek/rfid/data/") + filename;
  fs::File file = Uni.Storage->open(path.c_str(), "w");
  bool ok = false;
  if (file) { ok = file.write(_data, _dataLen) == (size_t)_dataLen; file.close(); }
  render();
  if (ok) { String msg = String("Saved: ") + filename; ShowStatusAction::show(msg.c_str(), 1600); }
  else ShowStatusAction::show("Failed", 1200);
  render();
}

void ChameleonLFScanScreen::_showActions() {
  static const InputSelectAction::Option opts[] = {
    {"Load to Slot", "slot"}, {"Save ID", "id"}, {"Save LF Data", "save"},
  };
  String title = String(_protocolName()) + " Actions";
  const char* r = InputSelectAction::popup(title.c_str(), opts, 3, nullptr);
  if (r && strcmp(r, "slot") == 0) _loadToSlot();
  else if (r && strcmp(r, "id") == 0) _saveId();
  else if (r && strcmp(r, "save") == 0) _saveToFile();
  else render();
}
