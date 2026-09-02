#include "FtpClientScreen.h"

#include "core/Device.h"
#include "core/INavigation.h"
#include "core/ScreenManager.h"
#include "ui/actions/InputTextAction.h"
#include "ui/actions/InputNumberAction.h"
#include "ui/actions/InputSelectAction.h"
#include "ui/actions/ShowStatusAction.h"
#include "ui/views/ProgressView.h"

#include <WiFi.h>
#include <WiFiClient.h>
#include <FS.h>
#include <new>

const char* FtpClientScreen::title() {
  switch (_state) {
    case STATE_REMOTE_LOADING:
    case STATE_REMOTE_BROWSER:
      return _remoteMode == REMOTE_UPLOAD_DEST ? "FTP Upload" : "FTP Download";
    case STATE_LOCAL_BROWSER:
      return "FTP Upload";
    case STATE_TRANSFER:
      return _transferIsUpload ? "FTP Upload" : "FTP Download";
    default:
      return "FTP Client";
  }
}

FtpClientScreen::~FtpClientScreen() {
  _closeConnection(false);
  uint32_t start = millis();
  while (_ftpTask && millis() - start < 2500) delay(10);
  if (_mutex && !_ftpTask) {
    vSemaphoreDelete(_mutex);
    _mutex = nullptr;
  }
}

void FtpClientScreen::onInit() {
  if (!_mutex) _mutex = xSemaphoreCreateMutex();
  _state = STATE_CONFIG;
  _updateLabels();
  _rebuildConfig();
}

void FtpClientScreen::onUpdate() {
  if (_state == STATE_CONNECTING) {
    if (_workerState == WORKER_READY) {
      _showActions();
      return;
    }
    if (_workerState == WORKER_FAILED || _workerState == WORKER_CLOSED) {
      String err = "FTP connection failed";
      if (_mutex && xSemaphoreTake(_mutex, pdMS_TO_TICKS(20)) == pdTRUE) {
        if (_commandError.length()) err = _commandError;
        xSemaphoreGive(_mutex);
      }
      _workerState = WORKER_IDLE;
      _state = STATE_CONFIG;
      _updateLabels();
      _rebuildConfig();
      ShowStatusAction::show(err.c_str(), 1700);
      render();
      return;
    }
    if (Uni.Nav->wasPressed() &&
        Uni.Nav->readDirection() == INavigation::DIR_BACK) {
      _stopRequested = true;
    }
    return;
  }

  if (_state == STATE_REMOTE_LOADING) {
    if (_workerState == WORKER_FAILED || _workerState == WORKER_CLOSED) {
      String err = "FTP session closed";
      if (_mutex && xSemaphoreTake(_mutex, pdMS_TO_TICKS(20)) == pdTRUE) {
        if (_commandError.length()) err = _commandError;
        xSemaphoreGive(_mutex);
      }
      ShowStatusAction::show(err.c_str(), 1500);
      _closeConnection(true);
      return;
    }

    if (_commandDone) {
      bool ok = _commandOk;
      _commandDone = false;
      if (!ok) {
        String err = "Directory read failed";
        if (_mutex && xSemaphoreTake(_mutex, pdMS_TO_TICKS(20)) == pdTRUE) {
          if (_commandError.length()) err = _commandError;
          xSemaphoreGive(_mutex);
        }
        ShowStatusAction::show(err.c_str(), 1500);
        _state = (_remoteMode == REMOTE_UPLOAD_DEST)
               ? STATE_LOCAL_BROWSER : STATE_REMOTE_BROWSER;
        render();
        return;
      }
      _copyRemoteListing();
      _state = STATE_REMOTE_BROWSER;
      render();
      return;
    }

    if (Uni.Nav->wasPressed() &&
        Uni.Nav->readDirection() == INavigation::DIR_BACK) {
      if (_remoteMode == REMOTE_UPLOAD_DEST) {
        _state = STATE_LOCAL_BROWSER;
        render();
      } else {
        _showActions();
      }
    }
    return;
  }

  if (_state == STATE_TRANSFER) {
    _updateTransferProgress();

    if (_commandDone) {
      bool ok = _commandOk;
      String err;
      if (_mutex && xSemaphoreTake(_mutex, pdMS_TO_TICKS(20)) == pdTRUE) {
        err = _commandError;
        xSemaphoreGive(_mutex);
      }
      _commandDone = false;
      ProgressView::finish();

      bool upload = _transferIsUpload;
      _state = upload ? STATE_LOCAL_BROWSER : STATE_REMOTE_BROWSER;
      render();

      if (ok) {
        ShowStatusAction::show(upload ? "Upload complete" : "Download complete", 1400);
      } else if (err == "Cancelled") {
        ShowStatusAction::show(upload ? "Upload cancelled" : "Download cancelled", 1200);
      } else {
        ShowStatusAction::show(
          err.length() ? err.c_str() : (upload ? "Upload failed" : "Download failed"),
          1600);
      }
      render();
      return;
    }

    if (Uni.Nav->wasPressed() &&
        Uni.Nav->readDirection() == INavigation::DIR_BACK) {
      _cancelTransfer = true;
    }
    return;
  }

  if (_state == STATE_LOCAL_BROWSER) {
    if (Uni.Nav->isPressed() && !_holdHandled &&
        Uni.Nav->heldDuration() >= HOLD_MS) {
      _holdHandled = true;
      Uni.Nav->suppressCurrentPress();
      _holdLocalUpload(_selectedIndex);
      return;
    }
    if (!Uni.Nav->isPressed()) _holdHandled = false;
    ListScreen::onUpdate();
    return;
  }

  if (_state == STATE_REMOTE_BROWSER) {
    if (Uni.Nav->isPressed() && !_holdHandled &&
        Uni.Nav->heldDuration() >= HOLD_MS) {
      _holdHandled = true;
      Uni.Nav->suppressCurrentPress();
      _holdRemote(_selectedIndex);
      return;
    }
    if (!Uni.Nav->isPressed()) _holdHandled = false;
    ListScreen::onUpdate();
    return;
  }

  ListScreen::onUpdate();
}

void FtpClientScreen::onRender() {
  if (_state == STATE_CONNECTING || _state == STATE_REMOTE_LOADING) {
    _renderConnecting();
    return;
  }
  if (_state == STATE_TRANSFER) return;
  if (_state == STATE_REMOTE_BROWSER) {
    _renderRemoteBrowser();
    return;
  }
  if (_state == STATE_LOCAL_BROWSER) {
    _renderLocalBrowser();
    return;
  }
  ListScreen::onRender();
}

void FtpClientScreen::onItemSelected(uint8_t index) {
  if (_state == STATE_ACTION) {
    if (index == 0) _beginDownload();
    else if (index == 1) _beginUpload();
    return;
  }

  if (_state == STATE_REMOTE_BROWSER) {
    _selectRemote(index);
    return;
  }

  if (_state == STATE_LOCAL_BROWSER) {
    _selectLocalUpload(index);
    return;
  }

  if (_state != STATE_CONFIG) return;

  switch (index) {
    case 0: _configHost(); break;
    case 1: _configPort(); break;
    case 2: _configUsername(); break;
    case 3: _configPassword(); break;
    case 4: _connect(); break;
    default: break;
  }
}

void FtpClientScreen::onBack() {
  if (_state == STATE_ACTION) {
    _closeConnection(false);
    Screen.goBack();
    return;
  }
  if (_state == STATE_CONNECTING || _state == STATE_REMOTE_LOADING) {
    _stopRequested = true;
    return;
  }
  if (_state == STATE_TRANSFER) {
    _cancelTransfer = true;
    return;
  }
  if (_state == STATE_REMOTE_BROWSER) {
    if (_remoteMode == REMOTE_UPLOAD_DEST) {
      _state = STATE_LOCAL_BROWSER;
      render();
    } else {
      _showActions();
    }
    return;
  }
  if (_state == STATE_LOCAL_BROWSER) {
    _showActions();
    return;
  }
  Screen.goBack();
}

void FtpClientScreen::_updateLabels() {
  snprintf(_hostLabel, sizeof(_hostLabel), "%s", _host.length() ? _host.c_str() : "-");
  snprintf(_portLabel, sizeof(_portLabel), "%d", _port);
  snprintf(_userLabel, sizeof(_userLabel), "%s",
           _username.length() ? _username.c_str() : "-");
  snprintf(_passwordLabel, sizeof(_passwordLabel), "%s",
           _password.length() ? "********" : "-");
}

void FtpClientScreen::_rebuildConfig() {
  uint8_t sel = min<uint8_t>(_selectedIndex, 4);
  _configItems[0] = {"Host", _hostLabel};
  _configItems[1] = {"Port", _portLabel};
  _configItems[2] = {"Username", _userLabel};
  _configItems[3] = {"Password", _passwordLabel};
  _configItems[4] = {"Connect", nullptr};
  setItems(_configItems, 5, sel);
}

void FtpClientScreen::_showActions() {
  _state = STATE_ACTION;
  setItems(_actionItems, 2, 0);
  render();
}

void FtpClientScreen::_configHost() {
  String initial = _host;
  if (!initial.length() && WiFi.status() == WL_CONNECTED) {
    IPAddress ip = WiFi.localIP();
    initial = String(ip[0]) + "." + String(ip[1]) + "." + String(ip[2]) + ".";
  }
#ifdef DEVICE_HAS_KEYBOARD
  const auto hostInputMode = InputTextAction::INPUT_TEXT;
#else
  const auto hostInputMode = InputTextAction::INPUT_IP_ADDRESS;
#endif

  String value = InputTextAction::popup(
    "Host", initial, hostInputMode);
  if (!InputTextAction::wasCancelled() && value.length()) _host = value;
  _updateLabels();
  _rebuildConfig();
  render();
}

void FtpClientScreen::_configPort() {
  int value = InputNumberAction::popup("Port", 1, 65535, _port);
  if (!InputNumberAction::wasCancelled()) _port = value;
  _updateLabels();
  _rebuildConfig();
  render();
}

void FtpClientScreen::_configUsername() {
  String value = InputTextAction::popup(
    "Username", _username, InputTextAction::INPUT_TEXT);
  if (!InputTextAction::wasCancelled()) _username = value;
  _updateLabels();
  _rebuildConfig();
  render();
}

void FtpClientScreen::_configPassword() {
  String value = InputTextAction::popup(
    "Password", "", InputTextAction::INPUT_TEXT);
  if (!InputTextAction::wasCancelled()) _password = value;
  _selectedIndex = 3;
  _updateLabels();
  _rebuildConfig();
  render();
}

void FtpClientScreen::_connect() {
  if (!_host.length()) {
    ShowStatusAction::show("Host required", 1200); render(); return;
  }
  if (!_username.length()) {
    ShowStatusAction::show("Username required", 1200); render(); return;
  }
  if (WiFi.status() != WL_CONNECTED) {
    ShowStatusAction::show("WiFi not connected", 1500); render(); return;
  }

  if (!_mutex) _mutex = xSemaphoreCreateMutex();
  if (!_mutex || !_startWorker()) {
    ShowStatusAction::show("FTP task failed", 1500);
    render();
    return;
  }

  _state = STATE_CONNECTING;
  render();
}

void FtpClientScreen::_closeConnection(bool returnToConfig) {
  _stopRequested = true;
  _cancelTransfer = true;

  uint32_t start = millis();
  while (_ftpTask && millis() - start < 800) delay(5);

  if (returnToConfig) {
    _state = STATE_CONFIG;
    _updateLabels();
    _rebuildConfig();
    render();
  }
}

void FtpClientScreen::_beginDownload() {
  _remoteMode = REMOTE_DOWNLOAD;
  _requestList(".");
}

void FtpClientScreen::_beginUpload() {
  if (!Uni.Storage || !Uni.Storage->isAvailable()) {
    ShowStatusAction::show("Storage not available", 1500);
    render();
    return;
  }
  _uploadLocalPath = "";
  _uploadName = "";
  _uploadIsDir = false;
  _localBrowser.root = "/";
  _loadLocalUploadDir("/");
}

void FtpClientScreen::_loadLocalUploadDir(const String& path) {
  _localBrowsePath = path;
  uint8_t n = _localBrowser.load(this, path, {}, "SD");
  _state = STATE_LOCAL_BROWSER;
  setItems(_localBrowser.items(), n);
  render();
}

void FtpClientScreen::_selectLocalUpload(uint8_t index) {
  if (index >= _localBrowser.count()) return;
  const auto& e = _localBrowser.entry(index);
  if (e.isDir) {
    _loadLocalUploadDir(e.path);
    return;
  }
  _chooseUploadSource(e.path, e.name, false);
}

void FtpClientScreen::_holdLocalUpload(uint8_t index) {
  if (index >= _localBrowser.count()) return;
  const auto& e = _localBrowser.entry(index);
  if (!e.isDir || e.name == "..") return;
  _chooseUploadSource(e.path, e.name, true);
}

void FtpClientScreen::_chooseUploadSource(const String& path,
                                          const String& name,
                                          bool isDir) {
  _uploadLocalPath = path;
  _uploadName = name;
  _uploadIsDir = isDir;
  _chooseUploadDestinationMode();
}

void FtpClientScreen::_chooseUploadDestinationMode() {
  static const InputSelectAction::Option opts[] = {
    {"Downloads", "downloads"},
    {"Browse", "browse"},
  };

  const char* result = InputSelectAction::popup(
    "Upload Destination", opts, 2, "downloads");

  if (!result) {
    _state = STATE_LOCAL_BROWSER;
    render();
    return;
  }

  if (strcmp(result, "downloads") == 0) {
    _chooseUploadDestination("Downloads");
    return;
  }

  _remoteMode = REMOTE_UPLOAD_DEST;
  _requestList(".");
}

void FtpClientScreen::_requestList(const String& path) {
  if (_workerState != WORKER_READY || !_mutex) return;

  if (xSemaphoreTake(_mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
    _commandPath = path;
    _commandError = "";
    xSemaphoreGive(_mutex);
  }

  _remotePath = path;
  _commandDone = false;
  _commandOk = false;
  _command = CMD_LIST;
  _state = STATE_REMOTE_LOADING;
  render();
}

void FtpClientScreen::_copyRemoteListing() {
  SharedRemoteEntry temp[MAX_REMOTE_ENTRIES];
  uint8_t count = 0;

  if (_mutex && xSemaphoreTake(_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
    count = _sharedEntryCount;
    for (uint8_t i = 0; i < count; ++i) temp[i] = _sharedEntries[i];
    xSemaphoreGive(_mutex);
  }

  uint8_t out = 0;
  if (_remoteMode == REMOTE_UPLOAD_DEST) {
    _remoteNames[out] = ".";
    _remoteSubs[out] = "UPLOAD HERE";
    _remotePaths[out] = _remotePath;
    _remoteIsDir[out] = true;
    _remoteSizes[out] = 0;
    _remoteItems[out] = {_remoteNames[out].c_str(), _remoteSubs[out].c_str()};
    out++;
  }

  if (_remotePath != ".") {
    _remoteNames[out] = "..";
    _remoteSubs[out] = "DIR";
    _remotePaths[out] = _parentRemotePath(_remotePath);
    _remoteIsDir[out] = true;
    _remoteSizes[out] = 0;
    _remoteItems[out] = {_remoteNames[out].c_str(), _remoteSubs[out].c_str()};
    out++;
  }

  for (uint8_t i = 0; i < count && out < MAX_REMOTE_ENTRIES + 2; ++i, ++out) {
    _remoteNames[out] = temp[i].name;
    _remotePaths[out] = temp[i].path;
    _remoteIsDir[out] = temp[i].isDir;
    _remoteSizes[out] = temp[i].size;
    if (temp[i].isDir) {
      _remoteSubs[out] = "DIR";
    } else if (temp[i].size >= 1024 * 1024) {
      _remoteSubs[out] = String((double)temp[i].size / (1024.0 * 1024.0), 1) + " MB";
    } else if (temp[i].size >= 1024) {
      _remoteSubs[out] = String((double)temp[i].size / 1024.0, 1) + " KB";
    } else {
      _remoteSubs[out] = String((unsigned long)temp[i].size) + " B";
    }
    _remoteItems[out] = {_remoteNames[out].c_str(), _remoteSubs[out].c_str()};
  }

  _remoteCount = out;
  setItems(_remoteItems, _remoteCount, 0);
}

void FtpClientScreen::_selectRemote(uint8_t index) {
  if (index >= _remoteCount) return;

  if (_remoteMode == REMOTE_UPLOAD_DEST) {
    if (_remoteNames[index] == ".") {
      _chooseUploadDestination(_remotePaths[index]);
    } else if (_remoteIsDir[index]) {
      _requestList(_remotePaths[index]);
    }
    return;
  }

  if (_remoteIsDir[index]) {
    _requestList(_remotePaths[index]);
    return;
  }

  if (!_confirmTransfer("Download file?")) {
    render();
    return;
  }
  _requestDownload(_remotePaths[index], false);
}

void FtpClientScreen::_holdRemote(uint8_t index) {
  if (index >= _remoteCount || !_remoteIsDir[index]) return;

  if (_remoteMode == REMOTE_UPLOAD_DEST) {
    if (_remoteNames[index] == "..") return;
    _chooseUploadDestination(_remotePaths[index]);
    return;
  }

  if (_remoteNames[index] == "..") return;
  if (!_confirmTransfer("Download directory?")) {
    render();
    return;
  }
  _requestDownload(_remotePaths[index], true);
}

void FtpClientScreen::_chooseUploadDestination(const String& remoteDir) {
  if (!_confirmTransfer(_uploadIsDir ? "Upload directory?" : "Upload file?")) {
    _state = STATE_LOCAL_BROWSER;
    render();
    return;
  }
  _requestUpload(remoteDir);
}

bool FtpClientScreen::_confirmTransfer(const char* title) {
  InputSelectAction::Option opts[] = {
    {"Yes", "yes"},
    {"No", "no"},
  };
  Uni.Lcd.setTextFont(1);
  const char* result = InputSelectAction::popup(title, opts, 2, "yes");
  return result && strcmp(result, "yes") == 0;
}

void FtpClientScreen::_requestDownload(const String& remotePath, bool directory) {
  if (!Uni.Storage || !Uni.Storage->isAvailable()) {
    ShowStatusAction::show("Storage not available", 1500);
    render();
    return;
  }
  Uni.Storage->makeDir("/unigeek");
  Uni.Storage->makeDir("/unigeek/wifi");
  Uni.Storage->makeDir("/unigeek/wifi/remote");
  Uni.Storage->makeDir("/unigeek/wifi/remote/ftp");
  Uni.Storage->makeDir(DOWNLOAD_DIR);

  if (_mutex && xSemaphoreTake(_mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
    _commandPath = remotePath;
    _commandError = "";
    _progressDone = 0;
    _progressTotal = 0;
    _progressFilesDone = 0;
    _progressFilesTotal = 0;
    _progressName = _baseName(remotePath);
    xSemaphoreGive(_mutex);
  }

  _transferIsUpload = false;
  _cancelTransfer = false;
  _commandDone = false;
  _commandOk = false;
  _command = directory ? CMD_DOWNLOAD_DIR : CMD_DOWNLOAD_FILE;
  _state = STATE_TRANSFER;

  ProgressView::init();
  ProgressView::progress(
    directory ? "Preparing directory..." : _progressName.c_str(), 0);
}

void FtpClientScreen::_requestUpload(const String& remoteDir) {
  if (!_uploadLocalPath.length()) return;

  if (_mutex && xSemaphoreTake(_mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
    _commandPath = _uploadLocalPath;
    _commandPath2 = remoteDir;
    _commandError = "";
    _progressDone = 0;
    _progressTotal = 0;
    _progressFilesDone = 0;
    _progressFilesTotal = 0;
    _progressName = _uploadName;
    xSemaphoreGive(_mutex);
  }

  _transferIsUpload = true;
  _cancelTransfer = false;
  _commandDone = false;
  _commandOk = false;
  _command = _uploadIsDir ? CMD_UPLOAD_DIR : CMD_UPLOAD_FILE;
  _state = STATE_TRANSFER;

  ProgressView::init();
  ProgressView::progress(
    _uploadIsDir ? "Preparing directory..." : _uploadName.c_str(), 0);
}

void FtpClientScreen::_updateTransferProgress() {
  uint64_t done = 0, total = 0;
  uint32_t filesDone = 0, filesTotal = 0;
  String name;

  if (_mutex && xSemaphoreTake(_mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
    done = _progressDone;
    total = _progressTotal;
    filesDone = _progressFilesDone;
    filesTotal = _progressFilesTotal;
    name = _progressName;
    xSemaphoreGive(_mutex);
  }

  uint8_t pct = total ? (uint8_t)min<uint64_t>(100, (done * 100) / total) : 0;
  String msg;
  if (filesTotal > 1)
    msg = name + "\n" + String(filesDone) + "/" + String(filesTotal) + " files";
  else
    msg = name.length() ? name : (_transferIsUpload ? "Uploading..." : "Downloading...");

  ProgressView::progress(msg.c_str(), pct);
}

String FtpClientScreen::_parentRemotePath(const String& path) const {
  if (path == "." || path.length() == 0) return ".";
  int slash = path.lastIndexOf('/');
  if (slash < 0) return ".";
  if (slash == 0) return "/";
  String parent = path.substring(0, slash);
  return parent.length() ? parent : ".";
}

String FtpClientScreen::_baseName(const String& path) const {
  String clean = path;
  while (clean.length() > 1 && clean.endsWith("/"))
    clean.remove(clean.length() - 1);
  int slash = clean.lastIndexOf('/');
  return slash >= 0 ? clean.substring(slash + 1) : clean;
}

String FtpClientScreen::_joinRemote(const String& dir, const String& name) const {
  if (dir == "/" ) return "/" + name;
  if (dir == "." || dir.length() == 0) return name;
  return dir + "/" + name;
}

bool FtpClientScreen::_startWorker() {
  if (_ftpTask) return false;

  _stopRequested = false;
  _cancelTransfer = false;
  _workerState = WORKER_CONNECTING;
  _command = CMD_NONE;
  _commandDone = false;

  if (_mutex && xSemaphoreTake(_mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
    _commandError = "";
    xSemaphoreGive(_mutex);
  }

  BaseType_t ok;
#if defined(SOC_CPU_CORES_NUM) && SOC_CPU_CORES_NUM > 1
  ok = xTaskCreatePinnedToCore(
    _workerEntry, "UG FTP", FTP_TASK_STACK_SIZE, this, 1, &_ftpTask, 1);
#else
  ok = xTaskCreate(
    _workerEntry, "UG FTP", FTP_TASK_STACK_SIZE, this, 1, &_ftpTask);
#endif

  if (ok != pdPASS) {
    _ftpTask = nullptr;
    _workerState = WORKER_IDLE;
    return false;
  }
  return true;
}

void FtpClientScreen::_workerEntry(void* arg) {
  static_cast<FtpClientScreen*>(arg)->_worker();
}

void FtpClientScreen::_setWorkerError(const char* message) {
  if (_mutex && xSemaphoreTake(_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
    _commandError = message ? message : "FTP failed";
    xSemaphoreGive(_mutex);
  }
  _workerState = WORKER_FAILED;
}

bool FtpClientScreen::_ftpReadReply(void* controlOpaque, int& code,
                                    String& text, uint32_t timeoutMs) {
  WiFiClient& control = *reinterpret_cast<WiFiClient*>(controlOpaque);
  code = 0;
  text = "";
  String line;
  int multiCode = 0;
  uint32_t start = millis();

  while (!_stopRequested && millis() - start < timeoutMs) {
    while (control.available()) {
      char c = (char)control.read();
      if (c == '\r') continue;
      if (c != '\n') {
        if (line.length() < 512) line += c;
        continue;
      }

      if (line.length() >= 3 &&
          isDigit(line[0]) && isDigit(line[1]) && isDigit(line[2])) {
        int thisCode = (line[0]-'0')*100 + (line[1]-'0')*10 + (line[2]-'0');
        if (text.length()) text += "\n";
        text += line;

        if (multiCode == 0 && line.length() > 3 && line[3] == '-') {
          multiCode = thisCode;
        } else if (multiCode != 0) {
          if (thisCode == multiCode && line.length() > 3 && line[3] == ' ') {
            code = thisCode;
            return true;
          }
        } else {
          code = thisCode;
          return true;
        }
      } else if (line.length()) {
        if (text.length()) text += "\n";
        text += line;
      }
      line = "";
    }

    if (!control.connected() && !control.available()) break;
    delay(5);
  }
  return false;
}

bool FtpClientScreen::_ftpCommand(void* controlOpaque, const String& command,
                                  int& code, String& text) {
  WiFiClient& control = *reinterpret_cast<WiFiClient*>(controlOpaque);
  control.print(command);
  control.print("\r\n");
  return _ftpReadReply(controlOpaque, code, text);
}

bool FtpClientScreen::_ftpOpenPassive(void* controlOpaque, void* dataOpaque) {
  WiFiClient& data = *reinterpret_cast<WiFiClient*>(dataOpaque);
  int code = 0;
  String reply;
  if (!_ftpCommand(controlOpaque, "PASV", code, reply) || code != 227) return false;

  int l = reply.indexOf('(');
  int r = reply.indexOf(')', l + 1);
  if (l < 0 || r < 0) return false;

  String tuple = reply.substring(l + 1, r);
  int nums[6] = {};
  int n = 0;
  int start = 0;
  while (n < 6) {
    int comma = tuple.indexOf(',', start);
    String part = comma >= 0 ? tuple.substring(start, comma) : tuple.substring(start);
    part.trim();
    nums[n++] = part.toInt();
    if (comma < 0) break;
    start = comma + 1;
  }
  if (n != 6) return false;

  IPAddress ip(nums[0], nums[1], nums[2], nums[3]);
  uint16_t port = (uint16_t)(nums[4] * 256 + nums[5]);
  return data.connect(ip, port);
}

bool FtpClientScreen::_parseMlsdLine(const String& raw,
                                     const String& parent,
                                     SharedRemoteEntry& out) {
  String line = raw;
  line.trim();
  int space = line.indexOf(' ');
  if (space <= 0) return false;

  String facts = line.substring(0, space);
  String name = line.substring(space + 1);
  name.trim();
  if (!name.length() || name == "." || name == "..") return false;

  String type;
  uint64_t size = 0;
  int pos = 0;
  while (pos < (int)facts.length()) {
    int semi = facts.indexOf(';', pos);
    if (semi < 0) semi = facts.length();
    String fact = facts.substring(pos, semi);
    int eq = fact.indexOf('=');
    if (eq > 0) {
      String key = fact.substring(0, eq);
      String value = fact.substring(eq + 1);
      key.toLowerCase();
      if (key == "type") type = value;
      else if (key == "size") size = strtoull(value.c_str(), nullptr, 10);
    }
    pos = semi + 1;
  }

  type.toLowerCase();
  if (type == "cdir" || type == "pdir") return false;

  out.name = name;
  out.path = _joinRemote(parent, name);
  out.isDir = type == "dir";
  out.size = size;
  return out.isDir || type == "file" || type.length() == 0;
}

bool FtpClientScreen::_parseListLine(const String& raw,
                                     const String& parent,
                                     SharedRemoteEntry& out) {
  String line = raw;
  line.trim();
  if (line.length() < 10) return false;

  // Unix-style LIST fallback:
  // drwxr-xr-x 1 user group 4096 Jan 01 12:00 name with spaces
  char first = line[0];
  if (first != 'd' && first != '-' && first != 'l') return false;

  int starts[9] = {};
  int fields = 0;
  bool inField = false;
  for (int i = 0; i < (int)line.length() && fields < 9; ++i) {
    if (line[i] != ' ' && !inField) {
      starts[fields++] = i;
      inField = true;
    } else if (line[i] == ' ') {
      inField = false;
    }
  }
  if (fields < 9) return false;

  int sizeStart = starts[4];
  int sizeEnd = line.indexOf(' ', sizeStart);
  String sizeText = sizeEnd > sizeStart ? line.substring(sizeStart, sizeEnd) : "0";

  String name = line.substring(starts[8]);
  if (first == 'l') {
    int arrow = name.indexOf(" -> ");
    if (arrow > 0) name = name.substring(0, arrow);
  }
  name.trim();
  if (!name.length() || name == "." || name == "..") return false;

  out.name = name;
  out.path = _joinRemote(parent, name);
  out.isDir = first == 'd';
  out.size = strtoull(sizeText.c_str(), nullptr, 10);
  return true;
}

static bool _ftpReadDataPayload(WiFiClient& data,
                                String& payload,
                                volatile bool& stopRequested,
                                size_t maxBytes,
                                uint32_t timeoutMs) {
  payload = "";
  payload.reserve(min<size_t>(4096, maxBytes));

  uint8_t buf[512];
  uint32_t lastData = millis();

  while (!stopRequested && (data.connected() || data.available())) {
    int avail = data.available();
    if (avail > 0) {
      int n = data.read(buf, min<int>(avail, sizeof(buf)));
      if (n < 0) return false;
      if (n == 0) continue;

      if (payload.length() + (size_t)n > maxBytes) return false;
      payload.concat(reinterpret_cast<const char*>(buf), (unsigned int)n);
      lastData = millis();
      continue;
    }

    if (millis() - lastData > timeoutMs) return false;
    delay(2);
  }

  return !stopRequested;
}

bool FtpClientScreen::_ftpList(void* controlOpaque, const String& path,
                               SharedRemoteEntry* out, uint8_t& count) {
  WiFiClient data;
  int code = 0;
  String reply;

  if (!_ftpOpenPassive(controlOpaque, &data)) return false;

  String command = "MLSD";
  if (path.length() && path != ".") command += " " + path;

  WiFiClient& control = *reinterpret_cast<WiFiClient*>(controlOpaque);
  control.print(command + "\r\n");
  if (!_ftpReadReply(controlOpaque, code, reply) || (code != 125 && code != 150)) {
    data.stop();

    // Fallback for servers without MLSD.
    if (!_ftpOpenPassive(controlOpaque, &data)) return false;
    // Request hidden entries as well on servers using traditional LIST.
    command = "LIST -a";
    if (path.length() && path != ".") command += " " + path;
    control.print(command + "\r\n");
    if (!_ftpReadReply(controlOpaque, code, reply) || (code != 125 && code != 150)) {
      data.stop();
      return false;
    }

    String payload;
    bool dataOk = _ftpReadDataPayload(
      data, payload, _stopRequested, MAX_LIST_PAYLOAD_BYTES, DATA_STALL_TIMEOUT_MS);
    data.stop();
    if (!dataOk) return false;

    count = 0;
    int pos = 0;
    while (pos < (int)payload.length() && count < MAX_REMOTE_ENTRIES) {
      int nl = payload.indexOf('\n', pos);
      if (nl < 0) nl = payload.length();
      String line = payload.substring(pos, nl);
      line.replace("\r", "");
      SharedRemoteEntry e;
      if (_parseListLine(line, path, e)) out[count++] = e;
      pos = nl + 1;
    }

    if (!_ftpReadReply(controlOpaque, code, reply) ||
        (code != 226 && code != 250))
      return false;
    return true;
  }

  String payload;
  bool dataOk = _ftpReadDataPayload(
    data, payload, _stopRequested, MAX_LIST_PAYLOAD_BYTES, DATA_STALL_TIMEOUT_MS);
  data.stop();
  if (!dataOk) return false;

  count = 0;
  int pos = 0;
  while (pos < (int)payload.length() && count < MAX_REMOTE_ENTRIES) {
    int nl = payload.indexOf('\n', pos);
    if (nl < 0) nl = payload.length();
    String line = payload.substring(pos, nl);
    line.replace("\r", "");
    SharedRemoteEntry e;
    if (_parseMlsdLine(line, path, e)) out[count++] = e;
    pos = nl + 1;
  }

  if (!_ftpReadReply(controlOpaque, code, reply) ||
      (code != 226 && code != 250))
    return false;

  // dirs first, alphabetical
  for (uint8_t i = 1; i < count; ++i) {
    SharedRemoteEntry key = out[i];
    int j = i - 1;
    while (j >= 0) {
      bool move = false;
      if (out[j].isDir != key.isDir)
        move = !out[j].isDir && key.isDir;
      else
        move = strcasecmp(out[j].name.c_str(), key.name.c_str()) > 0;
      if (!move) break;
      out[j + 1] = out[j];
      --j;
    }
    out[j + 1] = key;
  }
  return true;
}

bool FtpClientScreen::_workerList(void* controlOpaque, const String& path) {
  SharedRemoteEntry temp[MAX_REMOTE_ENTRIES];
  uint8_t count = 0;
  if (!_ftpList(controlOpaque, path, temp, count)) {
    if (_mutex && xSemaphoreTake(_mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
      _commandError = "FTP list failed";
      xSemaphoreGive(_mutex);
    }
    return false;
  }

  if (_mutex && xSemaphoreTake(_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
    _sharedEntryCount = count;
    for (uint8_t i = 0; i < count; ++i) _sharedEntries[i] = temp[i];
    xSemaphoreGive(_mutex);
  }
  return true;
}

bool FtpClientScreen::_workerDownloadFile(void* controlOpaque,
                                          const String& remotePath,
                                          const String& localPath,
                                          uint64_t knownSize) {
  WiFiClient& control = *reinterpret_cast<WiFiClient*>(controlOpaque);
  WiFiClient data;
  int code = 0;
  String reply;

  uint64_t size = knownSize;
  if (!size && _ftpCommand(controlOpaque, "SIZE " + remotePath, code, reply) &&
      code == 213) {
    int nl = reply.lastIndexOf('\n');
    String last = nl >= 0 ? reply.substring(nl + 1) : reply;
    if (last.length() > 4)
      size = strtoull(last.substring(4).c_str(), nullptr, 10);
  }

  if (!_ftpOpenPassive(controlOpaque, &data)) return false;
  control.print("RETR " + remotePath + "\r\n");
  if (!_ftpReadReply(controlOpaque, code, reply) ||
      (code != 125 && code != 150)) {
    data.stop();
    return false;
  }

  fs::File local = Uni.Storage->open(localPath.c_str(), "w");
  if (!local) {
    data.stop();
    return false;
  }

  if (_mutex && xSemaphoreTake(_mutex, pdMS_TO_TICKS(20)) == pdTRUE) {
    if (_progressTotal == 0) _progressTotal = size;
    _progressName = _baseName(remotePath);
    xSemaphoreGive(_mutex);
  }

  uint8_t buf[4096];
  bool ok = true;
  uint64_t received = 0;
  uint32_t lastData = millis();

  while (!_stopRequested && !_cancelTransfer &&
         (data.connected() || data.available())) {
    int avail = data.available();
    if (avail > 0) {
      int n = data.read(buf, min<int>(avail, sizeof(buf)));
      if (n < 0) {
        ok = false;
        break;
      }

      if (n > 0) {
        if (local.write(buf, n) != (size_t)n) {
          ok = false;
          break;
        }

        received += (uint64_t)n;
        lastData = millis();

        if (_mutex && xSemaphoreTake(_mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
          _progressDone += n;
          xSemaphoreGive(_mutex);
        }
      }
    } else {
      if (millis() - lastData > DATA_STALL_TIMEOUT_MS) {
        ok = false;
        break;
      }
      delay(2);
    }
  }

  local.close();
  data.stop();

  bool completed = _ftpReadReply(controlOpaque, code, reply) &&
                   (code == 226 || code == 250);

  if (_cancelTransfer || _stopRequested) ok = false;
  if (!completed) ok = false;
  if (size && received != size) ok = false;

  if (!ok) {
    Uni.Storage->deleteFile(localPath.c_str());
    return false;
  }

  if (_mutex && xSemaphoreTake(_mutex, pdMS_TO_TICKS(20)) == pdTRUE) {
    _progressFilesDone++;
    xSemaphoreGive(_mutex);
  }
  return true;
}

bool FtpClientScreen::_workerCalcRemoteDir(void* controlOpaque,
                                           const String& remotePath,
                                           uint64_t& bytes,
                                           uint32_t& files,
                                           int depth) {
  if (depth > 12 || _cancelTransfer || _stopRequested) return false;
  SharedRemoteEntry* entries =
      new (std::nothrow) SharedRemoteEntry[MAX_REMOTE_ENTRIES];
  if (!entries) return false;

  uint8_t count = 0;
  if (!_ftpList(controlOpaque, remotePath, entries, count)) {
    delete[] entries;
    return false;
  }

  for (uint8_t i = 0; i < count; ++i) {
    if (_cancelTransfer || _stopRequested) {
      delete[] entries;
      return false;
    }

    if (entries[i].isDir) {
      String child = entries[i].path;
      if (!_workerCalcRemoteDir(controlOpaque, child,
                                bytes, files, depth + 1)) {
        delete[] entries;
        return false;
      }
    } else {
      bytes += entries[i].size;
      files++;
    }
  }

  delete[] entries;
  return !_cancelTransfer && !_stopRequested;
}

bool FtpClientScreen::_workerDownloadTree(void* controlOpaque,
                                          const String& remotePath,
                                          const String& localPath,
                                          int depth) {
  if (depth > 12 || _cancelTransfer || _stopRequested) return false;
  Uni.Storage->makeDir(localPath.c_str());

  SharedRemoteEntry* entries =
      new (std::nothrow) SharedRemoteEntry[MAX_REMOTE_ENTRIES];
  if (!entries) return false;

  uint8_t count = 0;
  if (!_ftpList(controlOpaque, remotePath, entries, count)) {
    delete[] entries;
    return false;
  }

  for (uint8_t i = 0; i < count; ++i) {
    if (_cancelTransfer || _stopRequested) {
      delete[] entries;
      return false;
    }

    String localChild = localPath + "/" + entries[i].name;
    String remoteChild = entries[i].path;
    bool isDir = entries[i].isDir;
    uint64_t childSize = entries[i].size;

    if (isDir) {
      if (!_workerDownloadTree(controlOpaque, remoteChild,
                               localChild, depth + 1)) {
        delete[] entries;
        return false;
      }
    } else {
      if (!_workerDownloadFile(controlOpaque, remoteChild,
                               localChild, childSize)) {
        delete[] entries;
        return false;
      }
    }
  }

  delete[] entries;
  return !_cancelTransfer && !_stopRequested;
}

bool FtpClientScreen::_workerDownloadDir(void* controlOpaque,
                                         const String& remotePath,
                                         const String& localPath) {
  uint64_t bytes = 0;
  uint32_t files = 0;
  if (!_workerCalcRemoteDir(controlOpaque, remotePath, bytes, files, 0)) return false;

  if (_mutex && xSemaphoreTake(_mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
    _progressTotal = bytes;
    _progressFilesTotal = files;
    _progressDone = 0;
    _progressFilesDone = 0;
    _progressName = _baseName(remotePath);
    xSemaphoreGive(_mutex);
  }

  return _workerDownloadTree(controlOpaque, remotePath, localPath, 0);
}

bool FtpClientScreen::_workerEnsureRemoteDir(void* controlOpaque,
                                              const String& remotePath) {
  int code = 0;
  String reply;
  if (_ftpCommand(controlOpaque, "MKD " + remotePath, code, reply) &&
      (code == 257 || code == 250))
    return true;

  // If MKD failed because the directory already exists, verify it with CWD.
  // Preserve and restore the control connection's working directory because
  // all relative paths in the UI are defined from the login directory.
  String original;
  if (_ftpCommand(controlOpaque, "PWD", code, reply) && code == 257) {
    int q1 = reply.indexOf('"');
    int q2 = q1 >= 0 ? reply.indexOf('"', q1 + 1) : -1;
    if (q1 >= 0 && q2 > q1) original = reply.substring(q1 + 1, q2);
  }

  if (!_ftpCommand(controlOpaque, "CWD " + remotePath, code, reply) || code != 250)
    return false;

  bool restored = true;
  if (original.length()) {
    restored = _ftpCommand(controlOpaque, "CWD " + original, code, reply) &&
               code == 250;
  }
  return restored;
}

bool FtpClientScreen::_workerUploadFile(void* controlOpaque,
                                        const String& localPath,
                                        const String& remotePath,
                                        uint64_t knownSize) {
  WiFiClient& control = *reinterpret_cast<WiFiClient*>(controlOpaque);
  WiFiClient data;
  int code = 0;
  String reply;

  fs::File local = Uni.Storage->open(localPath.c_str(), "r");
  if (!local) return false;

  if (!_ftpOpenPassive(controlOpaque, &data)) {
    local.close();
    return false;
  }

  control.print("STOR " + remotePath + "\r\n");
  if (!_ftpReadReply(controlOpaque, code, reply) ||
      (code != 125 && code != 150)) {
    data.stop();
    local.close();
    return false;
  }

  if (_mutex && xSemaphoreTake(_mutex, pdMS_TO_TICKS(20)) == pdTRUE) {
    if (_progressTotal == 0) _progressTotal = knownSize;
    _progressName = _baseName(localPath);
    xSemaphoreGive(_mutex);
  }

  uint8_t buf[4096];
  bool ok = true;
  uint64_t sentTotal = 0;
  uint32_t lastProgress = millis();

  while (!_stopRequested && !_cancelTransfer && local.available()) {
    size_t n = local.read(buf, sizeof(buf));
    if (!n) break;

    size_t sent = 0;
    while (sent < n && !_stopRequested && !_cancelTransfer) {
      size_t w = data.write(buf + sent, n - sent);

      if (w > 0) {
        sent += w;
        sentTotal += w;
        lastProgress = millis();

        if (_mutex && xSemaphoreTake(_mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
          _progressDone += w;
          xSemaphoreGive(_mutex);
        }
        continue;
      }

      if (!data.connected() ||
          millis() - lastProgress > DATA_STALL_TIMEOUT_MS) {
        ok = false;
        break;
      }
      delay(2);
    }

    if (!ok) break;
  }

  data.stop();
  local.close();

  bool completed = _ftpReadReply(controlOpaque, code, reply) &&
                   (code == 226 || code == 250);

  if (_cancelTransfer || _stopRequested) return false;
  if (!ok || !completed) return false;
  if (knownSize && sentTotal != knownSize) return false;

  if (_mutex && xSemaphoreTake(_mutex, pdMS_TO_TICKS(20)) == pdTRUE) {
    _progressFilesDone++;
    xSemaphoreGive(_mutex);
  }
  return true;
}

bool FtpClientScreen::_workerCalcLocalDir(const String& localPath,
                                          uint64_t& bytes,
                                          uint32_t& files,
                                          int depth) {
  if (depth > 12 || _cancelTransfer || _stopRequested) return false;
  fs::File dir = Uni.Storage->open(localPath.c_str(), "r");
  if (!dir || !dir.isDirectory()) {
    if (dir) dir.close();
    return false;
  }

  fs::File child = dir.openNextFile();
  while (child && !_cancelTransfer && !_stopRequested) {
    String name = _baseName(String(child.name()));
    String childPath = localPath;
    if (!childPath.endsWith("/")) childPath += "/";
    childPath += name;

    if (child.isDirectory()) {
      child.close();
      if (!_workerCalcLocalDir(childPath, bytes, files, depth + 1)) {
        dir.close();
        return false;
      }
    } else {
      bytes += child.size();
      files++;
      child.close();
    }
    child = dir.openNextFile();
  }
  if (child) child.close();
  dir.close();
  return !_cancelTransfer && !_stopRequested;
}

bool FtpClientScreen::_workerUploadTree(void* controlOpaque,
                                        const String& localPath,
                                        const String& remotePath,
                                        int depth) {
  if (depth > 12 || _cancelTransfer || _stopRequested) return false;
  if (!_workerEnsureRemoteDir(controlOpaque, remotePath)) return false;

  fs::File dir = Uni.Storage->open(localPath.c_str(), "r");
  if (!dir || !dir.isDirectory()) {
    if (dir) dir.close();
    return false;
  }

  fs::File child = dir.openNextFile();
  while (child && !_cancelTransfer && !_stopRequested) {
    String name = _baseName(String(child.name()));

    String localChild = localPath;
    if (!localChild.endsWith("/")) localChild += "/";
    localChild += name;

    String remoteChild = _joinRemote(remotePath, name);

    if (child.isDirectory()) {
      child.close();
      if (!_workerUploadTree(controlOpaque, localChild, remoteChild, depth + 1)) {
        dir.close();
        return false;
      }
    } else {
      uint64_t size = child.size();
      child.close();
      if (!_workerUploadFile(controlOpaque, localChild, remoteChild, size)) {
        dir.close();
        return false;
      }
    }
    child = dir.openNextFile();
  }
  if (child) child.close();
  dir.close();
  return !_cancelTransfer && !_stopRequested;
}

bool FtpClientScreen::_workerUploadDir(void* controlOpaque,
                                       const String& localPath,
                                       const String& remotePath) {
  uint64_t bytes = 0;
  uint32_t files = 0;
  if (!_workerCalcLocalDir(localPath, bytes, files, 0)) return false;

  if (_mutex && xSemaphoreTake(_mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
    _progressTotal = bytes;
    _progressFilesTotal = files;
    _progressDone = 0;
    _progressFilesDone = 0;
    _progressName = _baseName(localPath);
    xSemaphoreGive(_mutex);
  }

  return _workerUploadTree(controlOpaque, localPath, remotePath, 0);
}

void FtpClientScreen::_worker() {
  WiFiClient control;
  String host = _host;
  String user = _username;
  String password = _password;
  int port = _port;

  if (!control.connect(host.c_str(), port)) {
    _setWorkerError("FTP connect failed");
    goto cleanup;
  }

  {
    int code = 0;
    String reply;
    if (!_ftpReadReply(&control, code, reply) || code != 220) {
      _setWorkerError("FTP greeting failed");
      goto cleanup;
    }

    if (!_ftpCommand(&control, "USER " + user, code, reply)) {
      _setWorkerError("FTP USER failed");
      goto cleanup;
    }

    if (code == 331) {
      if (!_ftpCommand(&control, "PASS " + password, code, reply) ||
          (code != 230 && code != 202)) {
        _setWorkerError("FTP authentication failed");
        goto cleanup;
      }
    } else if (code != 230) {
      _setWorkerError("FTP authentication failed");
      goto cleanup;
    }

    // Binary mode is required for arbitrary files.
    if (!_ftpCommand(&control, "TYPE I", code, reply) || code != 200) {
      _setWorkerError("FTP binary mode failed");
      goto cleanup;
    }
  }

  password = "";
  _workerState = WORKER_READY;

  while (!_stopRequested && control.connected()) {
    WorkerCommand cmd = _command;
    if (cmd == CMD_NONE) {
      vTaskDelay(pdMS_TO_TICKS(10));
      continue;
    }

    String path;
    String path2;
    if (_mutex && xSemaphoreTake(_mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
      path = _commandPath;
      path2 = _commandPath2;
      _commandError = "";
      xSemaphoreGive(_mutex);
    }

    _command = CMD_NONE;
    _commandDone = false;
    _commandOk = false;
    _cancelTransfer = false;

    bool ok = false;
    if (cmd == CMD_LIST) {
      ok = _workerList(&control, path);
    } else if (cmd == CMD_DOWNLOAD_FILE) {
      String local = String(DOWNLOAD_DIR) + "/" + _baseName(path);
      ok = _workerDownloadFile(&control, path, local, 0);
    } else if (cmd == CMD_DOWNLOAD_DIR) {
      String local = String(DOWNLOAD_DIR) + "/" + _baseName(path);
      ok = _workerDownloadDir(&control, path, local);
    } else if (cmd == CMD_UPLOAD_FILE) {
      if (_workerEnsureRemoteDir(&control, path2)) {
        String remote = _joinRemote(path2, _baseName(path));
        fs::File f = Uni.Storage->open(path.c_str(), "r");
        uint64_t size = f ? (uint64_t)f.size() : 0;
        if (f) f.close();
        ok = _workerUploadFile(&control, path, remote, size);
      }
    } else if (cmd == CMD_UPLOAD_DIR) {
      if (_workerEnsureRemoteDir(&control, path2)) {
        String remote = _joinRemote(path2, _baseName(path));
        ok = _workerUploadDir(&control, path, remote);
      }
    }

    if (_cancelTransfer && cmd != CMD_LIST) {
      ok = false;
      if (_mutex && xSemaphoreTake(_mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        _commandError = "Cancelled";
        xSemaphoreGive(_mutex);
      }
    } else if (!ok && _mutex &&
               xSemaphoreTake(_mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
      if (!_commandError.length()) _commandError = "FTP operation failed";
      xSemaphoreGive(_mutex);
    }

    _commandOk = ok;
    _commandDone = true;
  }

  if (!_stopRequested && !control.connected())
    _workerState = WORKER_CLOSED;

cleanup:
  if (control.connected()) {
    int code = 0;
    String reply;
    _ftpCommand(&control, "QUIT", code, reply);
  }
  control.stop();
  password = "";
  if (_workerState != WORKER_FAILED) _workerState = WORKER_CLOSED;
  _ftpTask = nullptr;
  vTaskDelete(nullptr);
}

void FtpClientScreen::_renderConnecting() {
  auto& lcd = Uni.Lcd;
  int x = bodyX(), y = bodyY(), w = bodyW(), h = bodyH();
  lcd.fillRect(x, y, w, h, TFT_BLACK);
  lcd.setTextFont(1);
  lcd.setTextSize(1);
  lcd.setTextDatum(MC_DATUM);
  lcd.setTextColor(TFT_LIGHTGREY, TFT_BLACK);

  String msg = (_state == STATE_REMOTE_LOADING)
             ? "Loading remote files..."
             : "Connecting to " + _host + ":" + String(_port) + "...";
  lcd.drawString(msg.c_str(), x + w / 2, y + h / 2);
  lcd.setTextDatum(TL_DATUM);
}

void FtpClientScreen::_renderRemoteBrowser() {
  ListScreen::onRender();
}

void FtpClientScreen::_renderLocalBrowser() {
  ListScreen::onRender();
}
