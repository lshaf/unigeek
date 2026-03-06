#pragma once

#include "ui/templates/BaseScreen.h"

class WifiESPNowChatScreen : public BaseScreen
{
public:
  const char* title() override { return "ESPNOW Chat"; }
  bool inhibitPowerSave() override { return true; }

  ~WifiESPNowChatScreen() override;

  void onInit() override;
  void onUpdate() override;
  void onRender() override;

  void onDataRecv(const uint8_t* mac, const uint8_t* data, int len);
  static void onDataRecvCb(const uint8_t* mac, const uint8_t* data, int len) {
    if (_instance) _instance->onDataRecv(mac, data, len);
  }

  void sendMessage(const char* text);

private:
  static constexpr int MAX_MESSAGES = 10;

  static WifiESPNowChatScreen* _instance;
  static const uint8_t         _broadcast[6];
  static const uint8_t         _code[5];

  struct Message {
    uint8_t code[5];
    uint8_t nameLength;
    char    name[14];
    uint8_t messageLength;
    char    text[224];
  };

  struct Entry {
    uint8_t sender[6];
    Message msg;
  };

  Entry        _messages[MAX_MESSAGES];
  int          _msgCount  = 0;
  bool         _composing = false;
  bool         _dirty     = false;
  portMUX_TYPE _lock      = portMUX_INITIALIZER_UNLOCKED;
};