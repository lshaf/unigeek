#include "WifiESPNowChatScreen.h"
#include "core/Device.h"
#include "core/ScreenManager.h"
#include "core/ConfigManager.h"
#include "screens/wifi/WifiMenuScreen.h"
#include "ui/actions/InputTextAction.h"
#include "ui/actions/ShowStatusAction.h"

#include <WiFi.h>
#include <esp_now.h>

// ── Static definitions ─────────────────────────────────────────────────────

WifiESPNowChatScreen* WifiESPNowChatScreen::_instance  = nullptr;
const uint8_t         WifiESPNowChatScreen::_broadcast[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
const uint8_t         WifiESPNowChatScreen::_code[5]      = {0x00, 0x01, 0x14, 0x25, 0x36};

// ── Destructor ─────────────────────────────────────────────────────────────

WifiESPNowChatScreen::~WifiESPNowChatScreen()
{
  esp_now_unregister_recv_cb();
  esp_now_deinit();
  WiFi.mode(WIFI_OFF);
  _instance = nullptr;
}

// ── Lifecycle ──────────────────────────────────────────────────────────────

void WifiESPNowChatScreen::onInit()
{
  _instance = this;

  WiFi.mode(WIFI_STA);

  if (esp_now_init() != ESP_OK) {
    ShowStatusAction::show("ESP-NOW init failed!", 1500);
    Screen.setScreen(new WifiMenuScreen());
    return;
  }

  esp_now_peer_info_t peer = {};
  memcpy(peer.peer_addr, _broadcast, 6);
  peer.channel = 0;
  peer.encrypt = false;

  if (!esp_now_is_peer_exist(_broadcast)) {
    if (esp_now_add_peer(&peer) != ESP_OK) {
      ShowStatusAction::show("Failed to add peer!", 1500);
      Screen.setScreen(new WifiMenuScreen());
      return;
    }
  }

  esp_now_register_recv_cb(&WifiESPNowChatScreen::onDataRecvCb);
}

void WifiESPNowChatScreen::onUpdate()
{
  if (_composing) return;

  if (_dirty) {
    _dirty = false;
    render();
  }

  if (Uni.Nav->wasPressed()) {
    auto dir = Uni.Nav->readDirection();
    if (dir == INavigation::DIR_BACK) {
      Screen.setScreen(new WifiMenuScreen());
      return;
    }
    // DIR_PRESS = encoder button (no-keyboard) or ENTER key (keyboard boards)
    // NavigationImpl converts \n → DIR_PRESS before onUpdate() runs
    if (dir == INavigation::DIR_PRESS) {
      _composing = true;
      String msg = InputTextAction::popup("Message");
      render();
      _composing = false;
      if (msg.length() > 0) sendMessage(msg.c_str());
    }
  }
}

void WifiESPNowChatScreen::onRender()
{
  const uint16_t themeColor = Config.getThemeColor();
  const int      LINE_H     = 9;
  const int      HINT_H     = 10;

  Sprite sp(&Uni.Lcd);
  sp.createSprite(bodyW(), bodyH());
  sp.fillSprite(TFT_BLACK);

  // Hint bar
  sp.drawFastHLine(0, bodyH() - HINT_H - 1, bodyW(), TFT_DARKGREY);
  sp.setTextColor(TFT_DARKGREY, TFT_BLACK);
  sp.setTextDatum(BL_DATUM);
  sp.setTextSize(1);
#ifdef DEVICE_HAS_KEYBOARD
  sp.drawString("ENTER:Msg  \\b:Exit", 0, bodyH());
#else
  sp.drawString("PRESS:Msg  BACK:Exit", 0, bodyH());
#endif

  // Messages: newest at bottom, oldest at top
  portENTER_CRITICAL(&_lock);
  int y = bodyH() - HINT_H - 2 - LINE_H;
  for (int i = _msgCount - 1; i >= 0 && y >= 0; i--) {
    const Entry& e = _messages[i];

    char name[15] = {};
    strncpy(name, e.msg.name, 14);

    char text[225] = {};
    strncpy(text, e.msg.text, 224);

    char line[64];
    snprintf(line, sizeof(line), "[%s] %s", name, text);

    // Sent messages use theme color, received use white
    bool sent = (e.sender[0] == 0xFF && e.sender[1] == 0xFF &&
                 e.sender[2] == 0xFF && e.sender[3] == 0xFF &&
                 e.sender[4] == 0xFF && e.sender[5] == 0xFF);
    sp.setTextColor(sent ? themeColor : TFT_WHITE, TFT_BLACK);
    sp.setTextDatum(TL_DATUM);
    sp.drawString(line, 0, y);
    y -= LINE_H;
  }
  portEXIT_CRITICAL(&_lock);

  sp.pushSprite(bodyX(), bodyY());
  sp.deleteSprite();
}

// ── ESP-NOW callbacks ──────────────────────────────────────────────────────

void WifiESPNowChatScreen::onDataRecv(const uint8_t* mac, const uint8_t* data, int len)
{
  if (len < (int)sizeof(Message)) return;

  const Message* msg = reinterpret_cast<const Message*>(data);
  if (memcmp(msg->code, _code, sizeof(_code)) != 0) return;

  portENTER_CRITICAL(&_lock);
  if (_msgCount >= MAX_MESSAGES) {
    // Shift out oldest
    for (int i = 0; i < MAX_MESSAGES - 1; i++) _messages[i] = _messages[i + 1];
    _msgCount = MAX_MESSAGES - 1;
  }
  memcpy(_messages[_msgCount].sender, mac, 6);
  memcpy(&_messages[_msgCount].msg, msg, sizeof(Message));
  _msgCount++;
  portEXIT_CRITICAL(&_lock);

  if (Uni.Speaker) Uni.Speaker->playNotification();

  // Auto-reply to ping
  if (msg->messageLength >= 5 && memcmp(msg->text, "!ping", 5) == 0) {
    sendMessage("!pong");
    return;
  }

  _dirty = true;
}

// ── Send ──────────────────────────────────────────────────────────────────

void WifiESPNowChatScreen::sendMessage(const char* text)
{
  String name  = Config.get(APP_CONFIG_DEVICE_NAME, APP_CONFIG_DEVICE_NAME_DEFAULT);
  size_t tlen  = strlen(text);
  if (tlen > 224) tlen = 224;

  Message msg = {};
  memcpy(msg.code, _code, sizeof(_code));
  msg.nameLength    = (uint8_t)name.length();
  msg.messageLength = (uint8_t)tlen;
  strncpy(msg.name, name.c_str(), sizeof(msg.name));
  strncpy(msg.text, text, sizeof(msg.text));

  esp_err_t res = esp_now_send(_broadcast,
                               reinterpret_cast<uint8_t*>(&msg),
                               sizeof(msg));
  if (res != ESP_OK) {
    ShowStatusAction::show("Send failed!", 1500);
    render();
    return;
  }

  // Add to own message queue as sent (sender = broadcast addr as convention)
  portENTER_CRITICAL(&_lock);
  if (_msgCount >= MAX_MESSAGES) {
    for (int i = 0; i < MAX_MESSAGES - 1; i++) _messages[i] = _messages[i + 1];
    _msgCount = MAX_MESSAGES - 1;
  }
  memcpy(_messages[_msgCount].sender, _broadcast, 6);
  memcpy(&_messages[_msgCount].msg, &msg, sizeof(Message));
  _msgCount++;
  portEXIT_CRITICAL(&_lock);

  render();
}
