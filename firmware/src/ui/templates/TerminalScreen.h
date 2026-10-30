#pragma once

#include "ui/templates/ListScreen.h"
#include "ui/views/TextScrollView.h"
#include "ui/views/TerminalCommandLine.h"
#include "utils/terminal/TerminalTransport.h"
#include "utils/terminal/TerminalTranscript.h"
#include "utils/terminal/TerminalLog.h"

// Shared screen for byte-stream terminals (UART Terminal, USB Serial Terminal).
//
// Setup state: a list with the transport's own rows (Baud, pins, ...) followed
// by the common rows Log Mode [+ Filename / AP Name / Network], Line Ending,
// Display and Start. A subclass can leave out the common rows it doesn't
// support and rename Start.
//
// Terminal state: same layout and keys as the network terminals (TCP / SSH):
// status header, scrollable output that follows new data, input line at the
// bottom.
//   - Keyboard boards: type in the input line; Enter sends it with the selected
//     line ending; Fn + ; . , / scroll; Backspace on an empty line leaves.
//   - Other boards: PRESS opens the terminal keyboard, arrows scroll, BACK leaves.
// Leaving asks for confirmation (ConfirmAction, honours the global setting) and
// returns to the setup list. Sent lines are echoed locally as "> text" unless
// the subclass turns local echo off.
//
// Subclasses provide the titles, the transport rows and the transport itself.
class TerminalScreen : public ListScreen {
public:
  enum LineEnding : uint8_t { LINE_END_CR, LINE_END_LF, LINE_END_CRLF };

  ~TerminalScreen() override;

  const char* title() override;
  void onInit() override;
  void onUpdate() override;
  void onRender() override;
  void onBack() override;
  void onItemSelected(uint8_t index) override;

protected:
  explicit TerminalScreen(LineEnding defaultLineEnding) : _lineEnding(defaultLineEnding) {}

  // ── Subclass interface ────────────────────────────────────────────────────
  virtual const char* _setupTitle() const    = 0;  // e.g. "UART Terminal"
  virtual const char* _terminalTitle() const = 0;  // e.g. "UART Terminal"
  virtual const char* _logDir() const        = 0;  // e.g. "/unigeek/utility/uart"
  virtual const char* _logBaseName() const   = 0;  // fallback file name, e.g. "uart_log"
  virtual const char* _defaultApName() const = 0;  // e.g. "UartStream"

  // Fill up to max rows shown before the common ones; return how many.
  virtual uint8_t _transportItems(ListItem* out, uint8_t max) = 0;
  // A transport row was selected. Call _refreshItems() after changing a value.
  virtual void _onTransportItem(uint8_t index) = 0;
  // A configured transport, not begun yet. nullptr if it can't start (the
  // subclass has already told the user why).
  virtual TerminalTransport* _createTransport() = 0;

  virtual const char* _exitQuestion() const { return "Close terminal?"; }

  // Which common setup rows this terminal offers (all by default). Without a
  // row the default applies: log off, the constructor's line ending, text.
  virtual bool _offersLogMode() const    { return true; }
  virtual bool _offersLineEnding() const { return true; }
  virtual bool _offersDisplay() const    { return true; }
  virtual const char* _startLabel() const         { return "Start"; }
  virtual const char* _startFailedMessage() const { return "Failed to start"; }

  // Echo sent lines locally as "> text". Off when the other side echoes
  // (Telnet, SSH).
  virtual bool _localEcho() const { return true; }
  // Wrap output at the character instead of the word, keeping columns of
  // shell output aligned.
  virtual bool _characterWrap() const { return false; }

  // Optional hooks (achievements, extra start-up lines, ...).
  virtual void _onSessionStarted() {}
  virtual void _onDataReceived() {}
  virtual void _onDataSent() {}
  virtual void _onLogStarted(TerminalLog::Mode mode) { (void)mode; }

  // ── Helpers for subclasses ────────────────────────────────────────────────
  void _refreshItems();
  void _pushInfo(const String& message);  // shown on screen, not logged
  bool _running() const { return _state == STATE_RUNNING; }

private:
  enum State : uint8_t { STATE_CONFIG, STATE_RUNNING };
  enum Row : uint8_t { ROW_TRANSPORT, ROW_LOG_MODE, ROW_LOG_TARGET, ROW_LINE_ENDING, ROW_DISPLAY, ROW_START };

  static constexpr uint8_t  MAX_ITEMS          = 12;
  static constexpr uint8_t  MAX_TRANSPORT_ROWS = 6;
  static constexpr int      PAD                = 4;
  static constexpr int      INPUT_H            = 14;
  static constexpr size_t   MAX_RX_PER_UPDATE  = 1024;
  static constexpr uint32_t RENDER_INTERVAL_MS = 50;
  static constexpr uint32_t OVERFLOW_NOTE_MS   = 1000;

  State _state = STATE_CONFIG;

  // ── setup values ──────────────────────────────────────────────────────────
  LineEnding        _lineEnding;
  bool              _hex     = false;
  TerminalLog::Mode _logMode = TerminalLog::MODE_OFF;
  String            _logFilename;
  String            _apName;
  String            _networkSsid;

  char     _logModeLabel[16]   = {};
  char     _logTargetLabel[32] = {};
  char     _lineEndingLabel[6] = {};
  char     _displayLabel[6]    = {};
  ListItem _items[MAX_ITEMS];
  Row      _rows[MAX_ITEMS]    = {};
  uint8_t  _transportRowCount  = 0;

  // ── session ───────────────────────────────────────────────────────────────
  TerminalTransport*  _transport = nullptr;
  TerminalTranscript  _transcript;
  TerminalLog         _log;
  TextScrollView      _outputView;
  TerminalCommandLine _inputLine;
  String              _echoLine;          // terminal-keyboard local echo
  char                _status[48]    = {};
  bool                _remoteClosed  = false;
  bool                _followOutput  = true;
  bool                _dirty         = false;
  uint32_t            _lastRenderMs  = 0;
  uint32_t            _lastDropped   = 0;
  uint32_t            _lastOverflowMs = 0;

  void _configLogMode();
  void _configLogTarget();
  void _configLogFile();
  void _configApName();
  void _configNetwork();
  void _configLineEnding();
  void _configDisplay();

  void _startSession();
  void _stopSession();
  void _requestExit();

  void _drainTransport();
  void _handleKeyboardInput();
  void _openInput();
  void _sendLine(const String& text);
  bool _sendRaw(const uint8_t* data, size_t len);
  bool _writeAll(const uint8_t* data, size_t len);
  bool _writeLineEnding();
  void _scroll(INavigation::Direction dir);
  void _renderTerminal();

  static bool _terminalWriteThunk(void* context, const uint8_t* data, size_t len);
  static void _logLineSink(void* context, const String& line);
  static void _logInfoSink(void* context, const String& message);
};
