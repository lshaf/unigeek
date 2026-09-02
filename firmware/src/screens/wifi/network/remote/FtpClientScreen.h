#pragma once

#include "ui/templates/ListScreen.h"
#include "ui/views/BrowseFileView.h"

class FtpClientScreen : public ListScreen
{
public:
  FtpClientScreen() = default;
  FtpClientScreen(const String& host, int port)
    : _host(host), _port(port) {}

  const char* title() override;
  bool inhibitPowerOff() override { return _state == STATE_TRANSFER; }
  ~FtpClientScreen();

  void onInit() override;
  void onUpdate() override;
  void onRender() override;
  void onItemSelected(uint8_t index) override;
  void onBack() override;

private:
  enum State : uint8_t {
    STATE_CONFIG,
    STATE_CONNECTING,
    STATE_ACTION,
    STATE_REMOTE_LOADING,
    STATE_REMOTE_BROWSER,
    STATE_LOCAL_BROWSER,
    STATE_TRANSFER
  };

  enum WorkerState : uint8_t {
    WORKER_IDLE,
    WORKER_CONNECTING,
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

  enum RemoteMode : uint8_t {
    REMOTE_DOWNLOAD,
    REMOTE_UPLOAD_DEST
  };

  struct SharedRemoteEntry {
    String name;
    String path;
    bool isDir = false;
    uint64_t size = 0;
  };

  static constexpr uint8_t MAX_REMOTE_ENTRIES = 96;
  static constexpr uint32_t FTP_TASK_STACK_SIZE = 24576;
  static constexpr uint32_t HOLD_MS = 900;
  static constexpr uint32_t REPLY_TIMEOUT_MS = 12000;
  static constexpr uint32_t DATA_STALL_TIMEOUT_MS = 12000;
  static constexpr size_t MAX_LIST_PAYLOAD_BYTES = 65536;
  static constexpr const char* DOWNLOAD_DIR = "/unigeek/wifi/remote/ftp/downloads";

  State _state = STATE_CONFIG;
  RemoteMode _remoteMode = REMOTE_DOWNLOAD;

  String _host;
  String _username;
  String _password;
  int _port = 21;

  char _hostLabel[40] = {};
  char _portLabel[16] = {};
  char _userLabel[32] = {};
  char _passwordLabel[16] = "-";

  ListItem _configItems[5];
  ListItem _actionItems[2] = {
    {"Download", nullptr},
    {"Upload", nullptr},
  };

  BrowseFileView _localBrowser;
  String _localBrowsePath = "/";
  String _uploadLocalPath;
  String _uploadName;
  bool _uploadIsDir = false;

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
  bool _holdHandled = false;
  bool _transferIsUpload = false;

  TaskHandle_t _ftpTask = nullptr;
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

  uint64_t _progressDone = 0;
  uint64_t _progressTotal = 0;
  uint32_t _progressFilesDone = 0;
  uint32_t _progressFilesTotal = 0;
  String _progressName;

  void _updateLabels();
  void _rebuildConfig();
  void _showActions();
  void _configHost();
  void _configPort();
  void _configUsername();
  void _configPassword();
  void _connect();
  void _closeConnection(bool returnToConfig = false);

  void _beginDownload();
  void _beginUpload();

  void _loadLocalUploadDir(const String& path);
  void _selectLocalUpload(uint8_t index);
  void _holdLocalUpload(uint8_t index);
  void _chooseUploadSource(const String& path, const String& name, bool isDir);
  void _chooseUploadDestinationMode();

  void _requestList(const String& path);
  void _copyRemoteListing();
  void _selectRemote(uint8_t index);
  void _holdRemote(uint8_t index);
  void _chooseUploadDestination(const String& remoteDir);

  bool _confirmTransfer(const char* title);
  void _requestDownload(const String& remotePath, bool directory);
  void _requestUpload(const String& remoteDir);
  void _updateTransferProgress();

  String _parentRemotePath(const String& path) const;
  String _baseName(const String& path) const;
  String _joinRemote(const String& dir, const String& name) const;

  bool _startWorker();
  static void _workerEntry(void* arg);
  void _worker();
  void _setWorkerError(const char* message);

  // Worker-only FTP helpers.
  bool _ftpReadReply(void* controlOpaque, int& code, String& text,
                     uint32_t timeoutMs = REPLY_TIMEOUT_MS);
  bool _ftpCommand(void* controlOpaque, const String& command,
                   int& code, String& text);
  bool _ftpOpenPassive(void* controlOpaque, void* dataOpaque);
  bool _ftpList(void* controlOpaque, const String& path,
                SharedRemoteEntry* out, uint8_t& count);
  bool _parseMlsdLine(const String& line, const String& parent,
                      SharedRemoteEntry& out);
  bool _parseListLine(const String& line, const String& parent,
                      SharedRemoteEntry& out);

  bool _workerList(void* controlOpaque, const String& path);
  bool _workerDownloadFile(void* controlOpaque, const String& remotePath,
                           const String& localPath, uint64_t knownSize);
  bool _workerDownloadDir(void* controlOpaque, const String& remotePath,
                          const String& localPath);
  bool _workerCalcRemoteDir(void* controlOpaque, const String& remotePath,
                            uint64_t& bytes, uint32_t& files, int depth);
  bool _workerDownloadTree(void* controlOpaque, const String& remotePath,
                           const String& localPath, int depth);

  bool _workerUploadFile(void* controlOpaque, const String& localPath,
                         const String& remotePath, uint64_t knownSize);
  bool _workerUploadDir(void* controlOpaque, const String& localPath,
                        const String& remotePath);
  bool _workerCalcLocalDir(const String& localPath,
                           uint64_t& bytes, uint32_t& files, int depth);
  bool _workerUploadTree(void* controlOpaque, const String& localPath,
                         const String& remotePath, int depth);
  bool _workerEnsureRemoteDir(void* controlOpaque, const String& remotePath);

  void _renderConnecting();
  void _renderRemoteBrowser();
  void _renderLocalBrowser();
};
