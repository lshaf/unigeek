#pragma once

#include "ui/templates/ListScreen.h"
#include "ui/views/BrowseFileView.h"

class WebDavClientScreen : public ListScreen
{
public:
  WebDavClientScreen() = default;
  explicit WebDavClientScreen(const String& baseUrl)
    : _baseUrl(baseUrl) {}

  const char* title() override;
  bool inhibitPowerOff() override { return _state == STATE_TRANSFER; }

  void onInit() override;
  void onUpdate() override;
  void onRender() override;
  void onItemSelected(uint8_t index) override;
  void onBack() override;

private:
  enum State : uint8_t {
    STATE_CONFIG,
    STATE_ACTION,
    STATE_REMOTE_BROWSER,
    STATE_LOCAL_BROWSER,
    STATE_TRANSFER
  };

  enum RemoteMode : uint8_t {
    REMOTE_DOWNLOAD,
    REMOTE_UPLOAD_DEST
  };

  struct RemoteEntry {
    String name;
    String path;
    bool isDir = false;
    uint64_t size = 0;
  };

  static constexpr uint8_t MAX_REMOTE_ENTRIES = 80;
  static constexpr uint32_t HOLD_MS = 900;
  static constexpr size_t MAX_PROPFIND_BYTES = 65536;
  static constexpr const char* DOWNLOAD_DIR =
      "/unigeek/wifi/remote/webdav/downloads";

  State _state = STATE_CONFIG;
  RemoteMode _remoteMode = REMOTE_DOWNLOAD;

  String _baseUrl;
  String _username;
  String _password;

  char _urlLabel[48] = {};
  char _userLabel[32] = {};
  char _passwordLabel[16] = "-";

  ListItem _configItems[4];
  ListItem _actionItems[2] = {
    {"Download", nullptr},
    {"Upload", nullptr},
  };

  BrowseFileView _localBrowser;
  String _uploadLocalPath;
  String _uploadName;
  bool _uploadIsDir = false;

  RemoteEntry _entries[MAX_REMOTE_ENTRIES];
  String _remoteNames[MAX_REMOTE_ENTRIES + 2];
  String _remoteSubs[MAX_REMOTE_ENTRIES + 2];
  String _remotePaths[MAX_REMOTE_ENTRIES + 2];
  bool _remoteIsDir[MAX_REMOTE_ENTRIES + 2] = {};
  uint64_t _remoteSizes[MAX_REMOTE_ENTRIES + 2] = {};
  ListItem _remoteItems[MAX_REMOTE_ENTRIES + 2];
  uint8_t _entryCount = 0;
  uint8_t _remoteCount = 0;
  String _remotePath;
  bool _holdHandled = false;

  bool _cancelTransfer = false;
  bool _transferIsUpload = false;
  uint64_t _progressDone = 0;
  uint64_t _progressTotal = 0;
  uint32_t _progressFilesDone = 0;
  uint32_t _progressFilesTotal = 0;
  String _progressName;
  int _lastHttpCode = 0;
  String _lastError;

  void _updateLabels();
  void _rebuildConfig();
  void _showActions();

  void _configUrl();
  void _configUsername();
  void _configPassword();
  void _connect();

  void _beginDownload();
  void _beginUpload();

  void _loadLocalDir(const String& path);
  void _selectLocal(uint8_t index);
  void _holdLocal(uint8_t index);
  void _chooseUploadSource(const String& path, const String& name, bool isDir);
  void _chooseUploadDestinationMode();

  bool _loadRemote(const String& path);
  void _buildRemoteItems();
  void _selectRemote(uint8_t index);
  void _holdRemote(uint8_t index);
  void _chooseUploadDestination(const String& path);

  bool _confirm(const char* title);
  bool _pollCancel();
  void _setHttpError(int code, const char* context);
  const char* _httpStatusText(int code) const;

  bool _propfind(const String& path, RemoteEntry* out, uint8_t& count);
  bool _mkcol(const String& path);
  bool _ensureCollection(const String& path);
  bool _getFile(const String& remotePath, const String& localPath, uint64_t size);
  bool _putFile(const String& localPath, const String& remotePath, uint64_t size);

  bool _scanRemote(const String& path, uint64_t& bytes, uint32_t& files, int depth);
  bool _downloadTree(const String& remote, const String& local, int depth);
  bool _scanLocal(const String& path, uint64_t& bytes, uint32_t& files, int depth);
  bool _uploadTree(const String& local, const String& remote, int depth);

  String _urlFor(const String& relative) const;
  String _normalizeBase(String value) const;
  String _normalizeRemote(String value, bool dir = false) const;
  String _parentRemote(const String& value) const;
  String _joinRemote(const String& dir, const String& name) const;
  String _baseName(const String& value) const;
  String _safeLocalName(String value) const;
  String _urlEncodePath(const String& value) const;
  bool _hrefToRelative(String href, String& relative) const;
  String _extractTag(const String& block, const char* localName) const;
  String _xmlDecode(String value) const;
  String _urlDecode(String value) const;

  void _startProgress(bool upload, const String& name,
                      uint64_t bytes = 0, uint32_t files = 0);
  void _updateProgress();
  void _finishProgress(bool ok, const char* error);

  void _renderRemoteBrowser();
  void _renderLocalBrowser();
};
