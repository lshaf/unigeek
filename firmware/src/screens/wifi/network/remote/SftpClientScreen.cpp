#include "SftpClientScreen.h"

#include "core/Device.h"
#include "core/INavigation.h"
#include "core/ScreenManager.h"
#include "ui/actions/InputTextAction.h"
#include "ui/actions/InputNumberAction.h"
#include "ui/actions/InputSelectAction.h"
#include "ui/actions/ShowStatusAction.h"
#include "ui/views/ProgressView.h"

#include <WiFi.h>
#include <FS.h>
#include <fcntl.h>
#include <libssh/libssh.h>
#include <libssh/sftp.h>

const char* SftpClientScreen::title() {
  switch (_state) {
    case STATE_CONFIG:
    case STATE_SELECT_AUTH:
    case STATE_SELECT_KEY:
    case STATE_CONNECTING:
    case STATE_HOSTKEY_CONFIRM:
      return "SFTP Client";
    case STATE_REMOTE_LOADING:
    case STATE_REMOTE_BROWSER:
      return _remoteMode == REMOTE_UPLOAD_DEST ? "SFTP Upload" : "SFTP Download";
    case STATE_LOCAL_BROWSER:
      return "SFTP Upload";
    case STATE_TRANSFER:
      return _transferIsUpload ? "SFTP Upload" : "SFTP Download";
    case STATE_ACTION:
    default:
      return "SFTP Client";
  }
}

SftpClientScreen::~SftpClientScreen() {
  _closeConnection(false);

  uint32_t start = millis();
  while (_sftpTask && millis() - start < 3000) delay(10);

  if (_mutex && !_sftpTask) {
    vSemaphoreDelete(_mutex);
    _mutex = nullptr;
  }
}

void SftpClientScreen::onInit() {
  if (!_mutex) _mutex = xSemaphoreCreateMutex();

  // A new SFTP session starts at connection configuration. The action hub is
  // shown only after the SSH/SFTP session is established successfully.
  _state = STATE_CONFIG;
  _updateLabels();
  _rebuildConfig();
}

void SftpClientScreen::onUpdate() {
  if (_state == STATE_HOSTKEY_CONFIRM) {
    if (!Uni.Nav->wasPressed()) return;
    auto dir = Uni.Nav->readDirection();

    if (dir == INavigation::DIR_BACK) {
      _acceptHostKey(false);
      return;
    }
    if (dir == INavigation::DIR_UP || dir == INavigation::DIR_DOWN ||
        dir == INavigation::DIR_LEFT || dir == INavigation::DIR_RIGHT) {
      _hostKeySelection = _hostKeySelection ? 0 : 1;
      render();
      return;
    }
    if (dir == INavigation::DIR_PRESS) {
      _acceptHostKey(_hostKeySelection == 0);
      return;
    }
    return;
  }

  if (_state == STATE_CONNECTING) {
    if (_workerState == WORKER_WAIT_HOSTKEY) {
      _hostKeySelection = 0;
      _state = STATE_HOSTKEY_CONFIRM;
      render();
      return;
    }

    if (_workerState == WORKER_READY) {
      // Keep the configured credential/session data available while the user
      // moves between Download and Upload. Sensitive worker-local copies are
      // still cleared by the worker immediately after authentication.
      _showActions();
      return;
    }

    if (_workerState == WORKER_FAILED || _workerState == WORKER_CLOSED) {
      String err = "SFTP connection failed";
      if (_mutex && xSemaphoreTake(_mutex, pdMS_TO_TICKS(20)) == pdTRUE) {
        if (_commandError.length()) err = _commandError;
        xSemaphoreGive(_mutex);
      }
      _password = "";
      _keyData = "";
      _workerState = WORKER_IDLE;
      _state = STATE_CONFIG;
      _updateLabels();
      _rebuildConfig();
      ShowStatusAction::show(err.c_str(), 1800);
      render();
      return;
    }

    if (Uni.Nav->wasPressed() &&
        Uni.Nav->readDirection() == INavigation::DIR_BACK) {
      _stopRequested = true;
      _hostKeyDecision = -1;
    }
    return;
  }

  if (_state == STATE_REMOTE_LOADING) {
    if (_workerState == WORKER_FAILED || _workerState == WORKER_CLOSED) {
      String err = "SFTP list failed";
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

      const bool upload = _transferIsUpload;
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

void SftpClientScreen::onRender() {
  if (_state == STATE_CONNECTING || _state == STATE_REMOTE_LOADING) {
    _renderConnecting();
    return;
  }
  if (_state == STATE_HOSTKEY_CONFIRM) {
    _renderHostKeyConfirm();
    return;
  }
  if (_state == STATE_TRANSFER) {
    // ProgressView owns the body while a transfer is running.
    return;
  }
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

void SftpClientScreen::onItemSelected(uint8_t index) {
  if (_state == STATE_ACTION) {
    if (index == 0) {
      _remoteMode = REMOTE_DOWNLOAD;
      if (_workerState == WORKER_READY) _requestList(".");
      else                              _beginDownload();
    } else if (index == 1) {
      _beginUpload();
    }
    return;
  }

  if (_state == STATE_SELECT_AUTH) {
    _selectAuth(index);
    return;
  }

  if (_state == STATE_SELECT_KEY) {
    _selectKey(index);
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
    case 0: _configHost();     break;
    case 1: _configPort();     break;
    case 2: _configUsername(); break;
    case 3: _selectAuthMenu(); break;
    case 4:
      if (_authMode == AUTH_PASSWORD) _configPassword();
      else if (_authMode == AUTH_PRIVATE_KEY) _openKeyPicker();
      else _connect();
      break;
    case 5: _connect(); break;
    default: break;
  }
}

void SftpClientScreen::onBack() {
  if (_state == STATE_ACTION) {
    // Leaving the Download/Upload hub ends the SFTP session.
    _closeConnection(false);
    Screen.goBack();
    return;
  }

  if (_state == STATE_HOSTKEY_CONFIRM) {
    _acceptHostKey(false);
    return;
  }

  if (_state == STATE_CONNECTING || _state == STATE_REMOTE_LOADING) {
    _stopRequested = true;
    _hostKeyDecision = -1;
    return;
  }

  if (_state == STATE_TRANSFER) {
    _cancelTransfer = true;
    return;
  }

  if (_state == STATE_SELECT_AUTH || _state == STATE_SELECT_KEY) {
    _state = STATE_CONFIG;
    _updateLabels();
    _rebuildConfig();
    render();
    return;
  }

  if (_state == STATE_REMOTE_BROWSER) {
    if (_remoteMode == REMOTE_UPLOAD_DEST) {
      // Cancel destination selection and return to the local upload browser.
      _state = STATE_LOCAL_BROWSER;
      render();
    } else {
      // BACK from Download returns to the action hub while keeping the session.
      _showActions();
    }
    return;
  }

  if (_state == STATE_LOCAL_BROWSER) {
    _showActions();
    return;
  }

  // Before a connection exists, BACK from CONFIG returns to the parent menu.
  Screen.goBack();
}

void SftpClientScreen::_showActions() {
  _state = STATE_ACTION;
  setItems(_actionItems, 2, 0);
  render();
}

void SftpClientScreen::_beginDownload() {
  _remoteMode = REMOTE_DOWNLOAD;

  // Used only when no live SFTP session exists yet.
  _state = STATE_CONFIG;
  _updateLabels();
  _rebuildConfig();
  render();
}

void SftpClientScreen::_beginUpload() {
  if (_workerState != WORKER_READY) {
    _state = STATE_CONFIG;
    _updateLabels();
    _rebuildConfig();
    render();
    return;
  }

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

void SftpClientScreen::_updateLabels() {
  snprintf(_hostLabel, sizeof(_hostLabel), "%s",
           _host.length() ? _host.c_str() : "-");
  snprintf(_userLabel, sizeof(_userLabel), "%s",
           _username.length() ? _username.c_str() : "-");
  snprintf(_portLabel, sizeof(_portLabel), "%d", _port);

  if (_authMode == AUTH_PASSWORD)
    snprintf(_authLabel, sizeof(_authLabel), "Password");
  else if (_authMode == AUTH_PRIVATE_KEY)
    snprintf(_authLabel, sizeof(_authLabel), "Private Key");
  else
    snprintf(_authLabel, sizeof(_authLabel), "-");

  snprintf(_passwordLabel, sizeof(_passwordLabel), "%s",
           _password.length() ? "********" : "-");

  if (_keyPath.length()) {
    String name = _baseName(_keyPath);
    name.toCharArray(_keyLabel, sizeof(_keyLabel));
  } else {
    snprintf(_keyLabel, sizeof(_keyLabel), "-");
  }
}

void SftpClientScreen::_rebuildConfig() {
  uint8_t sel = _selectedIndex;
  _configItems[0] = {"Host", _hostLabel};
  _configItems[1] = {"Port", _portLabel};
  _configItems[2] = {"Username", _userLabel};
  _configItems[3] = {"Auth Method", _authLabel};

  if (_authMode == AUTH_PASSWORD) {
    _configItems[4] = {"Password", _passwordLabel};
    _configItems[5] = {"Connect", nullptr};
    if (sel > 5) sel = 5;
    setItems(_configItems, 6, sel);
  } else if (_authMode == AUTH_PRIVATE_KEY) {
    _configItems[4] = {"Key File", _keyLabel};
    _configItems[5] = {"Connect", nullptr};
    if (sel > 5) sel = 5;
    setItems(_configItems, 6, sel);
  } else {
    _configItems[4] = {"Connect", nullptr};
    if (sel > 4) sel = 4;
    setItems(_configItems, 5, sel);
  }
}

void SftpClientScreen::_configHost() {
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

void SftpClientScreen::_configPort() {
  int value = InputNumberAction::popup("Port", 1, 65535, _port);
  if (!InputNumberAction::wasCancelled()) _port = value;
  _updateLabels();
  _rebuildConfig();
  render();
}

void SftpClientScreen::_configUsername() {
  String value = InputTextAction::popup(
    "Username", _username, InputTextAction::INPUT_TEXT);
  if (!InputTextAction::wasCancelled() && value.length()) _username = value;
  _updateLabels();
  _rebuildConfig();
  render();
}

void SftpClientScreen::_configPassword() {
  String value = InputTextAction::popup(
    "Password", "", InputTextAction::INPUT_TEXT);
  if (!InputTextAction::wasCancelled()) _password = value;
  _selectedIndex = 4;
  _updateLabels();
  _rebuildConfig();
  render();
}

void SftpClientScreen::_selectAuthMenu() {
  _state = STATE_SELECT_AUTH;
  uint8_t selected = (_authMode == AUTH_PRIVATE_KEY) ? 1 : 0;
  setItems(_authItems, 2, selected);
  render();
}

void SftpClientScreen::_selectAuth(uint8_t index) {
  if (index > 1) return;

  _authMode = index == 0 ? AUTH_PASSWORD : AUTH_PRIVATE_KEY;
  if (_authMode == AUTH_PASSWORD) _keyData = "";
  else _password = "";

  _selectedIndex = 3;
  _state = STATE_CONFIG;
  _updateLabels();
  _rebuildConfig();
  render();
}

void SftpClientScreen::_openKeyPicker() {
  if (!Uni.Storage || !Uni.Storage->isAvailable()) {
    ShowStatusAction::show("Storage not available", 1500);
    render();
    return;
  }

  Uni.Storage->makeDir("/unigeek");
  Uni.Storage->makeDir("/unigeek/wifi");
  Uni.Storage->makeDir("/unigeek/wifi/remote");
  Uni.Storage->makeDir("/unigeek/wifi/remote/ssh");

  _localBrowsePath = "/unigeek/wifi/remote/ssh";
  _localBrowser.root = "/";
  _state = STATE_SELECT_KEY;
  _loadKeyDir(_localBrowsePath);
}

void SftpClientScreen::_loadKeyDir(const String& path) {
  _localBrowsePath = path;
  uint8_t n = _localBrowser.load(this, path, {}, "KEY");
  setItems(_localBrowser.items(), n);
  render();
}

void SftpClientScreen::_selectKey(uint8_t index) {
  if (index >= _localBrowser.count()) return;
  const auto& e = _localBrowser.entry(index);

  if (e.isDir) {
    _loadKeyDir(e.path);
    return;
  }

  _keyPath = e.path;
  _selectedIndex = 4;
  _state = STATE_CONFIG;
  _updateLabels();
  _rebuildConfig();
  render();
}


void SftpClientScreen::_loadLocalUploadDir(const String& path) {
  _localBrowsePath = path;
  uint8_t n = _localBrowser.load(this, path, {}, "SD");
  _state = STATE_LOCAL_BROWSER;
  setItems(_localBrowser.items(), n);
  render();
}

void SftpClientScreen::_selectLocalUpload(uint8_t index) {
  if (index >= _localBrowser.count()) return;
  const auto& e = _localBrowser.entry(index);

  if (e.isDir) {
    _loadLocalUploadDir(e.path);
    return;
  }

  _chooseUploadSource(e.path, e.name, false);
}

void SftpClientScreen::_holdLocalUpload(uint8_t index) {
  if (index >= _localBrowser.count()) return;
  const auto& e = _localBrowser.entry(index);
  if (!e.isDir || e.name == "..") return;

  _chooseUploadSource(e.path, e.name, true);
}

void SftpClientScreen::_chooseUploadSource(const String& path,
                                           const String& name,
                                           bool isDir) {
  _uploadLocalPath = path;
  _uploadName = name;
  _uploadIsDir = isDir;
  _chooseUploadDestinationMode();
}

void SftpClientScreen::_chooseUploadDestinationMode() {
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
    // Relative SFTP paths resolve from the connected user's home directory.
    _chooseUploadDestination("Downloads");
    return;
  }

  _remoteMode = REMOTE_UPLOAD_DEST;
  _requestList(".");
}

void SftpClientScreen::_connect() {
  if (!_host.length()) {
    ShowStatusAction::show("Host required", 1200); render(); return;
  }
  if (!_username.length()) {
    ShowStatusAction::show("Username required", 1200); render(); return;
  }
  if (WiFi.status() != WL_CONNECTED) {
    ShowStatusAction::show("WiFi not connected", 1500); render(); return;
  }
  if (_authMode == AUTH_NONE) {
    ShowStatusAction::show("Auth method required", 1500); render(); return;
  }

  if (_authMode == AUTH_PASSWORD) {
    if (!_password.length()) {
      ShowStatusAction::show("Password required", 1200); render(); return;
    }
  } else {
    if (!_keyPath.length()) {
      ShowStatusAction::show("Key file required", 1200); render(); return;
    }
    if (!Uni.Storage || !Uni.Storage->isAvailable()) {
      ShowStatusAction::show("Storage not available", 1500); render(); return;
    }
    _keyData = Uni.Storage->readFile(_keyPath.c_str());
    if (!_keyData.length()) {
      ShowStatusAction::show("Key read failed", 1500); render(); return;
    }
  }

  if (!_mutex) _mutex = xSemaphoreCreateMutex();
  if (!_mutex || !_startWorker()) {
    _keyData = "";
    ShowStatusAction::show("SFTP task failed", 1500);
    render();
    return;
  }

  _state = STATE_CONNECTING;
  render();
}

bool SftpClientScreen::_startWorker() {
  if (_sftpTask) return false;

  _stopRequested = false;
  _cancelTransfer = false;
  _hostKeyDecision = 0;
  _workerState = WORKER_CONNECTING;
  _command = CMD_NONE;
  _commandDone = false;

  if (_mutex && xSemaphoreTake(_mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
    _commandError = "";
    _hostFingerprint = "";
    xSemaphoreGive(_mutex);
  }

  BaseType_t ok;
#if defined(SOC_CPU_CORES_NUM) && SOC_CPU_CORES_NUM > 1
  ok = xTaskCreatePinnedToCore(
    _workerEntry, "UG SFTP", SFTP_TASK_STACK_SIZE, this, 1, &_sftpTask, 1);
#else
  ok = xTaskCreate(
    _workerEntry, "UG SFTP", SFTP_TASK_STACK_SIZE, this, 1, &_sftpTask);
#endif

  if (ok != pdPASS) {
    _sftpTask = nullptr;
    _workerState = WORKER_IDLE;
    return false;
  }
  return true;
}

void SftpClientScreen::_workerEntry(void* arg) {
  static_cast<SftpClientScreen*>(arg)->_worker();
}

void SftpClientScreen::_setWorkerError(const char* message) {
  if (_mutex && xSemaphoreTake(_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
    _commandError = message ? message : "SFTP failed";
    xSemaphoreGive(_mutex);
  }
  _workerState = WORKER_FAILED;
}

void SftpClientScreen::_worker() {
  ssh_session session = nullptr;
  sftp_session sftp = nullptr;

  String host = _host;
  String user = _username;
  String password = _password;
  String keyData = _keyData;
  AuthMode authMode = _authMode;
  int port = _port;

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

  // Fingerprint confirmation occurs before any user credential is sent.
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

    if (_mutex && xSemaphoreTake(_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
      _hostFingerprint = "SHA256 ";
      _hostFingerprint += hex;
      xSemaphoreGive(_mutex);
    }
    ssh_string_free_char(hex);

    _hostKeyDecision = 0;
    _workerState = WORKER_WAIT_HOSTKEY;
    while (!_stopRequested && _hostKeyDecision == 0)
      vTaskDelay(pdMS_TO_TICKS(20));

    if (_stopRequested || _hostKeyDecision < 0) {
      _workerState = WORKER_CLOSED;
      goto cleanup;
    }
    _workerState = WORKER_CONNECTING;
  }

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
    if (rc == SSH_AUTH_SUCCESS)
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

  sftp = sftp_new(session);
  if (!sftp || sftp_init(sftp) != SSH_OK) {
    _setWorkerError("SFTP init failed");
    goto cleanup;
  }

  _workerState = WORKER_READY;

  while (!_stopRequested) {
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
      ok = _workerList((void*)sftp, path);
    } else if (cmd == CMD_DOWNLOAD_FILE) {
      String local = String(DOWNLOAD_DIR) + "/" + _baseName(path);
      sftp_attributes a = sftp_stat(sftp, path.c_str());
      uint64_t size = a ? a->size : 0;
      if (a) sftp_attributes_free(a);
      ok = _workerDownloadFile((void*)sftp, path, local, size);
    } else if (cmd == CMD_DOWNLOAD_DIR) {
      String local = String(DOWNLOAD_DIR) + "/" + _baseName(path);
      ok = _workerDownloadDir((void*)sftp, path, local);
    } else if (cmd == CMD_UPLOAD_FILE) {
      if (!_workerEnsureRemoteDir((void*)sftp, path2)) {
        if (_mutex && xSemaphoreTake(_mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
          _commandError = "Remote directory not found";
          xSemaphoreGive(_mutex);
        }
        ok = false;
      } else {
        String remote = path2;
        if (!remote.endsWith("/")) remote += "/";
        remote += _baseName(path);
        fs::File f = Uni.Storage->open(path.c_str(), "r");
        uint64_t size = f ? (uint64_t)f.size() : 0;
        if (f) f.close();
        ok = _workerUploadFile((void*)sftp, path, remote, size);
      }
    } else if (cmd == CMD_UPLOAD_DIR) {
      if (!_workerEnsureRemoteDir((void*)sftp, path2)) {
        if (_mutex && xSemaphoreTake(_mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
          _commandError = "Remote directory not found";
          xSemaphoreGive(_mutex);
        }
        ok = false;
      } else {
        String remote = path2;
        if (!remote.endsWith("/")) remote += "/";
        remote += _baseName(path);
        ok = _workerUploadDir((void*)sftp, path, remote);
      }
    }

    if (_cancelTransfer && cmd != CMD_LIST) {
      ok = false;
      if (_mutex && xSemaphoreTake(_mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        _commandError = "Cancelled";
        xSemaphoreGive(_mutex);
      }
    }

    _commandOk = ok;
    _commandDone = true;
    _workerState = WORKER_READY;
  }

  _workerState = WORKER_CLOSED;

cleanup:
  if (sftp) {
    sftp_free(sftp);
    sftp = nullptr;
  }

  if (session) {
    ssh_disconnect(session);
    ssh_free(session);
    session = nullptr;
  }

  password = "";
  keyData = "";
  _sftpTask = nullptr;
  vTaskDelete(nullptr);
}

void SftpClientScreen::_closeConnection(bool returnToActions) {
  _stopRequested = true;
  _cancelTransfer = true;
  _hostKeyDecision = -1;

  uint32_t start = millis();
  while (_sftpTask && millis() - start < 800) delay(5);

  _password = "";
  _keyData = "";

  if (returnToActions) _showActions();
}

void SftpClientScreen::_requestList(const String& path) {
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

bool SftpClientScreen::_workerList(void* sftpOpaque, const String& path) {
  sftp_session sftp = (sftp_session)sftpOpaque;
  sftp_dir dir = sftp_opendir(sftp, path.c_str());
  if (!dir) {
    if (_mutex && xSemaphoreTake(_mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
      _commandError = "Cannot open remote dir";
      xSemaphoreGive(_mutex);
    }
    return false;
  }

  SharedRemoteEntry temp[MAX_REMOTE_ENTRIES];
  uint8_t count = 0;

  while (!_stopRequested && count < MAX_REMOTE_ENTRIES) {
    sftp_attributes a = sftp_readdir(sftp, dir);
    if (!a) break;

    String name = a->name ? a->name : "";
    if (name != "." && name != ".." && name.length()) {
      String child;
      if (path == "/") child = "/" + name;
      else if (path == ".") child = "./" + name;
      else child = path + "/" + name;

      temp[count].name = name;
      temp[count].path = child;
      temp[count].isDir = (a->type == SSH_FILEXFER_TYPE_DIRECTORY);
      temp[count].size = a->size;
      count++;
    }
    sftp_attributes_free(a);
  }

  sftp_closedir(dir);

  // Directories first, then case-insensitive alphabetical order.
  for (uint8_t i = 1; i < count; ++i) {
    SharedRemoteEntry key = temp[i];
    int j = i - 1;
    while (j >= 0) {
      bool move = false;
      if (temp[j].isDir != key.isDir)
        move = !temp[j].isDir && key.isDir;
      else
        move = strcasecmp(temp[j].name.c_str(), key.name.c_str()) > 0;
      if (!move) break;
      temp[j + 1] = temp[j];
      --j;
    }
    temp[j + 1] = key;
  }

  if (_mutex && xSemaphoreTake(_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
    _sharedEntryCount = count;
    for (uint8_t i = 0; i < count; ++i) _sharedEntries[i] = temp[i];
    xSemaphoreGive(_mutex);
  }
  return true;
}

void SftpClientScreen::_copyRemoteListing() {
  if (!_mutex) return;

  SharedRemoteEntry temp[MAX_REMOTE_ENTRIES];
  uint8_t count = 0;

  if (xSemaphoreTake(_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
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
    } else {
      uint64_t size = temp[i].size;
      if (size >= 1024 * 1024)
        _remoteSubs[out] = String((double)size / (1024.0 * 1024.0), 1) + " MB";
      else if (size >= 1024)
        _remoteSubs[out] = String((double)size / 1024.0, 1) + " KB";
      else
        _remoteSubs[out] = String((unsigned long)size) + " B";
    }

    _remoteItems[out] = {
      _remoteNames[out].c_str(),
      _remoteSubs[out].c_str()
    };
  }

  _remoteCount = out;
  setItems(_remoteItems, _remoteCount, 0);
}

void SftpClientScreen::_selectRemote(uint8_t index) {
  if (index >= _remoteCount) return;

  if (_remoteMode == REMOTE_UPLOAD_DEST) {
    // "." is the synthetic UPLOAD HERE action for the current directory.
    // PRESS executes it directly; PRESS on a real directory still navigates.
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

  if (!_confirmTransfer("Download file?", _remoteNames[index])) {
    render();
    return;
  }

  _requestDownload(_remotePaths[index], false);
}

void SftpClientScreen::_holdRemote(uint8_t index) {
  if (index >= _remoteCount || !_remoteIsDir[index]) return;

  if (_remoteMode == REMOTE_UPLOAD_DEST) {
    if (_remoteNames[index] == "..") return;
    _chooseUploadDestination(_remotePaths[index]);
    return;
  }

  if (_remoteNames[index] == "..") return;

  if (!_confirmTransfer("Download directory?", _remoteNames[index])) {
    render();
    return;
  }

  _requestDownload(_remotePaths[index], true);
}

void SftpClientScreen::_chooseUploadDestination(const String& remoteDir) {
  if (!_confirmTransfer(_uploadIsDir ? "Upload directory?" : "Upload file?",
                        _uploadName)) {
    render();
    return;
  }

  _requestUpload(remoteDir);
}

bool SftpClientScreen::_confirmTransfer(const char* title, const String& name) {
  (void)name;

  InputSelectAction::Option opts[] = {
    {"Yes", "yes"},
    {"No", "no"},
  };

  // InputSelectAction renders its title as a single-line overlay heading.
  // Do not embed the selected filename/directory with a newline here; doing so
  // produces visual artifacts on the UG display.
  // File-browser screens may leave a different LCD font selected. Normalize it
  // before opening the popup so its title/options render consistently.
  Uni.Lcd.setTextFont(1);
  Uni.Lcd.setTextSize(1);
  Uni.Lcd.setTextDatum(TL_DATUM);

  const char* result = InputSelectAction::popup(
    title, opts, 2, "yes");
  return result && strcmp(result, "yes") == 0;
}

void SftpClientScreen::_requestDownload(const String& remotePath, bool directory) {
  _transferIsUpload = false;
  if (!Uni.Storage || !Uni.Storage->isAvailable()) {
    ShowStatusAction::show("Storage not available", 1500);
    render();
    return;
  }

  Uni.Storage->makeDir("/unigeek");
  Uni.Storage->makeDir("/unigeek/wifi");
  Uni.Storage->makeDir("/unigeek/wifi/remote");
  Uni.Storage->makeDir("/unigeek/wifi/remote/sftp");
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

  _cancelTransfer = false;
  _commandDone = false;
  _commandOk = false;
  _command = directory ? CMD_DOWNLOAD_DIR : CMD_DOWNLOAD_FILE;
  _state = STATE_TRANSFER;

  ProgressView::init();
  ProgressView::progress(
    directory ? "Preparing directory..." : _progressName.c_str(), 0);
}


void SftpClientScreen::_requestUpload(const String& remoteDir) {
  if (!_uploadLocalPath.length()) {
    ShowStatusAction::show("Upload source missing", 1200);
    render();
    return;
  }

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

void SftpClientScreen::_updateTransferProgress() {
  if (!_mutex) return;

  uint64_t done = 0, total = 0;
  uint32_t filesDone = 0, filesTotal = 0;
  String name;

  if (xSemaphoreTake(_mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
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
    msg = name.length() ? name : "Downloading...";

  ProgressView::progress(msg.c_str(), pct);
}

bool SftpClientScreen::_workerDownloadFile(void* sftpOpaque,
                                           const String& remotePath,
                                           const String& localPath,
                                           uint64_t knownSize) {
  sftp_session sftp = (sftp_session)sftpOpaque;
  sftp_file remote = sftp_open(sftp, remotePath.c_str(), O_RDONLY, 0);
  if (!remote) {
    if (_mutex && xSemaphoreTake(_mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
      _commandError = "Remote file open failed";
      xSemaphoreGive(_mutex);
    }
    return false;
  }

  fs::File local = Uni.Storage->open(localPath.c_str(), "w");
  if (!local) {
    sftp_close(remote);
    if (_mutex && xSemaphoreTake(_mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
      _commandError = "Local file open failed";
      xSemaphoreGive(_mutex);
    }
    return false;
  }

  if (_mutex && xSemaphoreTake(_mutex, pdMS_TO_TICKS(20)) == pdTRUE) {
    if (_progressTotal == 0) _progressTotal = knownSize;
    _progressName = _baseName(remotePath);
    xSemaphoreGive(_mutex);
  }

  uint8_t buf[4096];
  bool ok = true;
  uint64_t received = 0;

  while (!_stopRequested && !_cancelTransfer) {
    ssize_t n = sftp_read(remote, buf, sizeof(buf));
    if (n < 0) {
      ok = false;
      if (_mutex && xSemaphoreTake(_mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        _commandError = "SFTP read failed";
        xSemaphoreGive(_mutex);
      }
      break;
    }
    if (n == 0) break;

    size_t written = local.write(buf, (size_t)n);
    if (written != (size_t)n) {
      ok = false;
      if (_mutex && xSemaphoreTake(_mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        _commandError = "Storage write failed";
        xSemaphoreGive(_mutex);
      }
      break;
    }

    received += written;

    if (_mutex && xSemaphoreTake(_mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
      _progressDone += written;
      xSemaphoreGive(_mutex);
    }
  }

  local.close();
  sftp_close(remote);

  if (_cancelTransfer || _stopRequested) ok = false;

  if (ok && knownSize && received != knownSize) {
    ok = false;
    if (_mutex && xSemaphoreTake(_mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
      _commandError = "Incomplete download";
      xSemaphoreGive(_mutex);
    }
  }

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

bool SftpClientScreen::_workerCalcDir(void* sftpOpaque,
                                      const String& remotePath,
                                      uint64_t& bytes,
                                      uint32_t& files,
                                      int depth) {
  if (depth > 12 || _cancelTransfer || _stopRequested) return false;

  sftp_session sftp = (sftp_session)sftpOpaque;
  sftp_dir dir = sftp_opendir(sftp, remotePath.c_str());
  if (!dir) return false;

  bool ok = true;
  while (!_cancelTransfer && !_stopRequested) {
    sftp_attributes a = sftp_readdir(sftp, dir);
    if (!a) break;

    String name = a->name ? a->name : "";
    if (name != "." && name != "..") {
      String child = remotePath == "/" ? "/" + name : remotePath + "/" + name;
      if (a->type == SSH_FILEXFER_TYPE_DIRECTORY) {
        if (!_workerCalcDir(sftpOpaque, child, bytes, files, depth + 1)) {
          ok = false;
          sftp_attributes_free(a);
          break;
        }
      } else if (a->type == SSH_FILEXFER_TYPE_REGULAR) {
        bytes += a->size;
        files++;
      }
    }
    sftp_attributes_free(a);
  }

  sftp_closedir(dir);
  return ok && !_cancelTransfer && !_stopRequested;
}

bool SftpClientScreen::_workerDownloadTree(void* sftpOpaque,
                                           const String& remotePath,
                                           const String& localPath,
                                           int depth) {
  if (depth > 12 || _cancelTransfer || _stopRequested) return false;

  Uni.Storage->makeDir(localPath.c_str());

  sftp_session sftp = (sftp_session)sftpOpaque;
  sftp_dir dir = sftp_opendir(sftp, remotePath.c_str());
  if (!dir) return false;

  bool ok = true;
  while (!_cancelTransfer && !_stopRequested) {
    sftp_attributes a = sftp_readdir(sftp, dir);
    if (!a) break;

    String name = a->name ? a->name : "";
    if (name != "." && name != "..") {
      String remoteChild =
        remotePath == "/" ? "/" + name : remotePath + "/" + name;
      String localChild = localPath + "/" + name;

      if (a->type == SSH_FILEXFER_TYPE_DIRECTORY) {
        if (!_workerDownloadTree(
              sftpOpaque, remoteChild, localChild, depth + 1)) {
          ok = false;
          sftp_attributes_free(a);
          break;
        }
      } else if (a->type == SSH_FILEXFER_TYPE_REGULAR) {
        if (!_workerDownloadFile(
              sftpOpaque, remoteChild, localChild, a->size)) {
          ok = false;
          sftp_attributes_free(a);
          break;
        }
      }
    }
    sftp_attributes_free(a);
  }

  sftp_closedir(dir);
  return ok && !_cancelTransfer && !_stopRequested;
}

bool SftpClientScreen::_workerDownloadDir(void* sftpOpaque,
                                          const String& remotePath,
                                          const String& localPath) {
  uint64_t totalBytes = 0;
  uint32_t totalFiles = 0;

  if (!_workerCalcDir(
        sftpOpaque, remotePath, totalBytes, totalFiles, 0)) {
    if (!_cancelTransfer && _mutex &&
        xSemaphoreTake(_mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
      _commandError = "Directory scan failed";
      xSemaphoreGive(_mutex);
    }
    return false;
  }

  if (_mutex && xSemaphoreTake(_mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
    _progressTotal = totalBytes;
    _progressFilesTotal = totalFiles;
    _progressDone = 0;
    _progressFilesDone = 0;
    _progressName = _baseName(remotePath);
    xSemaphoreGive(_mutex);
  }

  return _workerDownloadTree(sftpOpaque, remotePath, localPath, 0);
}


bool SftpClientScreen::_workerEnsureRemoteDir(void* sftpOpaque,
                                               const String& remotePath) {
  sftp_session sftp = (sftp_session)sftpOpaque;
  sftp_attributes a = sftp_stat(sftp, remotePath.c_str());
  if (a) {
    bool isDir = (a->type == SSH_FILEXFER_TYPE_DIRECTORY);
    sftp_attributes_free(a);
    return isDir;
  }

  if (sftp_mkdir(sftp, remotePath.c_str(), 0755) == SSH_OK) return true;

  // Some servers return failure when the path appeared concurrently. Recheck.
  a = sftp_stat(sftp, remotePath.c_str());
  if (!a) return false;
  bool isDir = (a->type == SSH_FILEXFER_TYPE_DIRECTORY);
  sftp_attributes_free(a);
  return isDir;
}

bool SftpClientScreen::_workerUploadFile(void* sftpOpaque,
                                         const String& localPath,
                                         const String& remotePath,
                                         uint64_t knownSize) {
  sftp_session sftp = (sftp_session)sftpOpaque;

  fs::File local = Uni.Storage->open(localPath.c_str(), "r");
  if (!local) {
    if (_mutex && xSemaphoreTake(_mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
      _commandError = "Local file open failed";
      xSemaphoreGive(_mutex);
    }
    return false;
  }

  sftp_file remote = sftp_open(
    sftp, remotePath.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
  if (!remote) {
    local.close();
    if (_mutex && xSemaphoreTake(_mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
      _commandError = "Remote file open failed";
      xSemaphoreGive(_mutex);
    }
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

  while (!_stopRequested && !_cancelTransfer) {
    size_t n = local.read(buf, sizeof(buf));
    if (n == 0) break;

    size_t sent = 0;
    while (sent < n && !_stopRequested && !_cancelTransfer) {
      ssize_t w = sftp_write(remote, buf + sent, n - sent);

      if (w < 0) {
        ok = false;
        if (_mutex && xSemaphoreTake(_mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
          _commandError = "SFTP write failed";
          xSemaphoreGive(_mutex);
        }
        break;
      }

      if (w == 0) {
        if (millis() - lastProgress >= WRITE_STALL_TIMEOUT_MS) {
          ok = false;
          if (_mutex && xSemaphoreTake(_mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
            _commandError = "SFTP write timed out";
            xSemaphoreGive(_mutex);
          }
          break;
        }
        vTaskDelay(pdMS_TO_TICKS(2));
        continue;
      }

      sent += (size_t)w;
      sentTotal += (uint64_t)w;
      lastProgress = millis();

      if (_mutex && xSemaphoreTake(_mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
        _progressDone += (uint64_t)w;
        xSemaphoreGive(_mutex);
      }
    }

    if (!ok) break;
  }

  sftp_close(remote);
  local.close();

  if (_cancelTransfer || _stopRequested) return false;

  if (ok && knownSize && sentTotal != knownSize) {
    ok = false;
    if (_mutex && xSemaphoreTake(_mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
      _commandError = "Incomplete upload";
      xSemaphoreGive(_mutex);
    }
  }

  if (!ok) return false;

  if (_mutex && xSemaphoreTake(_mutex, pdMS_TO_TICKS(20)) == pdTRUE) {
    _progressFilesDone++;
    xSemaphoreGive(_mutex);
  }
  return true;
}

bool SftpClientScreen::_workerCalcLocalDir(const String& localPath,
                                            uint64_t& bytes,
                                            uint32_t& files,
                                            int depth) {
  if (depth > 12 || _cancelTransfer || _stopRequested) return false;

  fs::File dir = Uni.Storage->open(localPath.c_str(), "r");
  if (!dir || !dir.isDirectory()) {
    if (dir) dir.close();
    return false;
  }

  bool ok = true;
  fs::File child = dir.openNextFile();
  while (child && !_cancelTransfer && !_stopRequested) {
    String name = _baseName(String(child.name()));
    String childPath = localPath;
    if (!childPath.endsWith("/")) childPath += "/";
    childPath += name;

    if (child.isDirectory()) {
      child.close();
      if (!_workerCalcLocalDir(childPath, bytes, files, depth + 1)) {
        ok = false;
        break;
      }
    } else {
      bytes += (uint64_t)child.size();
      files++;
      child.close();
    }
    child = dir.openNextFile();
  }
  if (child) child.close();
  dir.close();

  return ok && !_cancelTransfer && !_stopRequested;
}

bool SftpClientScreen::_workerUploadTree(void* sftpOpaque,
                                         const String& localPath,
                                         const String& remotePath,
                                         int depth) {
  if (depth > 12 || _cancelTransfer || _stopRequested) return false;
  if (!_workerEnsureRemoteDir(sftpOpaque, remotePath)) {
    if (_mutex && xSemaphoreTake(_mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
      _commandError = "Remote mkdir failed";
      xSemaphoreGive(_mutex);
    }
    return false;
  }

  fs::File dir = Uni.Storage->open(localPath.c_str(), "r");
  if (!dir || !dir.isDirectory()) {
    if (dir) dir.close();
    return false;
  }

  bool ok = true;
  fs::File child = dir.openNextFile();
  while (child && !_cancelTransfer && !_stopRequested) {
    String name = _baseName(String(child.name()));
    String localChild = localPath;
    if (!localChild.endsWith("/")) localChild += "/";
    localChild += name;

    String remoteChild = remotePath;
    if (!remoteChild.endsWith("/")) remoteChild += "/";
    remoteChild += name;

    if (child.isDirectory()) {
      child.close();
      if (!_workerUploadTree(
            sftpOpaque, localChild, remoteChild, depth + 1)) {
        ok = false;
        break;
      }
    } else {
      uint64_t size = (uint64_t)child.size();
      child.close();
      if (!_workerUploadFile(
            sftpOpaque, localChild, remoteChild, size)) {
        ok = false;
        break;
      }
    }
    child = dir.openNextFile();
  }
  if (child) child.close();
  dir.close();

  return ok && !_cancelTransfer && !_stopRequested;
}

bool SftpClientScreen::_workerUploadDir(void* sftpOpaque,
                                        const String& localPath,
                                        const String& remotePath) {
  uint64_t totalBytes = 0;
  uint32_t totalFiles = 0;

  if (!_workerCalcLocalDir(localPath, totalBytes, totalFiles, 0)) {
    if (!_cancelTransfer && _mutex &&
        xSemaphoreTake(_mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
      _commandError = "Directory scan failed";
      xSemaphoreGive(_mutex);
    }
    return false;
  }

  if (_mutex && xSemaphoreTake(_mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
    _progressTotal = totalBytes;
    _progressFilesTotal = totalFiles;
    _progressDone = 0;
    _progressFilesDone = 0;
    _progressName = _baseName(localPath);
    xSemaphoreGive(_mutex);
  }

  return _workerUploadTree(sftpOpaque, localPath, remotePath, 0);
}

String SftpClientScreen::_parentRemotePath(const String& path) const {
  if (path == "." || path.length() == 0) return ".";
  int slash = path.lastIndexOf('/');
  if (slash < 0) return ".";
  if (slash == 0) return "/";
  String parent = path.substring(0, slash);
  return parent.length() ? parent : ".";
}

String SftpClientScreen::_baseName(const String& path) const {
  String clean = path;
  while (clean.length() > 1 && clean.endsWith("/"))
    clean.remove(clean.length() - 1);
  int slash = clean.lastIndexOf('/');
  return slash >= 0 ? clean.substring(slash + 1) : clean;
}

void SftpClientScreen::_acceptHostKey(bool trust) {
  _hostKeyDecision = trust ? 1 : -1;
  // Leave WAIT_HOSTKEY immediately on the UI side. Otherwise onUpdate() can
  // re-open the Trust prompt before the worker observes the decision.
  _workerState = WORKER_CONNECTING;
  if (trust) {
    _state = STATE_CONNECTING;
  } else {
    _stopRequested = true;
    _state = STATE_CONNECTING;
  }
  render();
}

void SftpClientScreen::_renderConnecting() {
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

void SftpClientScreen::_renderHostKeyConfirm() {
  auto& lcd = Uni.Lcd;
  int x = bodyX(), y = bodyY(), w = bodyW(), h = bodyH();
  lcd.fillRect(x, y, w, h, TFT_BLACK);
  lcd.setTextFont(1);
  lcd.setTextSize(1);
  lcd.setTextDatum(TL_DATUM);

  int cy = y + 4;
  lcd.setTextColor(TFT_YELLOW, TFT_BLACK);
  lcd.drawString("SSH Host Key", x + 4, cy);
  cy += 12;

  lcd.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
  String hostLine = _host + ":" + String(_port);
  lcd.drawString(hostLine.c_str(), x + 4, cy);
  cy += 14;

  String fp;
  if (_mutex && xSemaphoreTake(_mutex, pdMS_TO_TICKS(20)) == pdTRUE) {
    fp = _hostFingerprint;
    xSemaphoreGive(_mutex);
  }

  int charsPerLine = max(8, (w - 8) / 6);
  for (int pos = 0; pos < (int)fp.length() && cy < y + h - 32;
       pos += charsPerLine) {
    String line = fp.substring(pos, min(pos + charsPerLine, (int)fp.length()));
    lcd.drawString(line.c_str(), x + 4, cy);
    cy += 10;
  }

  int buttonY = y + h - 25;
  int buttonW = (w - 12) / 2;

  auto drawButton = [&](int bx, const char* label, bool selected) {
    lcd.drawRoundRect(
      bx, buttonY, buttonW, 19, 3,
      selected ? TFT_YELLOW : TFT_DARKGREY);
    lcd.setTextDatum(MC_DATUM);
    lcd.setTextColor(
      selected ? TFT_WHITE : TFT_LIGHTGREY, TFT_BLACK);
    lcd.drawString(label, bx + buttonW / 2, buttonY + 9);
    lcd.setTextDatum(TL_DATUM);
  };

  drawButton(x + 4, "Trust", _hostKeySelection == 0);
  drawButton(x + 8 + buttonW, "Cancel", _hostKeySelection == 1);
}

void SftpClientScreen::_renderRemoteBrowser() {
  // Let ListScreen use the full browser body. A manually painted footer here
  // obscures the last visible rows.
  ListScreen::onRender();
}

void SftpClientScreen::_renderLocalBrowser() {
  ListScreen::onRender();
}
