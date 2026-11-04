#include "RemoteAccessScreen.h"
#include "core/ScreenManager.h"
#include "screens/wifi/network/remote/RemoteHostMenuScreen.h"

void RemoteAccessScreen::onInit() {
  setItems(_items);
}

void RemoteAccessScreen::onItemSelected(uint8_t index) {
  switch (index) {
    // Each client first offers New Host / Saved Hosts.
    case 0: Screen.push(new RemoteHostMenuScreen(RemoteHostMenuScreen::TCP));    break;
    case 1: Screen.push(new RemoteHostMenuScreen(RemoteHostMenuScreen::TELNET)); break;
    case 2: Screen.push(new RemoteHostMenuScreen(RemoteHostMenuScreen::SSH));    break;
    case 3: Screen.push(new RemoteHostMenuScreen(RemoteHostMenuScreen::FTP));    break;
    case 4: Screen.push(new RemoteHostMenuScreen(RemoteHostMenuScreen::SFTP));   break;
    case 5: Screen.push(new RemoteHostMenuScreen(RemoteHostMenuScreen::WEBDAV)); break;
    default: break;
  }
}

void RemoteAccessScreen::onBack() {
  Screen.goBack();
}
