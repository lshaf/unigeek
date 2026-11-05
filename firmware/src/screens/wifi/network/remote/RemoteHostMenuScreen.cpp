#include "RemoteHostMenuScreen.h"

#include "core/ScreenManager.h"
#include "ui/actions/ShowStatusAction.h"
#include "screens/wifi/network/remote/TcpClientScreen.h"
#include "screens/wifi/network/remote/TelnetClientScreen.h"
#include "screens/wifi/network/remote/SshClientScreen.h"
#include "screens/wifi/network/remote/SftpClientScreen.h"
#include "screens/wifi/network/remote/FtpClientScreen.h"
#include "screens/wifi/network/remote/WebDavClientScreen.h"

const char* RemoteHostMenuScreen::title() {
  switch (_service) {
    case TCP:    return "TCP Client";
    case TELNET: return "Telnet Client";
    case SSH:    return "SSH Client";
    case FTP:    return "FTP Client";
    case SFTP:   return "SFTP Client";
    default:     return "WebDAV Client";
  }
}

const char* RemoteHostMenuScreen::_dir() const {
  switch (_service) {
    case TCP:    return "/unigeek/wifi/remote/tcp";
    case TELNET: return "/unigeek/wifi/remote/telnet";
    case FTP:    return "/unigeek/wifi/remote/ftp";
    case WEBDAV: return "/unigeek/wifi/remote/webdav";
    default:     return "/unigeek/wifi/remote/ssh";  // SSH and SFTP
  }
}

void RemoteHostMenuScreen::onInit() {
  _showMenu();
}

// Back from a client: a deleted or renamed host must not linger in the list.
void RemoteHostMenuScreen::onRestore() {
  if (_state == STATE_SAVED) {
    _showSaved();
    if (_hosts.empty()) _showMenu(1);
  } else {
    render();
  }
}

void RemoteHostMenuScreen::_showMenu(uint8_t selected) {
  _state = STATE_MENU;
  setItems(_menuItems, 2, selected);
  render();
}

void RemoteHostMenuScreen::_showSaved() {
  _hosts = SavedHosts::load(_dir());
  if (_hosts.size() > MAX_SAVED) _hosts.resize(MAX_SAVED);
  for (size_t i = 0; i < _hosts.size(); ++i) {
    // Name, with the address it points to as the detail.
    _details[i] = _hosts[i].has("url") ? _hosts[i].get("url") : _hosts[i].get("host");
    _hostItems[i] = {_hosts[i].name.c_str(), _details[i].c_str()};
  }
  _state = STATE_SAVED;
  const uint8_t n = (uint8_t)_hosts.size();
  setItems(_hostItems, n, _savedSelection < n ? _savedSelection : 0);
  render();
}

void RemoteHostMenuScreen::onItemSelected(uint8_t index) {
  if (_state == STATE_MENU) {
    if (index == 0) { _open(nullptr); return; }
    _showSaved();
    if (_hosts.empty()) {
      ShowStatusAction::show("No saved hosts", 1500);
      _showMenu(1);
    }
    return;
  }
  if (index >= _hosts.size()) return;
  _savedSelection = index;
  _open(&_hosts[index]);
}

void RemoteHostMenuScreen::onBack() {
  if (_state == STATE_SAVED) { _showMenu(1); return; }
  Screen.goBack();
}

void RemoteHostMenuScreen::_open(const SavedHost* host) {
  switch (_service) {
    case TCP:    { auto* s = new TcpClientScreen();    if (host) s->loadSaved(*host); Screen.push(s); break; }
    case TELNET: { auto* s = new TelnetClientScreen(); if (host) s->loadSaved(*host); Screen.push(s); break; }
    case SSH:    { auto* s = new SshClientScreen();    if (host) s->loadSaved(*host); Screen.push(s); break; }
    case FTP:    { auto* s = new FtpClientScreen();    if (host) s->loadSaved(*host); Screen.push(s); break; }
    case SFTP:   { auto* s = new SftpClientScreen();   if (host) s->loadSaved(*host); Screen.push(s); break; }
    case WEBDAV: { auto* s = new WebDavClientScreen(); if (host) s->loadSaved(*host); Screen.push(s); break; }
  }
}
