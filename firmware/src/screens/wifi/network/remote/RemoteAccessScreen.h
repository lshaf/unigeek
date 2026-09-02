#pragma once

#include "ui/templates/ListScreen.h"

class RemoteAccessScreen : public ListScreen
{
public:
  const char* title() override { return "Remote Access"; }

  void onInit() override;
  void onItemSelected(uint8_t index) override;
  void onBack() override;

private:
  ListItem _items[6] = {
    {"TCP"},
    {"Telnet"},
    {"SSH"},
    {"FTP"},
    {"SFTP"},
    {"WebDAV"},
  };
};
