#include "CctvSnifferScreen.h"
#include "core/Device.h"
#include "core/ScreenManager.h"
#include "screens/wifi/network/NetworkMenuScreen.h"
#include "ui/actions/InputTextAction.h"
#include "ui/actions/InputSelectOption.h"
#include "ui/actions/ShowStatusAction.h"
#include "ui/views/ProgressView.h"
#include "utils/network/IpScanUtil.h"
#include <WiFi.h>
#include <TJpg_Decoder.h>

CctvSnifferScreen* CctvSnifferScreen::_instance = nullptr;

// ── Lifecycle ───────────────────────────────────────────────────────────────

void CctvSnifferScreen::onInit()
{
  _username = "admin";
  _showConfig();
}

void CctvSnifferScreen::onBack()
{
  switch (_state) {
    case STATE_SCANNING:
      break;  // can't exit during scan
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
      Screen.setScreen(new NetworkMenuScreen());
      break;
  }
}

void CctvSnifferScreen::onItemSelected(uint8_t index)
{
  switch (_state) {
    case STATE_CONFIG:
      switch (index) {
        case 0: { // Mode
          static constexpr InputSelectAction::Option opts[] = {
            {"Local Network (LAN)", "lan"},
            {"Single IP",           "single"},
            {"File IP",             "file"},
          };
          const char* val = InputSelectAction::popup("Mode", opts, 3);
          if (val) {
            if (strcmp(val, "lan") == 0)         _mode = MODE_LAN;
            else if (strcmp(val, "single") == 0) _mode = MODE_SINGLE_IP;
            else if (strcmp(val, "file") == 0)   _mode = MODE_FILE_IP;
          }
          _showConfig();
          break;
        }
        case 1: // IP or File or Network (depends on mode)
          if (_mode == MODE_SINGLE_IP) {
            String ip = InputTextAction::popup("Target IP", _targetIp, true);
            if (ip.length()) _targetIp = ip;
            _showConfig();
          } else if (_mode == MODE_FILE_IP) {
            String file = InputTextAction::popup("File Path", _targetFile);
            if (file.length()) _targetFile = file;
            _showConfig();
          }
          // LAN mode: network info is display-only
          break;
        case 2: // Start
          _startScan();
          break;
      }
      break;

    case STATE_CAMERA_LIST:
      if (index < _cameraCount) {
        _showCameraMenu(index);
      }
      break;

    case STATE_CAMERA_MENU:
      switch (index) {
        case 0: { // Username
          String u = InputTextAction::popup("Username", _username);
          if (u.length()) _username = u;
          _showCameraMenu(_selectedCamera);
          break;
        }
        case 1: { // Password
          String p = InputTextAction::popup("Password", _password);
          _password = p;
          _showCameraMenu(_selectedCamera);
          break;
        }
        case 2: // Stream
          _startStream();
          break;
        case 3: // Back
          _showCameraList();
          break;
      }
      break;

    default:
      break;
  }
}

void CctvSnifferScreen::onUpdate()
{
  if (_state == STATE_SCANNING) {
    if (Uni.Nav->wasPressed()) {
      auto dir = Uni.Nav->readDirection();
      if (dir == INavigation::DIR_BACK || dir == INavigation::DIR_PRESS) {
        if (_cameraCount > 0) {
          _showCameraList();
        } else {
          _showConfig();
        }
        return;
      }
    }
    return;
  }

  if (_state == STATE_STREAMING) {
    if (_stream.isConnected()) {
      _stream.readFrame(_onFrame, this);

      _frameCount++;
      unsigned long now = millis();
      if (now - _lastFrame >= 1000) {
        _fps = _frameCount * 1000.0f / (now - _lastFrame);
        _frameCount = 0;
        _lastFrame = now;

        // Draw FPS overlay
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
        return;
      }
    }
    return;
  }

  ListScreen::onUpdate();
}

void CctvSnifferScreen::onRender()
{
  if (_state == STATE_SCANNING) { _drawLog(); return; }
  ListScreen::onRender();
}

// ── Config ──────────────────────────────────────────────────────────────────

void CctvSnifferScreen::_showConfig()
{
  _state = STATE_CONFIG;

  static const char* modeNames[] = {"LAN", "Single IP", "File IP"};
  _modeSub = modeNames[_mode];

  _configItems[0] = {"Mode", _modeSub.c_str()};

  if (_mode == MODE_SINGLE_IP) {
    _ipSub = _targetIp.length() ? _targetIp : "Not set";
    _configItems[1] = {"IP", _ipSub.c_str()};
  } else if (_mode == MODE_FILE_IP) {
    if (!_targetFile.length()) _targetFile = "/unigeek/utility/cctv/targets.txt";
    int lastSlash = _targetFile.lastIndexOf('/');
    _fileSub = lastSlash >= 0 ? _targetFile.substring(lastSlash + 1) : _targetFile;
    _configItems[1] = {"File", _fileSub.c_str()};
  } else {
    _ipSub = WiFi.SSID();
    _configItems[1] = {"Network", _ipSub.c_str()};
  }

  _configItems[2] = {"Start Scan"};
  setItems(_configItems, 3);
}

// ── Scanning ────────────────────────────────────────────────────────────────

void CctvSnifferScreen::_startScan()
{
  if (_mode == MODE_SINGLE_IP && !_targetIp.length()) {
    ShowStatusAction::show("Set IP first!", 1500);
    return;
  }

  _state = STATE_SCANNING;
  _log.clear();
  _cameraCount = 0;
  memset(_cameras, 0, sizeof(_cameras));
  setItems(nullptr, 0);

  switch (_mode) {
    case MODE_LAN:       _scanLAN();      break;
    case MODE_SINGLE_IP: _scanSingleIP(); break;
    case MODE_FILE_IP:   _scanFileIP();   break;
  }

  if (_cameraCount > 0) {
    char buf[40];
    snprintf(buf, sizeof(buf), "[+] Found %d camera(s)", _cameraCount);
    _log.addLine(buf);
    _log.addLine("BACK/Press for results");
  } else {
    _log.addLine("[!] No cameras found");
    _log.addLine("BACK/Press to return");
  }
  render();
}

void CctvSnifferScreen::_scanLAN()
{
  if (WiFi.localIP()[0] == 0) {
    _log.addLine("[!] Not connected");
    return;
  }

  _log.addLine("[*] Ping scanning LAN...");
  render();

  static IpScanUtil::Host hosts[64];
  uint8_t hostCount = IpScanUtil::scan(1, 254, hosts, 64, false,
    [](uint8_t pct) { ProgressView::show("Ping scanning...", pct * 40 / 100); });

  if (hostCount == 0) {
    _log.addLine("[!] No hosts found");
    return;
  }

  char buf[40];
  snprintf(buf, sizeof(buf), "[*] %d host(s), scanning ports...", hostCount);
  _log.addLine(buf);
  render();

  for (uint8_t i = 0; i < hostCount; i++) {
    snprintf(buf, sizeof(buf), "[*] Host: %s", hosts[i].ip);
    _log.addLine(buf);
    render();
    _scanHost(hosts[i].ip);
    ProgressView::show("Scanning...", 40 + i * 60 / hostCount);
  }

  snprintf(buf, sizeof(buf), "[*] %d host(s) on network", hostCount);
  _log.addLine(buf);
}

void CctvSnifferScreen::_scanSingleIP()
{
  char buf[40];
  snprintf(buf, sizeof(buf), "[*] Scanning %s...", _targetIp.c_str());
  _log.addLine(buf);
  render();
  _scanHost(_targetIp.c_str());
}

void CctvSnifferScreen::_scanFileIP()
{
  if (!Uni.Storage || !Uni.Storage->exists(_targetFile.c_str())) {
    _log.addLine("[!] File not found");
    return;
  }

  String content = Uni.Storage->readFile(_targetFile.c_str());
  int lineStart = 0;
  int ipCount = 0;

  while (lineStart < (int)content.length()) {
    int lineEnd = content.indexOf('\n', lineStart);
    if (lineEnd < 0) lineEnd = content.length();

    String line = content.substring(lineStart, lineEnd);
    line.trim();
    lineStart = lineEnd + 1;

    if (line.length() == 0 || line[0] == '#') continue;

    IPAddress ip;
    if (!ip.fromString(line)) continue;

    ipCount++;
    char buf[40];
    snprintf(buf, sizeof(buf), "[*] Scanning %s...", line.c_str());
    _log.addLine(buf);
    render();
    _scanHost(line.c_str());
  }

  char buf[40];
  snprintf(buf, sizeof(buf), "[*] %d IP(s) from file", ipCount);
  _log.addLine(buf);
}

void CctvSnifferScreen::_scanHost(const char* ip)
{
  CctvScanUtil::Camera tempCams[8];
  uint8_t found = CctvScanUtil::scanPorts(ip, tempCams, 8);

  if (found == 0) return;

  char buf[60];
  snprintf(buf, sizeof(buf), "    %d port(s) open", found);
  _log.addLine(buf);

  for (uint8_t i = 0; i < found && _cameraCount < MAX_FOUND; i++) {
    bool detected = false;

    if (strcmp(tempCams[i].service, "RTSP") == 0) {
      detected = CctvScanUtil::probeRtsp(ip, tempCams[i].port,
                                          tempCams[i].brand, sizeof(tempCams[i].brand));
    } else if (strcmp(tempCams[i].service, "RTMP") != 0) {
      detected = CctvScanUtil::detectBrand(ip, tempCams[i].port,
                                            tempCams[i].brand, sizeof(tempCams[i].brand));
    }

    if (detected) {
      memcpy(&_cameras[_cameraCount], &tempCams[i], sizeof(CctvScanUtil::Camera));
      _cameraCount++;

      snprintf(buf, sizeof(buf), "    [+] %s:%d (%s)",
               ip, tempCams[i].port, tempCams[i].brand);
      _log.addLine(buf);
      render();
    }
  }
}

// ── Results ─────────────────────────────────────────────────────────────────

void CctvSnifferScreen::_showCameraList()
{
  if (_cameraCount == 0) {
    _showConfig();
    return;
  }

  _state = STATE_CAMERA_LIST;

  for (uint8_t i = 0; i < _cameraCount; i++) {
    snprintf(_cameraLabels[i], sizeof(_cameraLabels[i]),
             "%s:%d", _cameras[i].ip, _cameras[i].port);
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

// ── Log ─────────────────────────────────────────────────────────────────────

void CctvSnifferScreen::_drawLog()
{
  _log.draw(Uni.Lcd, bodyX(), bodyY(), bodyW(), bodyH());
}

// ── Streaming ───────────────────────────────────────────────────────────────

void CctvSnifferScreen::_startStream()
{
  const char* ip = _cameras[_selectedCamera].ip;
  uint16_t port = _cameras[_selectedCamera].port;
  const char* user = _username.length() ? _username.c_str() : nullptr;
  const char* pass = _password.length() ? _password.c_str() : nullptr;

  char streamUrl[128];
  _log.addLine("[*] Finding stream...");
  render();

  if (!CctvScanUtil::findStream(ip, port, user, pass, streamUrl, sizeof(streamUrl))) {
    snprintf(streamUrl, sizeof(streamUrl), "http://%s:%u/mjpg/video.mjpg", ip, port);
    _log.addLine("[!] Trying default path");
    render();
  }

  char buf[60];
  snprintf(buf, sizeof(buf), "[*] Connecting %s:%u", ip, port);
  _log.addLine(buf);
  render();

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
