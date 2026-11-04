#pragma once

#include <vector>
#include "ui/templates/ListScreen.h"
#include "utils/network/SavedHosts.h"

// First screen of each Remote Access client: New Host (the empty setup) or
// Saved Hosts (the names saved for this service; picking one opens the setup
// filled in, highlighted on its first row like any setup). SSH and SFTP share
// their saved hosts.
class RemoteHostMenuScreen : public ListScreen {
public:
  enum Service : uint8_t { TCP, TELNET, SSH, FTP, SFTP, WEBDAV };

  explicit RemoteHostMenuScreen(Service service) : _service(service) {}

  const char* title() override;
  void onInit() override;
  void onRestore() override;
  void onItemSelected(uint8_t index) override;
  void onBack() override;

private:
  enum State : uint8_t { STATE_MENU, STATE_SAVED };
  static constexpr uint8_t MAX_SAVED = 32;

  Service  _service;
  State    _state = STATE_MENU;
  ListItem _menuItems[2] = {{"New Host"}, {"Saved Hosts"}};
  std::vector<SavedHost> _hosts;
  ListItem _hostItems[MAX_SAVED];
  String   _details[MAX_SAVED];  // address shown next to each name
  uint8_t  _savedSelection = 0;

  const char* _dir() const;
  void _showMenu(uint8_t selected = 0);
  void _showSaved();
  void _open(const SavedHost* host);
};
