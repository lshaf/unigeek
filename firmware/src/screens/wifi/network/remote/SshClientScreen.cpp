#include "SshClientScreen.h"

#include "core/Device.h"
#include "core/INavigation.h"
#include "core/ScreenManager.h"
#include "ui/actions/InputTextAction.h"
#include "ui/actions/InputNumberAction.h"
#include "ui/actions/ShowStatusAction.h"

#include <WiFi.h>
#include <libssh/libssh.h>

SshClientScreen::~SshClientScreen() {
#ifdef DEVICE_HAS_KEYBOARD
  if (Uni.Nav) Uni.Nav->setSuppressKeys(false);
#endif
  _closeConnection(false);

  // The worker owns libssh objects. Do not destroy synchronization primitives
  // until it has finished its cleanup.
  uint32_t start = millis();
  while (_sshTask && millis() - start < 3000) {
    delay(10);
  }

  if (_ioMutex && !_sshTask) {
    vSemaphoreDelete(_ioMutex);
    _ioMutex = nullptr;
  }
}

void SshClientScreen::onInit() {
  _outputView.setWrapMode(TextScrollView::WRAP_CHARACTER);
  if (!_ioMutex) _ioMutex = xSemaphoreCreateMutex();
  _updateLabels();
  _rebuildItems();
}

void SshClientScreen::onUpdate() {
  if (_state == STATE_HOSTKEY_CONFIRM) {
    if (!Uni.Nav->wasPressed()) return;

    auto dir = Uni.Nav->readDirection();
    if (dir == INavigation::DIR_BACK) {
      _acceptHostKey(false);
      return;
    }

    if (dir == INavigation::DIR_UP || dir == INavigation::DIR_DOWN ||
        dir == INavigation::DIR_LEFT || dir == INavigation::DIR_RIGHT) {
      _hostKeySelection = (_hostKeySelection == 0) ? 1 : 0;
      render();
      return;
    }

    if (dir == INavigation::DIR_PRESS) {
      _acceptHostKey(_hostKeySelection == 0);
      return;
    }
    return;
  }

  if (_state == STATE_SELECT_AUTH || _state == STATE_SELECT_KEY) {
    ListScreen::onUpdate();
    return;
  }

  if (_state == STATE_CONNECTING) {
    if (_workerState == WORKER_WAIT_HOSTKEY) {
      _hostKeySelection = 0;
      _state = STATE_HOSTKEY_CONFIRM;
      render();
      return;
    }

    if (_workerState == WORKER_RUNNING) {
      _password = "";
      _keyData = "";
      _clearOutput();
      _remoteClosed = false;
      _state = STATE_OUTPUT;
      _followOutput = true;
      _inputLine.clear();
#ifdef DEVICE_HAS_KEYBOARD
      if (Uni.Nav) Uni.Nav->setSuppressKeys(true);
#endif
      render();
      return;
    }

    if (_workerState == WORKER_FAILED) {
      String error = "SSH connection failed";
      if (_ioMutex && xSemaphoreTake(_ioMutex, pdMS_TO_TICKS(20)) == pdTRUE) {
        if (_workerError.length() > 0) error = _workerError;
        xSemaphoreGive(_ioMutex);
      }
      _password = "";
      _keyData = "";
      _workerState = WORKER_IDLE;
      _state = STATE_CONFIG;
      ShowStatusAction::show(error.c_str(), 1800);
      render();
      return;
    }

    if (_workerState == WORKER_CLOSED) {
      _password = "";
      _keyData = "";
      _workerState = WORKER_IDLE;
      _state = STATE_CONFIG;
      _updateLabels();
      _rebuildItems();
      render();
      return;
    }

    if (Uni.Nav->wasPressed() && Uni.Nav->readDirection() == INavigation::DIR_BACK) {
      // Request cancellation, but stay in CONNECTING until the worker has
      // actually released its libssh objects.
      _stopRequested = true;
    }
    return;
  }

  if (_state == STATE_CONFIG) {
    ListScreen::onUpdate();
    return;
  }

  _drainWorkerRx();

  if (!_remoteClosed && (_workerState == WORKER_CLOSED || _workerState == WORKER_FAILED)) {
    _remoteClosed = true;
    if (_partialLine.length() > 0) _commitPartial();

    if (_workerState == WORKER_FAILED) {
      String error = "SSH session error";
      if (_ioMutex && xSemaphoreTake(_ioMutex, pdMS_TO_TICKS(20)) == pdTRUE) {
        if (_workerError.length() > 0) error = _workerError;
        xSemaphoreGive(_ioMutex);
      }
      _pushOutputLine("[" + error + "]");
    } else {
      _pushOutputLine("[Connection closed]");
    }
    render();
  }

#ifdef DEVICE_HAS_KEYBOARD
  _handleTerminalInput();
  if (_state != STATE_OUTPUT) return;
#endif

  if (!Uni.Nav->wasPressed()) return;
  auto dir = Uni.Nav->readDirection();

  if (dir == INavigation::DIR_BACK) {
    _closeConnection(true);
    return;
  }

  if (dir == INavigation::DIR_PRESS && !_remoteClosed) {
#ifndef DEVICE_HAS_KEYBOARD
    _openCommandInput();
#endif
    return;
  }

  if (dir == INavigation::DIR_UP || dir == INavigation::DIR_DOWN ||
      dir == INavigation::DIR_LEFT || dir == INavigation::DIR_RIGHT) {
    if (_outputView.onNav(dir)) _followOutput = _outputView.isAtBottom();
  }
}

void SshClientScreen::onRender() {
  if (_state == STATE_CONFIG || _state == STATE_SELECT_AUTH || _state == STATE_SELECT_KEY) {
    ListScreen::onRender();
    return;
  }
  if (_state == STATE_CONNECTING) {
    _renderConnecting();
    return;
  }
  if (_state == STATE_HOSTKEY_CONFIRM) {
    _renderHostKeyConfirm();
    return;
  }
  _renderOutput();
}

void SshClientScreen::onItemSelected(uint8_t index) {
  if (_state == STATE_SELECT_AUTH) {
    _selectAuth(index);
    return;
  }

  if (_state == STATE_SELECT_KEY) {
    _selectKey(index);
    return;
  }

  switch (index) {
    case 0: _configHost();     break;
    case 1: _configPort();     break;
    case 2: _configUsername(); break;
    case 3: _auth();           break;
    case 4:
      if (_authMode == AUTH_PASSWORD)          _configPassword();
      else if (_authMode == AUTH_PRIVATE_KEY)  _openKeyPicker();
      else                                     _connect();
      break;
    case 5: _connect();        break;
    default: break;
  }
}

void SshClientScreen::onBack() {
  if (_state == STATE_HOSTKEY_CONFIRM) {
    _acceptHostKey(false);
    return;
  }

  if (_state == STATE_CONNECTING) {
    _stopRequested = true;
    return;
  }

  if (_state == STATE_SELECT_AUTH || _state == STATE_SELECT_KEY) {
    _state = STATE_CONFIG;
    _updateLabels();
    _rebuildItems();
    render();
    return;
  }

  if (_state == STATE_OUTPUT) _closeConnection(true);
  else Screen.goBack();
}

void SshClientScreen::_updateLabels() {
  if (_host.length() == 0) snprintf(_hostLabel, sizeof(_hostLabel), "-");
  else                     _host.toCharArray(_hostLabel, sizeof(_hostLabel));

  if (_username.length() == 0) snprintf(_userLabel, sizeof(_userLabel), "-");
  else                         _username.toCharArray(_userLabel, sizeof(_userLabel));

  if (_authMode == AUTH_PASSWORD) {
    snprintf(_authLabel, sizeof(_authLabel), "Password");
  } else if (_authMode == AUTH_PRIVATE_KEY) {
    snprintf(_authLabel, sizeof(_authLabel), "Private Key");
  } else {
    snprintf(_authLabel, sizeof(_authLabel), "-");
  }

  snprintf(_passwordLabel, sizeof(_passwordLabel), "%s",
           _password.length() > 0 ? "********" : "-");

  if (_keyPath.length() == 0) {
    snprintf(_keyLabel, sizeof(_keyLabel), "-");
  } else {
    int slash = _keyPath.lastIndexOf('/');
    String keyName = (slash >= 0) ? _keyPath.substring(slash + 1) : _keyPath;
    keyName.toCharArray(_keyLabel, sizeof(_keyLabel));
  }

  snprintf(_portLabel, sizeof(_portLabel), "%d", _port);
}

void SshClientScreen::_rebuildItems() {
  uint8_t sel = _selectedIndex;
  _items[0] = {"Host", _hostLabel};
  _items[1] = {"Port", _portLabel};
  _items[2] = {"Username", _userLabel};
  _items[3] = {"Auth Method", _authLabel};

  if (_authMode == AUTH_PASSWORD) {
    _items[4] = {"Password", _passwordLabel};
    _items[5] = {"Connect", nullptr};
    setItems(_items, 6, min<uint8_t>(sel, 5));
  } else if (_authMode == AUTH_PRIVATE_KEY) {
    _items[4] = {"Key File", _keyLabel};
    _items[5] = {"Connect", nullptr};
    setItems(_items, 6, min<uint8_t>(sel, 5));
  } else {
    _items[4] = {"Connect", nullptr};
    setItems(_items, 5, min<uint8_t>(sel, 4));
  }
}

void SshClientScreen::_configHost() {
  String initial = _host;
  if (initial.length() == 0 && WiFi.status() == WL_CONNECTED) {
    IPAddress ip = WiFi.localIP();
    initial = String(ip[0]) + "." + String(ip[1]) + "." + String(ip[2]) + ".";
  }

#ifdef DEVICE_HAS_KEYBOARD
  const auto hostInputMode = InputTextAction::INPUT_TEXT;
#else
  const auto hostInputMode = InputTextAction::INPUT_IP_ADDRESS;
#endif

  String value = InputTextAction::popup("Host", initial, hostInputMode);
  if (!InputTextAction::wasCancelled() && value.length() > 0) {
    _host = value;
    _updateLabels();
    _rebuildItems();
  }
  render();
}

void SshClientScreen::_configPort() {
  int value = InputNumberAction::popup("Port", 1, 65535, _port);
  if (!InputNumberAction::wasCancelled()) {
    _port = value;
    _updateLabels();
    _rebuildItems();
  }
  render();
}

void SshClientScreen::_configUsername() {
  String value = InputTextAction::popup("Username", _username, InputTextAction::INPUT_TEXT);
  if (!InputTextAction::wasCancelled() && value.length() > 0) {
    _username = value;
    _updateLabels();
    _rebuildItems();
  }
  render();
}

void SshClientScreen::_configPassword() {
  String value = InputTextAction::popup(
    "Password", "", InputTextAction::INPUT_TEXT);

  if (!InputTextAction::wasCancelled()) {
    _password = value;
  }

  _selectedIndex = 4;
  _updateLabels();
  _rebuildItems();
  render();
}

void SshClientScreen::_auth() {
  _state = STATE_SELECT_AUTH;

  // If no method has been chosen yet, start on Password without making it the
  // active method until the user actually confirms the selection.
  uint8_t selected = (_authMode == AUTH_PRIVATE_KEY) ? 1 : 0;
  setItems(_authItems, 2, selected);
  render();
}

void SshClientScreen::_selectAuth(uint8_t index) {
  if (index > 1) return;

  _authMode = (index == 0) ? AUTH_PASSWORD : AUTH_PRIVATE_KEY;

  if (_authMode == AUTH_PASSWORD) {
    _keyData = "";
  } else {
    _password = "";
  }

  // Return to CONFIG with Auth still selected. The submenu index (0/1) must not
  // leak into the main menu selection, otherwise CONFIG jumps to Host/Port.
  _selectedIndex = 3;
  _state = STATE_CONFIG;
  _updateLabels();
  _rebuildItems();
  render();
}

void SshClientScreen::_openKeyPicker() {
  if (!Uni.Storage || !Uni.Storage->isAvailable()) {
    ShowStatusAction::show("Storage not available", 1500);
    render();
    return;
  }

  // Keep SSH keys under the Wi-Fi subtree by default. Create each level
  // explicitly because not every storage backend creates parent directories.
  Uni.Storage->makeDir("/unigeek");
  Uni.Storage->makeDir("/unigeek/wifi");
  Uni.Storage->makeDir("/unigeek/wifi/remote");
  Uni.Storage->makeDir("/unigeek/wifi/remote/ssh");

  _browsePath = "/unigeek/wifi/remote/ssh";
  _browser.root = "/";
  _state = STATE_SELECT_KEY;
  _loadKeyDir(_browsePath);
}

void SshClientScreen::_loadKeyDir(const String& path) {
  _browsePath = path;
  uint8_t n = _browser.load(this, path, {}, "KEY");
  setItems(_browser.items(), n);
  render();
}

void SshClientScreen::_selectKey(uint8_t index) {
  if (index >= _browser.count()) return;

  const auto& entry = _browser.entry(index);
  if (entry.isDir) {
    _loadKeyDir(entry.path);
    return;
  }

  _keyPath = entry.path;
  _state = STATE_CONFIG;
  _updateLabels();
  _rebuildItems();
  render();
}

void SshClientScreen::_connect() {
  if (_host.length() == 0) {
    ShowStatusAction::show("Host required", 1200);
    render();
    return;
  }
  if (_username.length() == 0) {
    ShowStatusAction::show("Username required", 1200);
    render();
    return;
  }
  if (_port < 1 || _port > 65535) {
    ShowStatusAction::show("Port required", 1200);
    render();
    return;
  }
  if (WiFi.status() != WL_CONNECTED) {
    ShowStatusAction::show("WiFi not connected", 1500);
    render();
    return;
  }

  if (_authMode == AUTH_NONE) {
    ShowStatusAction::show("Auth method required", 1500);
    render();
    return;
  }

  if (_authMode == AUTH_PASSWORD) {
    if (_password.length() == 0) {
      String value = InputTextAction::popup("Password", "", InputTextAction::INPUT_TEXT);
      if (InputTextAction::wasCancelled()) {
        render();
        return;
      }
      _password = value;
    }
  } else {
    if (_keyPath.length() == 0) {
      ShowStatusAction::show("Key file required", 1500);
      render();
      return;
    }
    if (!Uni.Storage || !Uni.Storage->isAvailable()) {
      ShowStatusAction::show("Storage not available", 1500);
      render();
      return;
    }
    _keyData = Uni.Storage->readFile(_keyPath.c_str());
    if (_keyData.length() == 0) {
      ShowStatusAction::show("Key read failed", 1500);
      render();
      return;
    }
  }

  if (!_ioMutex) _ioMutex = xSemaphoreCreateMutex();
  if (!_ioMutex) {
    ShowStatusAction::show("SSH mutex failed", 1500);
    return;
  }

  if (!_startSshWorker()) {
    _password = "";
    _keyData = "";
    ShowStatusAction::show("SSH task failed", 1500);
    render();
    return;
  }

  _state = STATE_CONNECTING;
  render();
}

bool SshClientScreen::_startSshWorker() {
  if (_sshTask) return false;

  _stopRequested = false;
  _workerState = WORKER_CONNECTING;
  _hostKeyDecision = 0;

  if (xSemaphoreTake(_ioMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
    _workerRx = "";
    _workerTx = "";
    _workerError = "";
    _hostFingerprint = "";
    xSemaphoreGive(_ioMutex);
  }

  BaseType_t ok;
#if defined(SOC_CPU_CORES_NUM) && SOC_CPU_CORES_NUM > 1
  ok = xTaskCreatePinnedToCore(
    _sshWorkerEntry, "UG SSH", SSH_TASK_STACK_SIZE, this, 1, &_sshTask, 1);
#else
  ok = xTaskCreate(
    _sshWorkerEntry, "UG SSH", SSH_TASK_STACK_SIZE, this, 1, &_sshTask);
#endif

  if (ok != pdPASS) {
    _sshTask = nullptr;
    _workerState = WORKER_IDLE;
    return false;
  }
  return true;
}

void SshClientScreen::_sshWorkerEntry(void* arg) {
  static_cast<SshClientScreen*>(arg)->_sshWorker();
}

void SshClientScreen::_setWorkerError(const char* message) {
  if (_ioMutex && xSemaphoreTake(_ioMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
    _workerError = message ? message : "SSH failed";
    xSemaphoreGive(_ioMutex);
  }
  _workerState = WORKER_FAILED;
}

void SshClientScreen::_sshWorker() {
  ssh_session session = nullptr;
  ssh_channel channel = nullptr;

  // Copy connection settings before entering libssh. The CONFIG UI is no longer
  // editable while STATE_CONNECTING is active.
  String host = _host;
  String user = _username;
  String password = _password;
  String keyData = _keyData;
  AuthMode authMode = _authMode;
  int port = _port;
  int cols = _terminalCols();
  int rows = _terminalRows();

  session = ssh_new();
  if (!session) {
    _setWorkerError("SSH session failed");
    goto cleanup;
  }

  if (ssh_options_set(session, SSH_OPTIONS_HOST, host.c_str()) != SSH_OK ||
      ssh_options_set(session, SSH_OPTIONS_USER, user.c_str()) != SSH_OK ||
      ssh_options_set(session, SSH_OPTIONS_PORT, &port) != SSH_OK) {
    _setWorkerError("SSH options failed");
    goto cleanup;
  }

  if (_stopRequested) goto cleanup;

  if (ssh_connect(session) != SSH_OK) {
    _setWorkerError("SSH connect failed");
    goto cleanup;
  }

  if (_stopRequested) goto cleanup;

  // Present the server identity before sending any user credential.
  {
    ssh_key serverKey = nullptr;
    unsigned char* hash = nullptr;
    size_t hashLen = 0;

    if (ssh_get_server_publickey(session, &serverKey) != SSH_OK || !serverKey) {
      if (serverKey) ssh_key_free(serverKey);
      _setWorkerError("SSH host key failed");
      goto cleanup;
    }

    int rc = ssh_get_publickey_hash(
      serverKey, SSH_PUBLICKEY_HASH_SHA256, &hash, &hashLen);
    ssh_key_free(serverKey);

    if (rc != SSH_OK || !hash || hashLen == 0) {
      if (hash) ssh_clean_pubkey_hash(&hash);
      _setWorkerError("SSH fingerprint failed");
      goto cleanup;
    }

    char* hex = ssh_get_hexa(hash, hashLen);
    ssh_clean_pubkey_hash(&hash);

    if (!hex) {
      _setWorkerError("SSH fingerprint failed");
      goto cleanup;
    }

    if (_ioMutex && xSemaphoreTake(_ioMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
      _hostFingerprint = "SHA256 ";
      _hostFingerprint += hex;
      xSemaphoreGive(_ioMutex);
    }
    ssh_string_free_char(hex);

    _hostKeyDecision = 0;
    _workerState = WORKER_WAIT_HOSTKEY;

    while (!_stopRequested && _hostKeyDecision == 0) {
      vTaskDelay(pdMS_TO_TICKS(20));
    }

    if (_stopRequested || _hostKeyDecision < 0) {
      _workerState = WORKER_CLOSED;
      goto cleanup;
    }

    _workerState = WORKER_CONNECTING;
  }

  if (_stopRequested) goto cleanup;

  if (authMode == AUTH_PASSWORD) {
    if (ssh_userauth_password(session, nullptr, password.c_str()) != SSH_AUTH_SUCCESS) {
      _setWorkerError("SSH authentication failed");
      goto cleanup;
    }
  } else {
    ssh_key privateKey = nullptr;
    ssh_key publicKey = nullptr;

    int rc = ssh_pki_import_privkey_base64(
      keyData.c_str(), nullptr, nullptr, nullptr, &privateKey);
    if (rc != SSH_OK || !privateKey) {
      if (privateKey) ssh_key_free(privateKey);
      _setWorkerError("SSH key import failed");
      goto cleanup;
    }

    rc = ssh_pki_export_privkey_to_pubkey(privateKey, &publicKey);
    if (rc != SSH_OK || !publicKey) {
      if (publicKey) ssh_key_free(publicKey);
      ssh_key_free(privateKey);
      _setWorkerError("SSH public key failed");
      goto cleanup;
    }

    rc = ssh_userauth_try_publickey(session, nullptr, publicKey);
    if (rc != SSH_AUTH_SUCCESS) {
      ssh_key_free(publicKey);
      ssh_key_free(privateKey);
      _setWorkerError("SSH key not accepted");
      goto cleanup;
    }

    rc = ssh_userauth_publickey(session, nullptr, privateKey);
    ssh_key_free(publicKey);
    ssh_key_free(privateKey);

    if (rc != SSH_AUTH_SUCCESS) {
      _setWorkerError("SSH key auth failed");
      goto cleanup;
    }
  }

  password = "";
  keyData = "";

  channel = ssh_channel_new(session);
  if (!channel || ssh_channel_open_session(channel) != SSH_OK) {
    _setWorkerError("SSH channel failed");
    goto cleanup;
  }

  if (ssh_channel_request_pty_size(channel, "vt100", cols, rows) != SSH_OK) {
    _setWorkerError("SSH PTY failed");
    goto cleanup;
  }

  if (ssh_channel_request_shell(channel) != SSH_OK) {
    _setWorkerError("SSH shell failed");
    goto cleanup;
  }

  ssh_set_blocking(session, 0);
  _workerState = WORKER_RUNNING;

  while (!_stopRequested) {
    bool activity = false;
    char buf[256];

    // TX queue: move complete commands out of the shared String, then keep
    // retrying a temporarily non-writable channel. A zero-byte write is not an
    // error in non-blocking mode and must not silently discard the tail.
    String tx;
    if (_ioMutex && xSemaphoreTake(_ioMutex, pdMS_TO_TICKS(10)) == pdTRUE) {
      if (_workerTx.length() > 0) {
        tx = _workerTx;
        _workerTx = "";
      }
      xSemaphoreGive(_ioMutex);
    }

    if (tx.length() > 0) {
      int sent = 0;
      uint32_t lastProgress = millis();

      while (sent < (int)tx.length() && !_stopRequested) {
        int n = ssh_channel_write(channel, tx.c_str() + sent, tx.length() - sent);
        if (n == SSH_ERROR) {
          _setWorkerError("SSH write failed");
          goto cleanup;
        }

        if (n > 0) {
          sent += n;
          lastProgress = millis();
          activity = true;
          continue;
        }

        if (millis() - lastProgress >= WRITE_STALL_TIMEOUT_MS) {
          _setWorkerError("SSH write timed out");
          goto cleanup;
        }
        vTaskDelay(pdMS_TO_TICKS(2));
      }
    }

    int rxThisLoop = 0;
    for (int stream = 0; stream <= 1 && rxThisLoop < MAX_RX_PER_WORKER_LOOP; ++stream) {
      while (!_stopRequested && rxThisLoop < MAX_RX_PER_WORKER_LOOP) {
        int room = min<int>(sizeof(buf), MAX_RX_PER_WORKER_LOOP - rxThisLoop);
        int n = ssh_channel_read_nonblocking(channel, buf, room, stream);
        if (n == SSH_ERROR) {
          _setWorkerError("SSH read failed");
          goto cleanup;
        }
        if (n <= 0) break;

        if (_ioMutex && xSemaphoreTake(_ioMutex, pdMS_TO_TICKS(20)) == pdTRUE) {
          if ((int)_workerRx.length() + n > MAX_SHARED_RX_CHARS) {
            int drop = (int)_workerRx.length() + n - MAX_SHARED_RX_CHARS;
            _workerRx.remove(0, min(drop, (int)_workerRx.length()));
          }
          _workerRx.concat(buf, n);
          xSemaphoreGive(_ioMutex);
        }

        rxThisLoop += n;
        activity = true;
      }
    }

    if (!ssh_is_connected(session) || !ssh_channel_is_open(channel) ||
        ssh_channel_is_eof(channel)) {
      break;
    }

    if (!activity) vTaskDelay(pdMS_TO_TICKS(10));
  }

  if (_workerState != WORKER_FAILED) _workerState = WORKER_CLOSED;

cleanup:
  if (channel) {
    ssh_channel_send_eof(channel);
    ssh_channel_close(channel);
    ssh_channel_free(channel);
    channel = nullptr;
  }

  if (session) {
    ssh_disconnect(session);
    ssh_free(session);
    session = nullptr;
  }

  password = "";
  keyData = "";
  _sshTask = nullptr;
  vTaskDelete(nullptr);
}

void SshClientScreen::_closeConnection(bool returnToConfig) {
#ifdef DEVICE_HAS_KEYBOARD
  if (Uni.Nav) Uni.Nav->setSuppressKeys(false);
#endif
  _inputLine.clear();
  _stopRequested = true;
  _hostKeyDecision = -1;

  // When the session is already running, the worker is non-blocking and should
  // exit promptly. Avoid deleting it from the UI task because libssh objects
  // belong exclusively to the worker.
  uint32_t start = millis();
  while (_sshTask && _workerState == WORKER_RUNNING && millis() - start < 500) {
    delay(5);
  }

  _password = "";
  _keyData = "";
  _remoteClosed = false;

  if (returnToConfig) {
    _state = STATE_CONFIG;
    _updateLabels();
    _rebuildItems();
    render();
  }
}

void SshClientScreen::_openCommandInput() {
#ifdef DEVICE_HAS_KEYBOARD
  return;
#else
  String command = InputTextAction::popup(
    "Command", _inputLine.text(), InputTextAction::INPUT_TEXT);

  if (!InputTextAction::wasCancelled()) {
    _inputLine.clear();
    _sendCommand(command);
  }

  _drainWorkerRx();
  render();
#endif
}

void SshClientScreen::_handleTerminalInput() {
#ifdef DEVICE_HAS_KEYBOARD
  if (!Uni.Keyboard) return;

  auto action = _inputLine.consume(Uni.Keyboard);
  switch (action) {
    case TerminalCommandLine::ACTION_EXIT:
      _closeConnection(true);
      return;
    case TerminalCommandLine::ACTION_SUBMIT:
      if (!_remoteClosed) {
        String command = _inputLine.text();
        _inputLine.clear();
        _sendCommand(command);
        _followOutput = true;
      }
      render();
      return;
    case TerminalCommandLine::ACTION_SCROLL_UP:
      if (_outputView.onNav(INavigation::DIR_UP)) _followOutput = _outputView.isAtBottom();
      return;
    case TerminalCommandLine::ACTION_SCROLL_DOWN:
      if (_outputView.onNav(INavigation::DIR_DOWN)) _followOutput = _outputView.isAtBottom();
      return;
    case TerminalCommandLine::ACTION_SCROLL_LEFT:
      if (_outputView.onNav(INavigation::DIR_LEFT)) _followOutput = _outputView.isAtBottom();
      return;
    case TerminalCommandLine::ACTION_SCROLL_RIGHT:
      if (_outputView.onNav(INavigation::DIR_RIGHT)) _followOutput = _outputView.isAtBottom();
      return;
    case TerminalCommandLine::ACTION_CHANGED:
      render();
      return;
    default:
      return;
  }
#endif
}

void SshClientScreen::_sendCommand(const String& command) {
  if (_remoteClosed || _workerState != WORKER_RUNNING || !_ioMutex) return;

  String outgoing = command;
  outgoing += "\r\n";

  bool queued = false;
  bool queueFull = false;

  if (xSemaphoreTake(_ioMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
    if ((int)_workerTx.length() + (int)outgoing.length() <= MAX_SHARED_TX_CHARS) {
      _workerTx += outgoing;
      queued = true;
    } else {
      queueFull = true;
    }
    xSemaphoreGive(_ioMutex);
  }

  if (!queued) {
    _pushOutputLine(queueFull ? "[Send queue full]" : "[Send queue busy]");
    render();
  }
}

void SshClientScreen::_drainWorkerRx() {
  if (!_ioMutex) return;

  String data;
  if (xSemaphoreTake(_ioMutex, pdMS_TO_TICKS(10)) == pdTRUE) {
    if (_workerRx.length() > 0) {
      data = _workerRx;
      _workerRx = "";
    }
    xSemaphoreGive(_ioMutex);
  }

  if (data.length() == 0) return;

  for (int i = 0; i < (int)data.length(); ++i) {
    _appendByte((uint8_t)data[i]);
  }
  render();
}

void SshClientScreen::_appendByte(uint8_t c) {
  switch (_ansiState) {
    case ANSI_ESC:
      _ansiParams = "";
      if (c == '[')      _ansiState = ANSI_CSI;
      else if (c == ']') _ansiState = ANSI_OSC;
      else               _ansiState = ANSI_DATA;
      return;

    case ANSI_CSI:
      if (c >= 0x40 && c <= 0x7e) {
        _handleAnsiCsi(c);
        _ansiState = ANSI_DATA;
        _ansiParams = "";
      } else if (_ansiParams.length() < 24 &&
                 ((c >= '0' && c <= '9') || c == ';' || c == '?' || c == '>')) {
        _ansiParams += (char)c;
      }
      return;

    case ANSI_OSC:
      if (c == 0x07)      _ansiState = ANSI_DATA;
      else if (c == 0x1b) _ansiState = ANSI_OSC_ESC;
      return;

    case ANSI_OSC_ESC:
      _ansiState = (c == '\\') ? ANSI_DATA : ANSI_OSC;
      return;

    case ANSI_DATA:
    default:
      if (c == 0x1b) {
        _ansiState = ANSI_ESC;
        return;
      }
      break;
  }

  if (c == '\r') {
    _lineCursor = 0;
    return;
  }

  if (c == '\n') {
    _commitPartial();
    return;
  }

  if (c == '\b' || c == 0x7f) {
    if (_lineCursor > 0) _lineCursor--;
    return;
  }

  if (c == '\t') {
    int spaces = 8 - (_lineCursor % 8);
    while (spaces-- > 0) _putLineChar(' ');
  } else if (c >= 0x20 && c <= 0x7e) {
    _putLineChar((char)c);
  }

  if ((int)_partialLine.length() >= MAX_PARTIAL_LEN) _commitPartial();
}

void SshClientScreen::_putLineChar(char c) {
  if (_lineCursor < 0) _lineCursor = 0;
  if (_lineCursor > (int)_partialLine.length()) {
    while ((int)_partialLine.length() < _lineCursor) _partialLine += ' ';
  }

  if (_lineCursor < (int)_partialLine.length())
    _partialLine.setCharAt(_lineCursor, c);
  else
    _partialLine += c;

  _lineCursor++;
}

int SshClientScreen::_ansiParam(int index, int defaultValue) const {
  int current = 0;
  int value = 0;
  bool haveDigit = false;

  for (int i = 0; i <= (int)_ansiParams.length(); i++) {
    char c = (i < (int)_ansiParams.length()) ? _ansiParams[i] : ';';
    if (c >= '0' && c <= '9') {
      value = value * 10 + (c - '0');
      haveDigit = true;
    } else if (c == ';') {
      if (current == index) return haveDigit ? value : defaultValue;
      current++;
      value = 0;
      haveDigit = false;
    }
  }
  return defaultValue;
}

void SshClientScreen::_handleAnsiCsi(uint8_t finalByte) {
  int n = _ansiParam(0, 1);
  if (n < 1) n = 1;

  switch (finalByte) {
    case 'm':
      break;
    case 'C':
      _lineCursor = min((int)_partialLine.length(), _lineCursor + n);
      break;
    case 'D':
      _lineCursor = max(0, _lineCursor - n);
      break;
    case 'G':
      _lineCursor = max(0, n - 1);
      if (_lineCursor > MAX_PARTIAL_LEN) _lineCursor = MAX_PARTIAL_LEN;
      break;
    case 'K': {
      int mode = _ansiParam(0, 0);
      if (mode == 2) {
        _partialLine = "";
        _lineCursor = 0;
      } else if (mode == 1) {
        int upto = min(_lineCursor + 1, (int)_partialLine.length());
        for (int i = 0; i < upto; i++) _partialLine.setCharAt(i, ' ');
      } else if (_lineCursor < (int)_partialLine.length()) {
        _partialLine.remove(_lineCursor);
      }
      break;
    }
    default:
      break;
  }
}

void SshClientScreen::_commitPartial() {
  _pushOutputLine(_partialLine);
  _partialLine = "";
  _lineCursor = 0;
}

void SshClientScreen::_pushOutputLine(const String& line) {
  _transcript += line;
  _transcript += '\n';
  _trimTranscript();
}

void SshClientScreen::_trimTranscript() {
  if ((int)_transcript.length() <= MAX_TRANSCRIPT_CHARS) return;
  int excess = (int)_transcript.length() - MAX_TRANSCRIPT_CHARS;
  int cut = _transcript.indexOf('\n', excess);
  if (cut >= 0) _transcript.remove(0, cut + 1);
  else          _transcript.remove(0, excess);
}

void SshClientScreen::_clearOutput() {
  _transcript = "";
  _partialLine = "";
  _lineCursor = 0;
  _ansiParams = "";
  _ansiState = ANSI_DATA;
  _followOutput = true;
  _outputView.setContent("");
}

int SshClientScreen::_terminalCols() {
  const int textW = max(1, bodyW() - 3 - 8);
  return max(8, textW / 6);
}

int SshClientScreen::_terminalRows() {
  const int outputH = max(1, bodyH() - PAD - 11 - 4 - INPUT_H - 2);
  return max(1, outputH / 11);
}

void SshClientScreen::_renderConnecting() {
  auto& lcd = Uni.Lcd;
  const int x = bodyX();
  const int y = bodyY();
  const int w = bodyW();
  const int h = bodyH();

  lcd.fillRect(x, y, w, h, TFT_BLACK);
  lcd.setTextFont(1);
  lcd.setTextSize(1);
  lcd.setTextDatum(MC_DATUM);
  lcd.setTextColor(TFT_LIGHTGREY, TFT_BLACK);

  String msg = "Connecting to " + _host + ":" + String(_port) + "...";
  lcd.drawString(msg.c_str(), x + w / 2, y + h / 2);
  lcd.setTextDatum(TL_DATUM);
}

void SshClientScreen::_acceptHostKey(bool trust) {
  _hostKeyDecision = trust ? 1 : -1;
  // Leave WAIT_HOSTKEY immediately on the UI side. Otherwise onUpdate() can
  // re-open the Trust prompt before the worker observes the decision.
  _workerState = WORKER_CONNECTING;

  if (trust) {
    _state = STATE_CONNECTING;
    render();
  } else {
    _stopRequested = true;
    _state = STATE_CONNECTING;
    render();
  }
}

void SshClientScreen::_renderHostKeyConfirm() {
  auto& lcd = Uni.Lcd;
  const int x = bodyX();
  const int y = bodyY();
  const int w = bodyW();
  const int h = bodyH();

  lcd.fillRect(x, y, w, h, TFT_BLACK);
  lcd.setTextFont(1);
  lcd.setTextSize(1);
  lcd.setTextDatum(TL_DATUM);

  int cy = y + PAD;

  lcd.setTextColor(TFT_YELLOW, TFT_BLACK);
  lcd.drawString("SSH Host Key", x + PAD, cy);
  cy += 12;

  lcd.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
  String hostLine = _host + ":" + String(_port);
  lcd.drawString(hostLine.c_str(), x + PAD, cy);
  cy += 12;

  lcd.drawFastHLine(x + PAD, cy, w - PAD * 2, TFT_DARKGREY);
  cy += 5;

  String fingerprint;
  if (_ioMutex && xSemaphoreTake(_ioMutex, pdMS_TO_TICKS(20)) == pdTRUE) {
    fingerprint = _hostFingerprint;
    xSemaphoreGive(_ioMutex);
  }

  // 6 px per character with font 1. Wrap the SHA-256 fingerprint so the entire
  // server identity remains visible even on narrow screens.
  const int charsPerLine = max(8, (w - PAD * 2) / 6);
  int pos = 0;
  while (pos < (int)fingerprint.length() && cy < y + h - 34) {
    String line = fingerprint.substring(pos, min(pos + charsPerLine,
                                                 (int)fingerprint.length()));
    lcd.drawString(line.c_str(), x + PAD, cy);
    pos += charsPerLine;
    cy += 10;
  }

  const int buttonY = y + h - 27;
  const int buttonW = (w - PAD * 3) / 2;

  auto drawButton = [&](int bx, const char* label, bool selected) {
    uint16_t border = selected ? TFT_YELLOW : TFT_DARKGREY;
    uint16_t text   = selected ? TFT_WHITE  : TFT_LIGHTGREY;
    lcd.drawRoundRect(bx, buttonY, buttonW, 20, 3, border);
    lcd.setTextDatum(MC_DATUM);
    lcd.setTextColor(text, TFT_BLACK);
    lcd.drawString(label, bx + buttonW / 2, buttonY + 10);
    lcd.setTextDatum(TL_DATUM);
  };

  drawButton(x + PAD, "Trust", _hostKeySelection == 0);
  drawButton(x + PAD * 2 + buttonW, "Cancel", _hostKeySelection == 1);
}

void SshClientScreen::_renderOutput() {
  auto& lcd = Uni.Lcd;
  const int x = bodyX();
  const int y = bodyY();
  const int w = bodyW();
  const int h = bodyH();

  lcd.fillRect(x, y, w, h, TFT_BLACK);
  lcd.setTextFont(1);
  lcd.setTextSize(1);
  lcd.setTextDatum(TL_DATUM);

  int cy = y + PAD;

  lcd.setTextColor(TFT_GREEN, TFT_BLACK);
  String connected = "Connected to " + _host + ":" + String(_port);
  lcd.drawString(connected.c_str(), x + PAD, cy);
  cy += 11;

  lcd.drawFastHLine(x + PAD, cy, w - PAD * 2, TFT_DARKGREY);
  cy += 4;

  const int inputY = y + h - INPUT_H;
  const int outputBottom = inputY - 2;

  String display = _transcript;
  if (_partialLine.length() > 0) display += _partialLine;
  if (display.length() == 0 && !_remoteClosed) display = "Waiting for data...";

  _outputView.updateContent(display, _followOutput);
  _outputView.render(x, cy, w, max(1, outputBottom - cy));

  lcd.drawFastHLine(x + PAD, inputY - 2, w - PAD * 2, TFT_DARKGREY);
#ifdef DEVICE_HAS_KEYBOARD
  constexpr bool keyboardDevice = true;
#else
  constexpr bool keyboardDevice = false;
#endif
  _inputLine.render(x, inputY, w, INPUT_H, _remoteClosed, keyboardDevice);
}
