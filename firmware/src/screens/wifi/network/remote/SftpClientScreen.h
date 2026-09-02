#pragma once

#include "ui/templates/ListScreen.h"
#include "ui/views/BrowseFileView.h"

class SftpClientScreen : public ListScreen
{
public:
  SftpClientScreen() = default;
  SftpClientScreen(const String& host, int port)
    : _host(host), _port(port) {}

  const char* title() override;
  bool inhibitPowerOff() override { return _state == STATE_TRANSFER; }
  ~SftpClientScreen();

  void onInit() override;
  void onUpdate() override;
  void onRender() override;
  void onItemSelected(uint8_t index) override;
  void onBack() override;

private:
  enum State : uint8_t {
    STATE_ACTION,
    STATE_CONFIG,
    STATE_SELECT_AUTH,
    STATE_SELECT_KEY,
    STATE_CONNECTING,
    STATE_HOSTKEY_CONFIRM,
    STATE_REMOTE_LOADING,
    STATE_REMOTE_BROWSER,
    STATE_LOCAL_BROWSER,
    STATE_TRANSFER
  };

  enum AuthMode : uint8_t {
    AUTH_NONE,
    AUTH_PASSWORD,
    AUTH_PRIVATE_KEY
  };

  enum WorkerState : uint8_t {
    WORKER_IDLE,
    WORKER_CONNECTING,
    WORKER_WAIT_HOSTKEY,
    WORKER_READY,
    WORKER_FAILED,
    WORKER_CLOSED
  };

  enum WorkerCommand : uint8_t {
    CMD_NONE,
    CMD_LIST,
    CMD_DOWNLOAD_FILE,
    CMD_DOWNLOAD_DIR,
    CMD_UPLOAD_FILE,
    CMD_UPLOAD_DIR
  };

  struct SharedRemoteEntry {
    String name;
    String path;
    bool isDir = false;
    uint64_t size = 0;
  };

  enum RemoteMode : uint8_t {
    REMOTE_DOWNLOAD,
    REMOTE_UPLOAD_DEST
  };

  static constexpr uint8_t MAX_REMOTE_ENTRIES = 96;
  static constexpr uint32_t SFTP_TASK_STACK_SIZE = 28672;
  static constexpr uint32_t HOLD_MS = 900;
  static constexpr uint32_t WRITE_STALL_TIMEOUT_MS = 12000;
  static constexpr const char* DOWNLOAD_DIR = "/unigeek/wifi/remote/sftp/downloads";

  State _state = STATE_ACTION;
  AuthMode _authMode = AUTH_NONE;

  String _host;
  String _username;
  String _password;
  String _keyPath;
  String _keyData;
  int _port = 22;

  char _hostLabel[40] = {};
  char _portLabel[16] = {};
  char _userLabel[32] = {};
  char _authLabel[20] = "-";
  char _passwordLabel[16] = "-";
  char _keyLabel[32] = {};

  ListItem _actionItems[2] = {
    {"Download", nullptr},
    {"Upload", nullptr},
  };
  ListItem _configItems[6];
  ListItem _authItems[2] = {
    {"Password", nullptr},
    {"Private Key", nullptr},
  };

  BrowseFileView _localBrowser;
  String _localBrowsePath = "/unigeek/wifi/remote/ssh";
  String _uploadLocalPath;
  String _uploadName;
  bool _uploadIsDir = false;

  // Remote-browser UI snapshot. The worker writes _sharedEntries; the UI copies
  // them into these arrays after a LIST command completes.
  SharedRemoteEntry _sharedEntries[MAX_REMOTE_ENTRIES];
  uint8_t _sharedEntryCount = 0;

  String _remoteNames[MAX_REMOTE_ENTRIES + 2];
  String _remoteSubs[MAX_REMOTE_ENTRIES + 2];
  String _remotePaths[MAX_REMOTE_ENTRIES + 2];
  bool _remoteIsDir[MAX_REMOTE_ENTRIES + 2] = {};
  uint64_t _remoteSizes[MAX_REMOTE_ENTRIES + 2] = {};
  ListItem _remoteItems[MAX_REMOTE_ENTRIES + 2];
  uint8_t _remoteCount = 0;
  String _remotePath = ".";
  RemoteMode _remoteMode = REMOTE_DOWNLOAD;
  bool _holdHandled = false;
  bool _transferIsUpload = false;

  TaskHandle_t _sftpTask = nullptr;
  SemaphoreHandle_t _mutex = nullptr;
  volatile WorkerState _workerState = WORKER_IDLE;
  volatile WorkerCommand _command = CMD_NONE;
  volatile bool _stopRequested = false;
  volatile bool _cancelTransfer = false;
  volatile bool _commandDone = false;
  volatile bool _commandOk = false;

  String _commandPath;
  String _commandPath2;
  String _commandError;
  String _hostFingerprint;
  volatile int8_t _hostKeyDecision = 0; // 0 pending, 1 trust, -1 cancel
  uint8_t _hostKeySelection = 0;

  uint64_t _progressDone = 0;
  uint64_t _progressTotal = 0;
  uint32_t _progressFilesDone = 0;
  uint32_t _progressFilesTotal = 0;
  String _progressName;

  void _updateLabels();
  void _rebuildConfig();
  void _showActions();
  void _beginDownload();
  void _beginUpload();
  void _configHost();
  void _configPort();
  void _configUsername();
  void _configPassword();
  void _selectAuthMenu();
  void _selectAuth(uint8_t index);
  void _openKeyPicker();
  void _loadKeyDir(const String& path);
  void _selectKey(uint8_t index);

  void _loadLocalUploadDir(const String& path);
  void _selectLocalUpload(uint8_t index);
  void _holdLocalUpload(uint8_t index);
  void _chooseUploadSource(const String& path, const String& name, bool isDir);
  void _chooseUploadDestinationMode();

  void _connect();
  bool _startWorker();
  static void _workerEntry(void* arg);
  void _worker();
  void _setWorkerError(const char* message);
  void _closeConnection(bool returnToActions = true);

  void _requestList(const String& path);
  void _copyRemoteListing();
  void _selectRemote(uint8_t index);
  void _holdRemote(uint8_t index);
  void _chooseUploadDestination(const String& remoteDir);
  String _parentRemotePath(const String& path) const;
  String _baseName(const String& path) const;

  bool _confirmTransfer(const char* title, const String& name);
  void _requestDownload(const String& remotePath, bool directory);
  void _requestUpload(const String& remoteDir);
  void _updateTransferProgress();

  void _acceptHostKey(bool trust);
  void _renderConnecting();
  void _renderHostKeyConfirm();
  void _renderRemoteBrowser();
  void _renderLocalBrowser();

  // Worker-only helpers. libssh/SFTP objects never leave the worker task.
  bool _workerList(void* sftpOpaque, const String& path);
  bool _workerDownloadFile(void* sftpOpaque, const String& remotePath,
                           const String& localPath, uint64_t knownSize);
  bool _workerDownloadDir(void* sftpOpaque, const String& remotePath,
                          const String& localPath);
  bool _workerCalcDir(void* sftpOpaque, const String& remotePath,
                      uint64_t& bytes, uint32_t& files, int depth);
  bool _workerDownloadTree(void* sftpOpaque, const String& remotePath,
                           const String& localPath, int depth);
  bool _workerUploadFile(void* sftpOpaque, const String& localPath,
                         const String& remotePath, uint64_t knownSize);
  bool _workerUploadDir(void* sftpOpaque, const String& localPath,
                        const String& remotePath);
  bool _workerCalcLocalDir(const String& localPath,
                           uint64_t& bytes, uint32_t& files, int depth);
  bool _workerUploadTree(void* sftpOpaque, const String& localPath,
                         const String& remotePath, int depth);
  bool _workerEnsureRemoteDir(void* sftpOpaque, const String& remotePath);
};
