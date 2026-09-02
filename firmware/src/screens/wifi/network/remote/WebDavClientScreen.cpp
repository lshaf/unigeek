#include "WebDavClientScreen.h"

#include "core/Device.h"
#include "core/INavigation.h"
#include "core/ScreenManager.h"
#include "ui/actions/InputSelectAction.h"
#include "ui/actions/InputTextAction.h"
#include "ui/actions/ShowStatusAction.h"
#include "ui/views/ProgressView.h"

#include <FS.h>
#include <HTTPClient.h>
#include <WiFi.h>
#include <WiFiClient.h>
#include <WiFiClientSecure.h>
#include <new>

static bool _davOk(int code) {
  return code >= 200 && code < 300;
}

static bool _davTagLocalEquals(const String& qname, const char* localName) {
  int colon = qname.lastIndexOf(':');
  String local = colon >= 0 ? qname.substring(colon + 1) : qname;
  return local.equalsIgnoreCase(localName);
}

static bool _davNextElementBlock(const String& xml,
                                 const char* localName,
                                 int& pos,
                                 String& block) {
  block = "";

  while (pos < (int)xml.length()) {
    int lt = xml.indexOf('<', pos);
    if (lt < 0) return false;

    int nameStart = lt + 1;
    if (nameStart >= (int)xml.length()) return false;

    char first = xml[nameStart];
    if (first == '/' || first == '?' || first == '!') {
      pos = nameStart + 1;
      continue;
    }

    int nameEnd = nameStart;
    while (nameEnd < (int)xml.length()) {
      char c = xml[nameEnd];
      if (c == '>' || c == '/' || c == ' ' || c == '\t' ||
          c == '\r' || c == '\n')
        break;
      nameEnd++;
    }

    if (nameEnd <= nameStart) {
      pos = nameStart + 1;
      continue;
    }

    String qname = xml.substring(nameStart, nameEnd);
    if (!_davTagLocalEquals(qname, localName)) {
      pos = nameEnd;
      continue;
    }

    int openEnd = xml.indexOf('>', nameEnd);
    if (openEnd < 0) return false;

    String closeTag = "</" + qname + ">";
    int close = xml.indexOf(closeTag, openEnd + 1);
    if (close < 0) return false;

    int end = close + closeTag.length();
    block = xml.substring(lt, end);
    pos = end;
    return true;
  }

  return false;
}

static bool _davHasElement(const String& xml, const char* localName) {
  int pos = 0;

  while (pos < (int)xml.length()) {
    int lt = xml.indexOf('<', pos);
    if (lt < 0) return false;

    int nameStart = lt + 1;
    if (nameStart >= (int)xml.length()) return false;

    char first = xml[nameStart];
    if (first == '/' || first == '?' || first == '!') {
      pos = nameStart + 1;
      continue;
    }

    int nameEnd = nameStart;
    while (nameEnd < (int)xml.length()) {
      char c = xml[nameEnd];
      if (c == '>' || c == '/' || c == ' ' || c == '\t' ||
          c == '\r' || c == '\n')
        break;
      nameEnd++;
    }

    if (nameEnd > nameStart) {
      String qname = xml.substring(nameStart, nameEnd);
      if (_davTagLocalEquals(qname, localName)) return true;
    }

    pos = nameEnd > nameStart ? nameEnd : nameStart + 1;
  }

  return false;
}

static bool _davReadBodyLimited(HTTPClient& http, String& out, size_t maxBytes) {
  out = "";
  int remaining = http.getSize();

  if (remaining > 0 && (size_t)remaining > maxBytes) return false;

  size_t reserveBytes = remaining > 0
      ? min<size_t>((size_t)remaining + 1, maxBytes)
      : min<size_t>(4096, maxBytes);
  out.reserve(reserveBytes);

  WiFiClient* stream = http.getStreamPtr();
  if (!stream) return false;

  uint8_t buf[512];
  uint32_t lastData = millis();

  while ((http.connected() || stream->available()) &&
         (remaining > 0 || remaining == -1)) {
    int avail = stream->available();
    if (avail > 0) {
      int n = stream->read(buf, min<int>(avail, sizeof(buf)));
      if (n <= 0) break;

      if (out.length() + (size_t)n > maxBytes) return false;
      out.concat(reinterpret_cast<const char*>(buf), (unsigned int)n);

      lastData = millis();
      if (remaining > 0) remaining -= n;
    } else {
      if (remaining == 0) break;
      if (millis() - lastData > 12000) return false;
      delay(2);
    }
  }

  return remaining <= 0;
}

const char* WebDavClientScreen::title() {
  switch (_state) {
    case STATE_REMOTE_BROWSER:
      return _remoteMode == REMOTE_UPLOAD_DEST ? "WebDAV Upload" : "WebDAV Download";
    case STATE_LOCAL_BROWSER:
      return "WebDAV Upload";
    case STATE_TRANSFER:
      return _transferIsUpload ? "WebDAV Upload" : "WebDAV Download";
    default:
      return "WebDAV Client";
  }
}

void WebDavClientScreen::onInit() {
  _updateLabels();
  _rebuildConfig();
}

void WebDavClientScreen::onUpdate() {
  if (_state == STATE_TRANSFER) {
    _updateProgress();
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
      _holdLocal(_selectedIndex);
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

void WebDavClientScreen::onRender() {
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

void WebDavClientScreen::onItemSelected(uint8_t index) {
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
    _selectLocal(index);
    return;
  }

  if (_state != STATE_CONFIG) return;

  switch (index) {
    case 0: _configUrl(); break;
    case 1: _configUsername(); break;
    case 2: _configPassword(); break;
    case 3: _connect(); break;
    default: break;
  }
}

void WebDavClientScreen::onBack() {
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

  if (_state == STATE_ACTION) {
    _state = STATE_CONFIG;
    _updateLabels();
    _rebuildConfig();
    render();
    return;
  }

  Screen.goBack();
}

void WebDavClientScreen::_updateLabels() {
  String shown = _baseUrl.length() ? _baseUrl : "-";
  if (shown.length() >= sizeof(_urlLabel))
    shown = "..." + shown.substring(shown.length() - sizeof(_urlLabel) + 4);
  shown.toCharArray(_urlLabel, sizeof(_urlLabel));

  snprintf(_userLabel, sizeof(_userLabel), "%s",
           _username.length() ? _username.c_str() : "-");
  snprintf(_passwordLabel, sizeof(_passwordLabel), "%s",
           _password.length() ? "********" : "-");
}

void WebDavClientScreen::_rebuildConfig() {
  uint8_t sel = min<uint8_t>(_selectedIndex, 3);
  _configItems[0] = {"Server URL", _urlLabel};
  _configItems[1] = {"Username", _userLabel};
  _configItems[2] = {"Password", _passwordLabel};
  _configItems[3] = {"Connect", nullptr};
  setItems(_configItems, 4, sel);
}

void WebDavClientScreen::_showActions() {
  _state = STATE_ACTION;
  setItems(_actionItems, 2, 0);
  render();
}

void WebDavClientScreen::_configUrl() {
  String initial = _baseUrl.length() ? _baseUrl : "http://";
  String value = InputTextAction::popup("Server URL", initial.c_str());
  if (!InputTextAction::wasCancelled() && value.length())
    _baseUrl = _normalizeBase(value);
  _selectedIndex = 0;
  _updateLabels();
  _rebuildConfig();
  render();
}

void WebDavClientScreen::_configUsername() {
  String value = InputTextAction::popup("Username", _username.c_str());
  if (!InputTextAction::wasCancelled()) _username = value;
  _selectedIndex = 1;
  _updateLabels();
  _rebuildConfig();
  render();
}

void WebDavClientScreen::_configPassword() {
  String value = InputTextAction::popup("Password", "");
  if (!InputTextAction::wasCancelled()) _password = value;
  _selectedIndex = 2;
  _updateLabels();
  _rebuildConfig();
  render();
}

void WebDavClientScreen::_connect() {
  if (!_baseUrl.startsWith("http://") && !_baseUrl.startsWith("https://")) {
    ShowStatusAction::show("Use http:// or https://", 1500);
    render();
    return;
  }
  if (_baseUrl.indexOf('?') >= 0 || _baseUrl.indexOf('#') >= 0) {
    ShowStatusAction::show("URL must not contain ? or #", 1600);
    render();
    return;
  }
  if (WiFi.status() != WL_CONNECTED) {
    ShowStatusAction::show("WiFi not connected", 1500);
    render();
    return;
  }

  uint8_t count = 0;
  if (!_propfind("", nullptr, count)) {
    ShowStatusAction::show(
        _lastError.length() ? _lastError.c_str() : "WebDAV connection failed",
        1700);
    render();
    return;
  }

  _showActions();
}

void WebDavClientScreen::_beginDownload() {
  _remoteMode = REMOTE_DOWNLOAD;
  if (!_loadRemote("")) {
    ShowStatusAction::show(
        _lastError.length() ? _lastError.c_str() : "Remote listing failed",
        1500);
    _showActions();
  }
}

void WebDavClientScreen::_beginUpload() {
  if (!Uni.Storage || !Uni.Storage->isAvailable()) {
    ShowStatusAction::show("Storage not available", 1500);
    render();
    return;
  }

  _uploadLocalPath = "";
  _uploadName = "";
  _uploadIsDir = false;
  _localBrowser.root = "/";
  _loadLocalDir("/");
}

void WebDavClientScreen::_loadLocalDir(const String& path) {
  uint8_t n = _localBrowser.load(this, path, {}, "SD");
  _state = STATE_LOCAL_BROWSER;
  setItems(_localBrowser.items(), n);
  render();
}

void WebDavClientScreen::_selectLocal(uint8_t index) {
  if (index >= _localBrowser.count()) return;
  const auto& e = _localBrowser.entry(index);

  if (e.isDir) {
    _loadLocalDir(e.path);
    return;
  }
  _chooseUploadSource(e.path, e.name, false);
}

void WebDavClientScreen::_holdLocal(uint8_t index) {
  if (index >= _localBrowser.count()) return;
  const auto& e = _localBrowser.entry(index);
  if (!e.isDir || e.name == "..") return;
  _chooseUploadSource(e.path, e.name, true);
}

void WebDavClientScreen::_chooseUploadSource(const String& path,
                                             const String& name,
                                             bool isDir) {
  _uploadLocalPath = path;
  _uploadName = name;
  _uploadIsDir = isDir;
  _chooseUploadDestinationMode();
}

void WebDavClientScreen::_chooseUploadDestinationMode() {
  static constexpr InputSelectAction::Option opts[] = {
    {"Downloads", "downloads"},
    {"Browse", "browse"},
  };

  const char* result =
      InputSelectAction::popup("Upload Destination", opts, 2, "downloads");

  if (!result) {
    _state = STATE_LOCAL_BROWSER;
    render();
    return;
  }

  if (!strcmp(result, "downloads")) {
    _chooseUploadDestination("Downloads/");
    return;
  }

  _remoteMode = REMOTE_UPLOAD_DEST;
  if (!_loadRemote("")) {
    ShowStatusAction::show(
        _lastError.length() ? _lastError.c_str() : "Remote listing failed",
        1500);
    _state = STATE_LOCAL_BROWSER;
    render();
  }
}

bool WebDavClientScreen::_loadRemote(const String& path) {
  BrowseFileView::showLoading();

  RemoteEntry temp[MAX_REMOTE_ENTRIES];
  uint8_t count = 0;
  if (!_propfind(path, temp, count)) return false;

  _remotePath = _normalizeRemote(path, true);
  _entryCount = count;
  for (uint8_t i = 0; i < count; ++i) _entries[i] = temp[i];

  _buildRemoteItems();
  _state = STATE_REMOTE_BROWSER;
  render();
  return true;
}

void WebDavClientScreen::_buildRemoteItems() {
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

  if (_remotePath.length()) {
    _remoteNames[out] = "..";
    _remoteSubs[out] = "DIR";
    _remotePaths[out] = _parentRemote(_remotePath);
    _remoteIsDir[out] = true;
    _remoteSizes[out] = 0;
    _remoteItems[out] = {_remoteNames[out].c_str(), _remoteSubs[out].c_str()};
    out++;
  }

  for (uint8_t i = 0; i < _entryCount && out < MAX_REMOTE_ENTRIES + 2; ++i, ++out) {
    _remoteNames[out] = _entries[i].name;
    _remotePaths[out] = _entries[i].path;
    _remoteIsDir[out] = _entries[i].isDir;
    _remoteSizes[out] = _entries[i].size;

    if (_entries[i].isDir)
      _remoteSubs[out] = "DIR";
    else if (_entries[i].size >= 1024 * 1024)
      _remoteSubs[out] = String((double)_entries[i].size / (1024.0 * 1024.0), 1) + " MB";
    else if (_entries[i].size >= 1024)
      _remoteSubs[out] = String((double)_entries[i].size / 1024.0, 1) + " KB";
    else
      _remoteSubs[out] = String((unsigned long)_entries[i].size) + " B";

    _remoteItems[out] = {_remoteNames[out].c_str(), _remoteSubs[out].c_str()};
  }

  _remoteCount = out;
  setItems(_remoteItems, _remoteCount, 0);
}

void WebDavClientScreen::_selectRemote(uint8_t index) {
  if (index >= _remoteCount) return;

  if (_remoteMode == REMOTE_UPLOAD_DEST) {
    if (_remoteNames[index] == ".") {
      _chooseUploadDestination(_remotePaths[index]);
    } else if (_remoteIsDir[index]) {
      if (!_loadRemote(_remotePaths[index]))
        ShowStatusAction::show(
        _lastError.length() ? _lastError.c_str() : "Remote listing failed",
        1500);
    }
    return;
  }

  if (_remoteIsDir[index]) {
    if (!_loadRemote(_remotePaths[index]))
      ShowStatusAction::show(
        _lastError.length() ? _lastError.c_str() : "Remote listing failed",
        1500);
    return;
  }

  if (!_confirm("Download file?")) {
    render();
    return;
  }

  Uni.Storage->makeDir("/unigeek");
  Uni.Storage->makeDir("/unigeek/wifi");
  Uni.Storage->makeDir("/unigeek/wifi/remote");
  Uni.Storage->makeDir("/unigeek/wifi/remote/webdav");
  Uni.Storage->makeDir(DOWNLOAD_DIR);

  String local = String(DOWNLOAD_DIR) + "/" +
                 _safeLocalName(_baseName(_remotePaths[index]));
  _startProgress(false, _remoteNames[index], _remoteSizes[index], 1);
  bool ok = _getFile(_remotePaths[index], local, _remoteSizes[index]);
  _finishProgress(ok,
      ok ? "" : (_lastError.length() ? _lastError.c_str() : "Download failed"));
}

void WebDavClientScreen::_holdRemote(uint8_t index) {
  if (index >= _remoteCount || !_remoteIsDir[index]) return;

  if (_remoteMode == REMOTE_UPLOAD_DEST) {
    if (_remoteNames[index] == "..") return;
    _chooseUploadDestination(_remotePaths[index]);
    return;
  }

  if (_remoteNames[index] == "..") return;
  if (!_confirm("Download directory?")) {
    render();
    return;
  }

  Uni.Storage->makeDir("/unigeek");
  Uni.Storage->makeDir("/unigeek/wifi");
  Uni.Storage->makeDir("/unigeek/wifi/remote");
  Uni.Storage->makeDir("/unigeek/wifi/remote/webdav");
  Uni.Storage->makeDir(DOWNLOAD_DIR);

  _startProgress(false, _remoteNames[index]);
  uint64_t bytes = 0;
  uint32_t files = 0;
  bool ok = _scanRemote(_remotePaths[index], bytes, files, 0);
  if (ok) {
    _progressTotal = bytes;
    _progressFilesTotal = files;
    String local = String(DOWNLOAD_DIR) + "/" +
                   _safeLocalName(_baseName(_remotePaths[index]));
    ok = _downloadTree(_remotePaths[index], local, 0);
  }
  _finishProgress(ok,
      ok ? "" : (_lastError.length() ? _lastError.c_str()
                                     : "Directory download failed"));
}

void WebDavClientScreen::_chooseUploadDestination(const String& path) {
  if (!_confirm(_uploadIsDir ? "Upload directory?" : "Upload file?")) {
    _state = STATE_LOCAL_BROWSER;
    render();
    return;
  }

  String dir = _normalizeRemote(path, true);
  if (!_ensureCollection(dir)) {
    ShowStatusAction::show(
        _lastError.length() ? _lastError.c_str() : "Remote directory failed",
        1500);
    _state = STATE_LOCAL_BROWSER;
    render();
    return;
  }

  if (_uploadIsDir) {
    _startProgress(true, _uploadName);
    uint64_t bytes = 0;
    uint32_t files = 0;
    bool ok = _scanLocal(_uploadLocalPath, bytes, files, 0);
    if (ok) {
      _progressTotal = bytes;
      _progressFilesTotal = files;
      ok = _uploadTree(_uploadLocalPath, _joinRemote(dir, _uploadName) + "/", 0);
    }
    _finishProgress(ok,
        ok ? "" : (_lastError.length() ? _lastError.c_str()
                                       : "Directory upload failed"));
  } else {
    fs::File f = Uni.Storage->open(_uploadLocalPath.c_str(), "r");
    uint64_t size = f ? (uint64_t)f.size() : 0;
    if (f) f.close();

    _startProgress(true, _uploadName, size, 1);
    bool ok = _putFile(_uploadLocalPath, _joinRemote(dir, _uploadName), size);
    _finishProgress(ok,
        ok ? "" : (_lastError.length() ? _lastError.c_str() : "Upload failed"));
  }
}

bool WebDavClientScreen::_confirm(const char* title) {
  static constexpr InputSelectAction::Option opts[] = {
    {"Yes", "yes"},
    {"No", "no"},
  };
  Uni.Lcd.setTextFont(1);
  const char* result = InputSelectAction::popup(title, opts, 2, "yes");
  return result && !strcmp(result, "yes");
}

bool WebDavClientScreen::_propfind(const String& path,
                                   RemoteEntry* out,
                                   uint8_t& count) {
  count = 0;
  _lastError = "";
  _lastHttpCode = 0;

  const String url = _urlFor(_normalizeRemote(path, true));
  const bool secure = url.startsWith("https://");

  WiFiClient plain;
  WiFiClientSecure tls;
  if (secure) tls.setInsecure();

  WiFiClient& client = secure
      ? static_cast<WiFiClient&>(tls)
      : static_cast<WiFiClient&>(plain);

  HTTPClient http;
  if (!http.begin(client, url)) {
    _setHttpError(0, "HTTP setup");
    return false;
  }

  http.setTimeout(12000);
  if (_username.length())
    http.setAuthorization(_username.c_str(), _password.c_str());

  http.addHeader("Depth", "1");
  http.addHeader("Content-Type", "application/xml; charset=utf-8");
  http.addHeader("Accept", "application/xml, text/xml");

  const String body =
      "<?xml version=\"1.0\" encoding=\"utf-8\"?>"
      "<d:propfind xmlns:d=\"DAV:\"><d:prop>"
      "<d:resourcetype/><d:getcontentlength/><d:displayname/>"
      "</d:prop></d:propfind>";

  int code = http.sendRequest("PROPFIND", body);
  _lastHttpCode = code;

  if (code != 207 && code != 200) {
    http.end();
    _setHttpError(code, "PROPFIND");
    return false;
  }

  String xml;
  if (!_davReadBodyLimited(http, xml, MAX_PROPFIND_BYTES)) {
    http.end();
    _lastError = "WebDAV response too large";
    return false;
  }
  http.end();

  // A null output pointer means the caller only wants to verify that this is
  // a readable WebDAV collection (used by Connect and MKCOL verification).
  if (!out) {
    _lastError = "";
    return true;
  }

  String current = _normalizeRemote(path, true);
  int pos = 0;
  String block;

  while (_davNextElementBlock(xml, "response", pos, block)) {
    String href = _extractTag(block, "href");
    if (!href.length()) continue;

    String relative;
    if (!_hrefToRelative(_xmlDecode(href), relative)) continue;

    String resourceType = _extractTag(block, "resourcetype");
    bool isDir = _davHasElement(resourceType, "collection");
    relative = _normalizeRemote(relative, isDir);

    if (_normalizeRemote(relative, true) == current) continue;

    String actualName = _baseName(relative);
    actualName = _urlDecode(actualName);

    String displayName = _xmlDecode(_extractTag(block, "displayname"));
    displayName = _urlDecode(displayName);

    String name = actualName.length() ? actualName : displayName;
    if (!name.length() || name == "." || name == "..") continue;

    if (count >= MAX_REMOTE_ENTRIES) {
      _lastError = "Remote directory has too many entries";
      return false;
    }

    String sizeText = _extractTag(block, "getcontentlength");
    sizeText.trim();

    out[count].name = name;
    out[count].path = relative;
    out[count].isDir = isDir;
    out[count].size = sizeText.length()
        ? strtoull(sizeText.c_str(), nullptr, 10) : 0;
    count++;
  }

  for (uint8_t i = 1; i < count; ++i) {
    RemoteEntry key = out[i];
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

  _lastError = "";
  return true;
}

bool WebDavClientScreen::_mkcol(const String& path) {
  String url = _urlFor(_normalizeRemote(path, true));
  bool secure = url.startsWith("https://");

  WiFiClient plain;
  WiFiClientSecure tls;
  if (secure) tls.setInsecure();

  WiFiClient& client = secure
      ? static_cast<WiFiClient&>(tls)
      : static_cast<WiFiClient&>(plain);

  HTTPClient http;
  if (!http.begin(client, url)) {
    _setHttpError(0, "HTTP setup");
    return false;
  }

  http.setTimeout(12000);
  if (_username.length())
    http.setAuthorization(_username.c_str(), _password.c_str());

  int code = http.sendRequest("MKCOL");
  http.end();
  _lastHttpCode = code;

  if (code == 201 || code == 204) {
    _lastError = "";
    return true;
  }

  // 405 commonly means the collection already exists, but it can also mean
  // MKCOL is disabled. Verify with PROPFIND instead of blindly accepting it.
  if (code == 405) {
    uint8_t count = 0;
    if (_propfind(path, nullptr, count)) {
      _lastError = "";
      return true;
    }
    return false;
  }

  _setHttpError(code, "MKCOL");
  return false;
}

bool WebDavClientScreen::_ensureCollection(const String& path) {
  String clean = _normalizeRemote(path, true);
  if (!clean.length()) return true;

  String current;
  int pos = 0;
  while (pos < (int)clean.length()) {
    int slash = clean.indexOf('/', pos);
    if (slash < 0) break;
    String part = clean.substring(pos, slash);
    pos = slash + 1;
    if (!part.length()) continue;
    current += part + "/";
    if (!_mkcol(current)) return false;
  }
  return true;
}

bool WebDavClientScreen::_getFile(const String& remotePath,
                                  const String& localPath,
                                  uint64_t size) {
  if (_pollCancel()) return false;

  fs::File out = Uni.Storage->open(localPath.c_str(), "w");
  if (!out) {
    _lastError = "Local file open failed";
    return false;
  }

  String url = _urlFor(remotePath);
  bool secure = url.startsWith("https://");
  WiFiClient plain;
  WiFiClientSecure tls;
  if (secure) tls.setInsecure();

  WiFiClient& client = secure
      ? static_cast<WiFiClient&>(tls)
      : static_cast<WiFiClient&>(plain);

  HTTPClient http;
  if (!http.begin(client, url)) {
    out.close();
    Uni.Storage->deleteFile(localPath.c_str());
    _setHttpError(0, "HTTP setup");
    return false;
  }

  http.setTimeout(12000);
  if (_username.length())
    http.setAuthorization(_username.c_str(), _password.c_str());

  int code = http.GET();
  _lastHttpCode = code;
  if (!_davOk(code)) {
    http.end();
    out.close();
    Uni.Storage->deleteFile(localPath.c_str());
    _setHttpError(code, "GET");
    return false;
  }

  int remaining = http.getSize();
  if (remaining > 0 && _progressTotal == 0)
    _progressTotal = (uint64_t)remaining;

  WiFiClient* stream = http.getStreamPtr();
  if (!stream) {
    http.end();
    out.close();
    Uni.Storage->deleteFile(localPath.c_str());
    _lastError = "HTTP stream unavailable";
    return false;
  }

  uint8_t buf[4096];
  uint64_t gotTotal = 0;
  uint32_t lastData = millis();
  bool ok = true;

  while (!_pollCancel() &&
         (http.connected() || stream->available()) &&
         (remaining > 0 || remaining == -1)) {
    int avail = stream->available();
    if (avail > 0) {
      int n = stream->read(buf, min<int>(avail, sizeof(buf)));
      if (n <= 0) {
        ok = false;
        break;
      }

      if (out.write(buf, n) != (size_t)n) {
        _lastError = "SD write failed";
        ok = false;
        break;
      }

      gotTotal += (uint64_t)n;
      _progressDone += (uint64_t)n;
      lastData = millis();
      if (remaining > 0) remaining -= n;
      _updateProgress();
    } else {
      if (remaining == 0) break;
      if (millis() - lastData > 12000) {
        _lastError = "Download timed out";
        ok = false;
        break;
      }
      delay(2);
    }
  }

  if (remaining > 0) ok = false;

  http.end();
  out.close();

  if (_cancelTransfer) {
    Uni.Storage->deleteFile(localPath.c_str());
    _lastError = "Cancelled";
    return false;
  }

  if (!ok || (size && gotTotal != size)) {
    Uni.Storage->deleteFile(localPath.c_str());
    if (!_lastError.length()) _lastError = "Incomplete download";
    return false;
  }

  _progressFilesDone++;
  _lastError = "";
  return true;
}

bool WebDavClientScreen::_putFile(const String& localPath,
                                  const String& remotePath,
                                  uint64_t size) {
  if (_cancelTransfer) return false;

  fs::File in = Uni.Storage->open(localPath.c_str(), "r");
  if (!in) return false;

  String url = _urlFor(remotePath);
  bool secure = url.startsWith("https://");
  WiFiClient plain;
  WiFiClientSecure tls;
  if (secure) tls.setInsecure();
  WiFiClient& client = secure
      ? static_cast<WiFiClient&>(tls)
      : static_cast<WiFiClient&>(plain);

  HTTPClient http;
  if (!http.begin(client, url)) {
    in.close();
    return false;
  }
  http.setTimeout(15000);
  if (_username.length()) http.setAuthorization(_username.c_str(), _password.c_str());
  http.addHeader("Content-Type", "application/octet-stream");

  int code = http.sendRequest("PUT", &in, size);
  http.end();
  in.close();
  _lastHttpCode = code;

  if (!_davOk(code)) {
    _setHttpError(code, "PUT");
    return false;
  }
  if (_cancelTransfer) {
    _lastError = "Cancelled";
    return false;
  }
  _progressDone += size;
  _progressFilesDone++;
  _updateProgress();
  return true;
}

bool WebDavClientScreen::_scanRemote(const String& path,
                                     uint64_t& bytes,
                                     uint32_t& files,
                                     int depth) {
  if (depth > 12 || _pollCancel()) return false;

  RemoteEntry* temp = new (std::nothrow) RemoteEntry[MAX_REMOTE_ENTRIES];
  if (!temp) {
    _lastError = "Not enough memory";
    return false;
  }

  uint8_t count = 0;
  if (!_propfind(path, temp, count)) {
    delete[] temp;
    return false;
  }

  for (uint8_t i = 0; i < count; ++i) {
    if (_pollCancel()) {
      delete[] temp;
      return false;
    }

    if (temp[i].isDir) {
      String child = temp[i].path;
      if (!_scanRemote(child, bytes, files, depth + 1)) {
        delete[] temp;
        return false;
      }
    } else {
      bytes += temp[i].size;
      files++;
    }
  }

  delete[] temp;
  return !_cancelTransfer;
}

bool WebDavClientScreen::_downloadTree(const String& remote,
                                       const String& local,
                                       int depth) {
  if (depth > 12 || _pollCancel()) return false;
  Uni.Storage->makeDir(local.c_str());

  RemoteEntry* temp = new (std::nothrow) RemoteEntry[MAX_REMOTE_ENTRIES];
  if (!temp) {
    _lastError = "Not enough memory";
    return false;
  }

  uint8_t count = 0;
  if (!_propfind(remote, temp, count)) {
    delete[] temp;
    return false;
  }

  for (uint8_t i = 0; i < count; ++i) {
    if (_pollCancel()) {
      delete[] temp;
      return false;
    }

    String localChild = local + "/" + _safeLocalName(temp[i].name);
    _progressName = temp[i].name;

    if (temp[i].isDir) {
      String childRemote = temp[i].path;
      if (!_downloadTree(childRemote, localChild, depth + 1)) {
        delete[] temp;
        return false;
      }
    } else {
      String childRemote = temp[i].path;
      uint64_t childSize = temp[i].size;
      if (!_getFile(childRemote, localChild, childSize)) {
        delete[] temp;
        return false;
      }
    }
  }

  delete[] temp;
  return !_cancelTransfer;
}

bool WebDavClientScreen::_scanLocal(const String& path,
                                    uint64_t& bytes,
                                    uint32_t& files,
                                    int depth) {
  if (depth > 12 || _pollCancel()) return false;

  fs::File dir = Uni.Storage->open(path.c_str(), "r");
  if (!dir || !dir.isDirectory()) {
    if (dir) dir.close();
    return false;
  }

  fs::File child = dir.openNextFile();
  while (child && !_pollCancel()) {
    String name = _baseName(String(child.name()));
    String childPath = path;
    if (!childPath.endsWith("/")) childPath += "/";
    childPath += name;

    if (child.isDirectory()) {
      child.close();
      if (!_scanLocal(childPath, bytes, files, depth + 1)) {
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
  return !_cancelTransfer;
}

bool WebDavClientScreen::_uploadTree(const String& local,
                                     const String& remote,
                                     int depth) {
  if (depth > 12 || _pollCancel()) return false;
  if (!_ensureCollection(remote)) return false;

  fs::File dir = Uni.Storage->open(local.c_str(), "r");
  if (!dir || !dir.isDirectory()) {
    if (dir) dir.close();
    return false;
  }

  fs::File child = dir.openNextFile();
  while (child && !_pollCancel()) {
    String name = _baseName(String(child.name()));
    String localChild = local;
    if (!localChild.endsWith("/")) localChild += "/";
    localChild += name;
    _progressName = name;

    if (child.isDirectory()) {
      child.close();
      if (!_uploadTree(localChild, _joinRemote(remote, name) + "/", depth + 1)) {
        dir.close();
        return false;
      }
    } else {
      uint64_t size = child.size();
      child.close();
      if (!_putFile(localChild, _joinRemote(remote, name), size)) {
        dir.close();
        return false;
      }
    }
    child = dir.openNextFile();
  }

  if (child) child.close();
  dir.close();
  return !_cancelTransfer;
}

String WebDavClientScreen::_urlFor(const String& relative) const {
  String path = relative;
  while (path.startsWith("/")) path.remove(0, 1);
  return _baseUrl + _urlEncodePath(path);
}

String WebDavClientScreen::_normalizeBase(String value) const {
  value.trim();
  if (!value.endsWith("/")) value += "/";
  return value;
}

String WebDavClientScreen::_normalizeRemote(String value, bool dir) const {
  value.trim();
  while (value.startsWith("/")) value.remove(0, 1);
  while (value.indexOf("//") >= 0) value.replace("//", "/");
  if (value == ".") value = "";
  if (dir && value.length() && !value.endsWith("/")) value += "/";
  return value;
}

String WebDavClientScreen::_parentRemote(const String& value) const {
  String clean = _normalizeRemote(value, true);
  if (!clean.length()) return "";
  clean.remove(clean.length() - 1);
  int slash = clean.lastIndexOf('/');
  return slash < 0 ? "" : clean.substring(0, slash + 1);
}

String WebDavClientScreen::_joinRemote(const String& dir,
                                       const String& name) const {
  return _normalizeRemote(dir, true) + name;
}

String WebDavClientScreen::_baseName(const String& value) const {
  String clean = value;
  while (clean.length() && clean.endsWith("/"))
    clean.remove(clean.length() - 1);
  int slash = clean.lastIndexOf('/');
  return slash >= 0 ? clean.substring(slash + 1) : clean;
}

String WebDavClientScreen::_safeLocalName(String value) const {
  value = _urlDecode(value);
  value.trim();

  for (int i = 0; i < (int)value.length(); ++i) {
    unsigned char c = (unsigned char)value[i];
    if (c < 0x20 || value[i] == '/' || value[i] == '\\')
      value.setCharAt(i, '_');
  }

  if (!value.length() || value == "." || value == "..") value = "file";
  return value;
}

String WebDavClientScreen::_urlEncodePath(const String& value) const {
  static const char HEX_DIGITS[] = "0123456789ABCDEF";
  String out;
  out.reserve(value.length() + 8);

  for (int i = 0; i < (int)value.length(); ++i) {
    uint8_t c = (uint8_t)value[i];
    bool safe =
        (c >= 'a' && c <= 'z') ||
        (c >= 'A' && c <= 'Z') ||
        (c >= '0' && c <= '9') ||
        c == '-' || c == '_' || c == '.' || c == '~' || c == '/';

    if (safe) {
      out += (char)c;
    } else {
      out += '%';
      out += HEX_DIGITS[(c >> 4) & 0x0F];
      out += HEX_DIGITS[c & 0x0F];
    }
  }
  return out;
}

bool WebDavClientScreen::_hrefToRelative(String href, String& relative) const {
  relative = "";
  href.trim();

  int query = href.indexOf('?');
  if (query >= 0) href = href.substring(0, query);
  int fragment = href.indexOf('#');
  if (fragment >= 0) href = href.substring(0, fragment);

  int scheme = href.indexOf("://");
  if (scheme >= 0) {
    int slash = href.indexOf('/', scheme + 3);
    href = slash >= 0 ? href.substring(slash) : "/";
  }

  String base = _baseUrl;
  int baseScheme = base.indexOf("://");
  if (baseScheme >= 0) {
    int slash = base.indexOf('/', baseScheme + 3);
    base = slash >= 0 ? base.substring(slash) : "/";
  }

  href = _urlDecode(href);
  base = _urlDecode(base);

  String baseNoSlash = base;
  while (baseNoSlash.length() > 1 && baseNoSlash.endsWith("/"))
    baseNoSlash.remove(baseNoSlash.length() - 1);

  if (href == baseNoSlash || href == base) {
    relative = "";
    return true;
  }

  if (!href.startsWith(base)) return false;

  relative = href.substring(base.length());
  while (relative.startsWith("/")) relative.remove(0, 1);

  if (relative == ".." || relative.startsWith("../") ||
      relative.indexOf("/../") >= 0)
    return false;

  return true;
}

String WebDavClientScreen::_extractTag(const String& block,
                                       const char* localName) const {
  int pos = 0;

  while (pos < (int)block.length()) {
    int lt = block.indexOf('<', pos);
    if (lt < 0) break;

    int nameStart = lt + 1;
    if (nameStart >= (int)block.length()) break;

    char first = block[nameStart];
    if (first == '/' || first == '?' || first == '!') {
      pos = nameStart + 1;
      continue;
    }

    int nameEnd = nameStart;
    while (nameEnd < (int)block.length()) {
      char c = block[nameEnd];
      if (c == '>' || c == '/' || c == ' ' || c == '\t' ||
          c == '\r' || c == '\n')
        break;
      nameEnd++;
    }

    if (nameEnd <= nameStart) {
      pos = nameStart + 1;
      continue;
    }

    String qname = block.substring(nameStart, nameEnd);
    if (!_davTagLocalEquals(qname, localName)) {
      pos = nameEnd;
      continue;
    }

    int openEnd = block.indexOf('>', nameEnd);
    if (openEnd < 0) break;

    String closeTag = "</" + qname + ">";
    int close = block.indexOf(closeTag, openEnd + 1);
    if (close < 0) break;

    return block.substring(openEnd + 1, close);
  }

  return "";
}

String WebDavClientScreen::_xmlDecode(String value) const {
  value.replace("&quot;", "\"");
  value.replace("&apos;", "'");
  value.replace("&gt;", ">");
  value.replace("&lt;", "<");
  value.replace("&amp;", "&");
  value.trim();
  return value;
}

String WebDavClientScreen::_urlDecode(String value) const {
  String out;
  for (int i = 0; i < (int)value.length(); ++i) {
    if (value[i] == '%' && i + 2 < (int)value.length()) {
      char hex[3] = {value[i + 1], value[i + 2], 0};
      char* end = nullptr;
      long v = strtol(hex, &end, 16);
      if (end && *end == 0) {
        out += (char)v;
        i += 2;
        continue;
      }
    }
    out += value[i];
  }
  return out;
}


bool WebDavClientScreen::_pollCancel() {
  if (_cancelTransfer) return true;

  if (_state == STATE_TRANSFER &&
      Uni.Nav->wasPressed() &&
      Uni.Nav->readDirection() == INavigation::DIR_BACK) {
    _cancelTransfer = true;
  }
  return _cancelTransfer;
}

const char* WebDavClientScreen::_httpStatusText(int code) const {
  switch (code) {
    case 400: return "Bad request";
    case 401: return "Authentication failed";
    case 403: return "Permission denied";
    case 404: return "Remote path not found";
    case 405: return "WebDAV method not allowed";
    case 409: return "Remote parent missing";
    case 412: return "Precondition failed";
    case 423: return "Remote resource locked";
    case 507: return "Remote storage full";
    default:  return nullptr;
  }
}

void WebDavClientScreen::_setHttpError(int code, const char* context) {
  _lastHttpCode = code;
  const char* known = _httpStatusText(code);

  if (known) {
    _lastError = known;
  } else if (code < 0) {
    _lastError = "HTTP transport error";
  } else if (code == 0) {
    _lastError = context && *context ? String(context) + " failed"
                                    : "HTTP setup failed";
  } else {
    _lastError = context && *context ? String(context) + " failed (" +
                                       String(code) + ")"
                                    : "HTTP error " + String(code);
  }
}

void WebDavClientScreen::_startProgress(bool upload,
                                        const String& name,
                                        uint64_t bytes,
                                        uint32_t files) {
  _transferIsUpload = upload;
  _cancelTransfer = false;
  _progressDone = 0;
  _progressTotal = bytes;
  _progressFilesDone = 0;
  _progressFilesTotal = files;
  _progressName = name;
  _state = STATE_TRANSFER;
  ProgressView::init();
  ProgressView::progress(name.c_str(), 0);
}

void WebDavClientScreen::_updateProgress() {
  uint8_t pct = _progressTotal
      ? (uint8_t)min<uint64_t>(100, (_progressDone * 100) / _progressTotal)
      : 0;

  String msg = _progressName;
  if (_progressFilesTotal > 1)
    msg += "\n" + String(_progressFilesDone) + "/" +
           String(_progressFilesTotal) + " files";

  ProgressView::progress(msg.c_str(), pct);
}

void WebDavClientScreen::_finishProgress(bool ok, const char* error) {
  ProgressView::finish();
  _state = _transferIsUpload ? STATE_LOCAL_BROWSER : STATE_REMOTE_BROWSER;
  render();

  if (_cancelTransfer) {
    ShowStatusAction::show(
        _transferIsUpload ? "Upload cancelled" : "Download cancelled", 1300);
  } else if (ok) {
    ShowStatusAction::show(
        _transferIsUpload ? "Upload complete" : "Download complete", 1400);
  } else {
    ShowStatusAction::show(error && *error ? error : "Transfer failed", 1600);
  }
  render();
}

void WebDavClientScreen::_renderRemoteBrowser() {
  ListScreen::onRender();
}

void WebDavClientScreen::_renderLocalBrowser() {
  ListScreen::onRender();
}
