#include "CctvSnifferScreen.h"
#include "utils/network/TargetResolveUtil.h"
#include "core/Device.h"
#include "core/ScreenManager.h"
#include "ui/actions/InputTextAction.h"
#include "ui/actions/InputNumberAction.h"
#include "ui/actions/ShowStatusAction.h"
#include "ui/views/ProgressView.h"
#include "utils/network/ScanCancelUtil.h"
#include <WiFi.h>
#include <TJpg_Decoder.h>

namespace {
static const char* gCameraProgressLabel = nullptr;

static void cameraPortProgress(uint8_t pct)
{
  if (!gCameraProgressLabel) return;
  ProgressView::progress(
    gCameraProgressLabel,
    (uint8_t)((uint16_t)pct * 70 / 100)
  );
}
}

CctvSnifferScreen* CctvSnifferScreen::_instance = nullptr;

// ── Lifecycle ───────────────────────────────────────────────────────────────

void CctvSnifferScreen::onInit()
{
  _username = "admin";
  memset(_hosts,       0, sizeof(_hosts));
  memset(_cameras,     0, sizeof(_cameras));
  memset(_cameraItems, 0, sizeof(_cameraItems));
  memset(_configItems, 0, sizeof(_configItems));
  _showConfig();
}

void CctvSnifferScreen::onBack()
{
  switch (_state) {
    case STATE_SCANNING:
      break;

    case STATE_STREAMING:
      _stopStream();
      _showCameraMenu(_selectedCamera);
      break;

    case STATE_CAMERA_MENU:
      _showCameraList();
      break;

    case STATE_CAMERA_LIST:
      _showConfig();
      break;

    default:
      Screen.goBack();
      break;
  }
}

void CctvSnifferScreen::onItemSelected(uint8_t index)
{
  if (_state == STATE_CONFIG) {
    if (index >= _configCount) return;

    switch (_configActions[index]) {
      case CFG_MODE:
        _scanMode = (_scanMode == MODE_RANGE) ? MODE_TARGETS : MODE_RANGE;
        _showConfig(index);
        break;

      case CFG_TARGET_1:
        _editTarget(0, index);
        break;
      case CFG_TARGET_2:
        _editTarget(1, index);
        break;
      case CFG_TARGET_3:
        _editTarget(2, index);
        break;
      case CFG_TARGET_4:
        _editTarget(3, index);
        break;

      case CFG_START_IP: {
        int value = InputNumberAction::popup("Start IP", 1, _endIp, _startIp);
        if (!InputNumberAction::wasCancelled()) _startIp = value;
        _showConfig(index);
        break;
      }

      case CFG_END_IP: {
        int value = InputNumberAction::popup("End IP", _startIp, 254, _endIp);
        if (!InputNumberAction::wasCancelled()) _endIp = value;
        _showConfig(index);
        break;
      }

      case CFG_START_SCAN:
        _startScan();
        break;
    }
    return;
  }

  if (_state == STATE_CAMERA_LIST) {
    if (_cameraCount > 0 && index < _cameraCount) {
      _showCameraMenu(index);
    }
    return;
  }

  if (_state == STATE_CAMERA_MENU) {
    switch (index) {
      case 0: {
        String u = InputTextAction::popup("Username", _username);
        if (u.length()) _username = u;
        _showCameraMenu(_selectedCamera);
        break;
      }

      case 1: {
        String p = InputTextAction::popup("Password", _password);
        if (!InputTextAction::wasCancelled()) _password = p;
        _showCameraMenu(_selectedCamera);
        break;
      }

      case 2:
        _startStream();
        break;

      case 3:
        _showCameraList();
        break;
    }
  }
}

void CctvSnifferScreen::onUpdate()
{
  if (_state == STATE_SCANNING) return;

  if (_state == STATE_STREAMING) {
    if (_stream.isConnected()) {
      _stream.readFrame(_onFrame, this);

      _frameCount++;
      unsigned long now = millis();
      if (now - _lastFrame >= 1000) {
        _fps = _frameCount * 1000.0f / (now - _lastFrame);
        _frameCount = 0;
        _lastFrame = now;

        char fpsBuf[16];
        snprintf(fpsBuf, sizeof(fpsBuf), "%.1f FPS", _fps);
        Uni.Lcd.setTextColor(TFT_YELLOW, TFT_BLACK);
        Uni.Lcd.setTextDatum(TR_DATUM);
        Uni.Lcd.drawString(fpsBuf, bodyX() + bodyW() - 2, bodyY() + 2);
      }
    } else {
      ShowStatusAction::show("Stream disconnected", 1500);
      _stopStream();
      _showCameraMenu(_selectedCamera);
      return;
    }

    if (Uni.Nav->wasPressed()) {
      auto dir = Uni.Nav->readDirection();
      if (dir == INavigation::DIR_BACK || dir == INavigation::DIR_PRESS) {
        _stopStream();
        _showCameraMenu(_selectedCamera);
      }
    }
    return;
  }

  ListScreen::onUpdate();
}

// ── Config ──────────────────────────────────────────────────────────────────

void CctvSnifferScreen::_showConfig(uint8_t selectedIndex)
{
  _state = STATE_CONFIG;
  _configCount = 0;

  auto add = [&](const char* label, const char* sub, ConfigAction action) {
    _configItems[_configCount] = {label, sub};
    _configActions[_configCount] = action;
    _configCount++;
  };

  add("Mode", _scanMode == MODE_RANGE ? "Range" : "Targets", CFG_MODE);

  if (_scanMode == MODE_TARGETS) {
    static const ConfigAction targetActions[MAX_TARGETS] = {
      CFG_TARGET_1, CFG_TARGET_2, CFG_TARGET_3, CFG_TARGET_4
    };
    static const char* targetLabels[MAX_TARGETS] = {
      "Target 1", "Target 2", "Target 3", "Target 4"
    };

    for (uint8_t i = 0; i < MAX_TARGETS; ++i) {
      _targetSubs[i] = _targets[i].length() > 0 ? _targets[i] : "-";
      add(targetLabels[i], _targetSubs[i].c_str(), targetActions[i]);
    }
  } else {
    _startIpSub = String(_startIp);
    _endIpSub   = String(_endIp);
    add("Start IP", _startIpSub.c_str(), CFG_START_IP);
    add("End IP",   _endIpSub.c_str(),   CFG_END_IP);
  }

  add("Start Scan", nullptr, CFG_START_SCAN);
  setItems(_configItems, _configCount, selectedIndex);
}

// ── Scanning ────────────────────────────────────────────────────────────────

void CctvSnifferScreen::_startScan()
{
  if (WiFi.localIP()[0] == 0 && WiFi.localIP()[1] == 0 &&
      WiFi.localIP()[2] == 0 && WiFi.localIP()[3] == 0) {
    ShowStatusAction::show("WiFi not connected");
    return;
  }

  if (_scanMode == MODE_TARGETS) {
    if (!_hasTargets()) {
      ShowStatusAction::show("Enter at least one target");
      return;
    }

    for (uint8_t i = 0; i < MAX_TARGETS; ++i) {
      if (_targets[i].length() > 0 && !TargetResolveUtil::isValidTarget(_targets[i])) {
        ShowStatusAction::show("Invalid target");
        return;
      }
    }
  }

  _state = STATE_SCANNING;
  _cameraCount = 0;
  memset(_hosts,       0, sizeof(_hosts));
  memset(_cameras,     0, sizeof(_cameras));
  memset(_cameraItems, 0, sizeof(_cameraItems));

  ScanCancelUtil::begin();
  ProgressView::init();

  if (_scanMode == MODE_TARGETS) {
    uint8_t targetCount = 0;
    for (uint8_t i = 0; i < MAX_TARGETS; ++i) {
      if (_targets[i].length() > 0) targetCount++;
    }

    uint8_t done = 0;
    for (uint8_t i = 0; i < MAX_TARGETS && _cameraCount < MAX_FOUND; ++i) {
      if (_targets[i].length() == 0) continue;
      if (ScanCancelUtil::poll()) break;

      char label[48];
      snprintf(
        label, sizeof(label),
        "Scanning %s (%u/%u)...",
        _targets[i].c_str(),
        (unsigned)(done + 1),
        (unsigned)targetCount
      );
      ProgressView::progress(label, 0);
      String resolved;
      if (!TargetResolveUtil::resolve(_targets[i], resolved)) {
        ShowStatusAction::show("Could not resolve target", 900);
        done++;
        continue;
      }
      _scanTarget(resolved.c_str(), label);
      done++;
    }
  } else {
    _scanRange();
  }

  ProgressView::finish();

  if (ScanCancelUtil::wasCancelled()) {
    _showConfig();
    return;
  }
  _showCameraList();
}

bool CctvSnifferScreen::_validIp(const String& ip) const
{
  int a, b, c, d;
  char tail;

  return sscanf(ip.c_str(), "%d.%d.%d.%d%c", &a, &b, &c, &d, &tail) == 4 &&
         a >= 0 && a <= 255 &&
         b >= 0 && b <= 255 &&
         c >= 0 && c <= 255 &&
         d >= 0 && d <= 255;
}

bool CctvSnifferScreen::_hasTargets() const
{
  for (uint8_t i = 0; i < MAX_TARGETS; ++i) {
    if (_targets[i].length() > 0) return true;
  }
  return false;
}

void CctvSnifferScreen::_editTarget(uint8_t targetIndex, uint8_t selectedIndex)
{
  if (targetIndex >= MAX_TARGETS) return;

  String initial = _targets[targetIndex].length() > 0
    ? _targets[targetIndex]
    : _networkPrefix();

  char title[16];
  snprintf(title, sizeof(title), "Target %u", (unsigned)(targetIndex + 1));

  String ip = InputTextAction::popup(
    title,
    initial.c_str(),
    InputTextAction::INPUT_IP_ADDRESS
  );

  if (!InputTextAction::wasCancelled()) {
    _targets[targetIndex] = ip;
  }

  _showConfig(selectedIndex);
}

void CctvSnifferScreen::_scanTarget(const char* ip, const char* label)
{
  if (!ip || !ip[0] || _cameraCount >= MAX_FOUND) return;

  gCameraProgressLabel = label;
  ProgressView::progress(label, 0);
  _scanHost(ip, label);
  ProgressView::progress(label, 100);
  gCameraProgressLabel = nullptr;
}

void CctvSnifferScreen::_scanRange()
{
  uint8_t hostCount = IpScanUtil::scan(
    (uint8_t)_startIp,
    (uint8_t)_endIp,
    _hosts,
    MAX_FOUND_HOSTS,
    false,
    [](uint8_t pct) {
      ProgressView::progress("Scanning hosts...", pct);
    },
    []() { return ScanCancelUtil::poll(); }
  );

  for (uint8_t i = 0; i < hostCount && _cameraCount < MAX_FOUND; i++) {
    if (ScanCancelUtil::poll()) break;
    char label[48];
    snprintf(
      label, sizeof(label),
      "Scanning %s (%u/%u)...",
      _hosts[i].ip,
      (unsigned)(i + 1),
      (unsigned)hostCount
    );
    ProgressView::progress(label, 0);
    _scanTarget(_hosts[i].ip, label);
  }
}

void CctvSnifferScreen::_scanHost(const char* ip, const char* label)
{
  CctvScanUtil::Camera tempCams[8];
  const bool patient = _scanMode == MODE_TARGETS;
  uint8_t found = CctvScanUtil::scanPorts(
    ip,
    tempCams,
    8,
    patient,
    cameraPortProgress
  );

  if (found == 0) {
    ProgressView::progress(label, 100);
    return;
  }

  for (uint8_t i = 0; i < found && _cameraCount < MAX_FOUND; i++) {
    bool detected = false;

    if (strcmp(tempCams[i].service, "RTSP") == 0) {
      detected = CctvScanUtil::probeRtsp(
        ip,
        tempCams[i].port,
        tempCams[i].brand,
        sizeof(tempCams[i].brand),
        patient
      );
    } else if (strcmp(tempCams[i].service, "RTMP") != 0) {
      detected = CctvScanUtil::detectBrand(
        ip,
        tempCams[i].port,
        tempCams[i].brand,
        sizeof(tempCams[i].brand),
        patient
      );
    }

    if (detected) {
      memcpy(
        &_cameras[_cameraCount],
        &tempCams[i],
        sizeof(CctvScanUtil::Camera)
      );
      _cameraCount++;
    }

    ProgressView::progress(
      label,
      (uint8_t)(70 + ((uint16_t)(i + 1) * 30 / found))
    );
  }
}

// ── Results ─────────────────────────────────────────────────────────────────

void CctvSnifferScreen::_showCameraList()
{
  _state = STATE_CAMERA_LIST;

  if (_cameraCount == 0) {
    _cameraItems[0] = {"No cameras found"};
    setItems(_cameraItems, 1);
    return;
  }

  for (uint8_t i = 0; i < _cameraCount; i++) {
    snprintf(
      _cameraLabels[i],
      sizeof(_cameraLabels[i]),
      "%s:%d",
      _cameras[i].ip,
      _cameras[i].port
    );
    _cameraItems[i] = {_cameraLabels[i], _cameras[i].brand};
  }

  setItems(_cameraItems, _cameraCount);
}

void CctvSnifferScreen::_showCameraMenu(uint8_t camIdx)
{
  _state = STATE_CAMERA_MENU;
  _selectedCamera = camIdx;

  _usernameSub = _username;
  _passwordSub = _password.length() ? _password : "(empty)";

  _menuItems[0] = {"Username", _usernameSub.c_str()};
  _menuItems[1] = {"Password", _passwordSub.c_str()};
  _menuItems[2] = {"Stream"};
  _menuItems[3] = {"< Back"};

  setItems(_menuItems, 4);
}

String CctvSnifferScreen::_networkPrefix() const
{
  IPAddress ip = WiFi.localIP();
  if (ip[0] == 0 && ip[1] == 0 && ip[2] == 0 && ip[3] == 0) return "";

  char prefix[16];
  snprintf(prefix, sizeof(prefix), "%u.%u.%u.", ip[0], ip[1], ip[2]);
  return String(prefix);
}

// ── Streaming ───────────────────────────────────────────────────────────────

void CctvSnifferScreen::_startStream()
{
  const char* ip = _cameras[_selectedCamera].ip;
  uint16_t port = _cameras[_selectedCamera].port;
  const char* user = _username.length() ? _username.c_str() : nullptr;
  const char* pass = _password.length() ? _password.c_str() : nullptr;

  char streamUrl[128];
  if (!CctvScanUtil::findStream(ip, port, user, pass, streamUrl, sizeof(streamUrl))) {
    snprintf(streamUrl, sizeof(streamUrl), "http://%s:%u/mjpg/video.mjpg", ip, port);
  }


  if (!_stream.begin(streamUrl, user, pass)) {
    ShowStatusAction::show("Stream failed!", 1500);
    _showCameraMenu(_selectedCamera);
    return;
  }

  _state = STATE_STREAMING;
  _instance = this;
  _frameCount = 0;
  _lastFrame = millis();
  _fps = 0;

  // Clear body for streaming
  Uni.Lcd.fillRect(bodyX(), bodyY(), bodyW(), bodyH(), TFT_BLACK);
}

void CctvSnifferScreen::_stopStream()
{
  _stream.stop();
  _instance = nullptr;
}

bool CctvSnifferScreen::_onFrame(const uint8_t* jpegBuf, size_t jpegLen, void* userData)
{
  auto* self = static_cast<CctvSnifferScreen*>(userData);
  self->_drawFrame(jpegBuf, jpegLen);
  return true;
}

bool CctvSnifferScreen::_tjpgCallback(int16_t x, int16_t y, uint16_t w, uint16_t h, uint16_t* bitmap)
{
  if (!_instance) return false;
  Uni.Lcd.pushImage(_instance->bodyX() + x, _instance->bodyY() + y, w, h, bitmap);
  return true;
}

void CctvSnifferScreen::_drawFrame(const uint8_t* jpegBuf, size_t jpegLen)
{
  TJpgDec.setJpgScale(1);
  TJpgDec.setSwapBytes(true);
  TJpgDec.setCallback(_tjpgCallback);

  // Get JPEG dimensions first to decide scale
  uint16_t jw = 0, jh = 0;
  TJpgDec.getJpgSize(&jw, &jh, jpegBuf, jpegLen);

  // Auto-scale to fit body
  if (jw > (uint16_t)bodyW() * 2 || jh > (uint16_t)bodyH() * 2) {
    TJpgDec.setJpgScale(4);
  } else if (jw > (uint16_t)bodyW() || jh > (uint16_t)bodyH()) {
    TJpgDec.setJpgScale(2);
  } else {
    TJpgDec.setJpgScale(1);
  }

  TJpgDec.drawJpg(0, 0, jpegBuf, jpegLen);
}
