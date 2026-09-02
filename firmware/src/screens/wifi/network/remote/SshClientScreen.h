#pragma once

#include "ui/templates/ListScreen.h"
#include "ui/views/TextScrollView.h"
#include "ui/views/TerminalCommandLine.h"
#include "ui/views/BrowseFileView.h"

class SshClientScreen : public ListScreen
{
public:
  SshClientScreen() = default;
  SshClientScreen(const String& host, int port)
    : _host(host), _port(port) {}

  const char* title() override { return "SSH Client"; }
  ~SshClientScreen();

  void onInit() override;
  void onUpdate() override;
  void onRender() override;
  void onItemSelected(uint8_t index) override;
  void onBack() override;

private:
  enum State : uint8_t {
    STATE_CONFIG,
    STATE_SELECT_AUTH,
    STATE_SELECT_KEY,
    STATE_CONNECTING,
    STATE_HOSTKEY_CONFIRM,
    STATE_OUTPUT
  };
  enum AuthMode : uint8_t { AUTH_NONE, AUTH_PASSWORD, AUTH_PRIVATE_KEY };
  enum WorkerState : uint8_t {
    WORKER_IDLE,
    WORKER_CONNECTING,
    WORKER_WAIT_HOSTKEY,
    WORKER_RUNNING,
    WORKER_FAILED,
    WORKER_CLOSED
  };
  enum AnsiParseState : uint8_t { ANSI_DATA, ANSI_ESC, ANSI_CSI, ANSI_OSC, ANSI_OSC_ESC };

  static constexpr int MAX_PARTIAL_LEN       = 160;
  static constexpr int MAX_TRANSCRIPT_CHARS  = 4096;
  static constexpr int MAX_SHARED_RX_CHARS   = 4096;
  static constexpr int MAX_SHARED_TX_CHARS   = 4096;
  static constexpr int MAX_RX_PER_WORKER_LOOP = 2048;
  static constexpr uint32_t WRITE_STALL_TIMEOUT_MS = 2000;
  static constexpr int PAD                   = 4;
  static constexpr int INPUT_H               = 14;
  static constexpr uint32_t SSH_TASK_STACK_SIZE = 24576;

  State _state = STATE_CONFIG;

  String _host;
  String _username;
  String _password;
  String _keyPath;
  String _keyData;
  AuthMode _authMode = AUTH_NONE;
  int    _port = 22;

  char _hostLabel[40] = {};
  char _portLabel[16] = {};
  char _userLabel[32] = {};
  char _authLabel[20] = "-";
  char _passwordLabel[16] = "-";
  char _keyLabel[32] = {};
  ListItem _items[6];
  BrowseFileView _browser;
  String _browsePath = "/unigeek/wifi/remote/ssh";
  ListItem _authItems[2] = {
    {"Password", nullptr},
    {"Private Key", nullptr},
  };

  TaskHandle_t _sshTask = nullptr;
  SemaphoreHandle_t _ioMutex = nullptr;
  volatile WorkerState _workerState = WORKER_IDLE;
  volatile bool _stopRequested = false;
  String _workerRx;
  String _workerTx;
  String _workerError;
  String _hostFingerprint;
  volatile int8_t _hostKeyDecision = 0; // 0=pending, 1=trust, -1=cancel
  uint8_t _hostKeySelection = 0;        // 0=Trust, 1=Cancel

  TextScrollView _outputView;
  TerminalCommandLine _inputLine;
  String _transcript;
  String _partialLine;
  int    _lineCursor = 0;
  String _ansiParams;
  AnsiParseState _ansiState = ANSI_DATA;
  bool _remoteClosed = false;
  bool _followOutput = true;

  void _updateLabels();
  void _rebuildItems();
  void _configHost();
  void _configPort();
  void _configUsername();
  void _configPassword();
  void _auth();
  void _selectAuth(uint8_t index);
  void _openKeyPicker();
  void _loadKeyDir(const String& path);
  void _selectKey(uint8_t index);
  void _connect();
  void _closeConnection(bool returnToConfig = true);

  bool _startSshWorker();
  static void _sshWorkerEntry(void* arg);
  void _sshWorker();
  void _setWorkerError(const char* message);
  void _drainWorkerRx();
  void _openCommandInput();
  void _handleTerminalInput();
  void _sendCommand(const String& command);

  void _appendByte(uint8_t c);
  void _handleAnsiCsi(uint8_t finalByte);
  int  _ansiParam(int index, int defaultValue) const;
  void _putLineChar(char c);
  void _commitPartial();
  void _pushOutputLine(const String& line);
  void _trimTranscript();
  void _clearOutput();

  int _terminalCols();
  int _terminalRows();
  void _renderConnecting();
  void _renderHostKeyConfirm();
  void _acceptHostKey(bool trust);
  void _renderOutput();
};
