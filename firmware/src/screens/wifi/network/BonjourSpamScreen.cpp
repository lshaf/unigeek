#include "BonjourSpamScreen.h"
#include <WiFi.h>
#include <stdio.h>
#include "core/Device.h"
#include "core/ConfigManager.h"
#include "core/ScreenManager.h"
#include "core/AchievementManager.h"
#include "ui/actions/ShowStatusAction.h"

// Send one phantom per tick. 30 ms × 40 phantoms = 1.2 s full cycle.
static constexpr uint32_t TICK_MS   = 30;
static constexpr uint32_t RENDER_MS = 200;

BonjourSpamScreen::BonjourSpamScreen() {}

BonjourSpamScreen::~BonjourSpamScreen() {
  BonjourSpamUtil::end();
}

void BonjourSpamScreen::onInit() {
  if (WiFi.status() != WL_CONNECTED) {
    ShowStatusAction::show("Not connected", 1500);
    Screen.goBack();
    return;
  }
  BonjourSpamUtil::resetCounter();
  _showIdle();
}

void BonjourSpamScreen::onUpdate() {
  if (_state == ST_RUNNING) {
    if (Uni.Nav->wasPressed()) {
      auto dir = Uni.Nav->readDirection();
      if (dir == INavigation::DIR_BACK || dir == INavigation::DIR_PRESS) {
        _stop();
        return;
      }
    }

    if (millis() - _lastTickMs >= TICK_MS) {
      _lastTickMs = millis();
      BonjourSpamUtil::tick();
    }
    if (!_spam1minFired && millis() - _spamStartMs >= 60000) {
      _spam1minFired = true;
      int n = Achievement.inc("wifi_bonjour_spam_1min");
      if (n == 1) Achievement.unlock("wifi_bonjour_spam_1min");
    }
    if (millis() - _lastRenderMs >= RENDER_MS) {
      _lastRenderMs = millis();
      render();
    }
    return;
  }

  ListScreen::onUpdate();
}

void BonjourSpamScreen::onRender() {
  if (_state == ST_RUNNING) {
    _drawStatus();
    return;
  }
  ListScreen::onRender();
}

void BonjourSpamScreen::onBack() {
  if (_state == ST_RUNNING) { _stop(); return; }
  BonjourSpamUtil::end();
  Screen.goBack();
}

void BonjourSpamScreen::onItemSelected(uint8_t index) {
  if (_state != ST_IDLE) return;
  // Categories occupy indices [0 .. CAT_COUNT - 1]
  if (index < BonjourSpamUtil::CAT_COUNT) {
    _toggleCategory(index);
  } else if (index == BonjourSpamUtil::CAT_COUNT) {
    // "Start Spam" is placed at the last position (index CAT_COUNT)
    _start();
  }
}

void BonjourSpamScreen::_showIdle() {
  using B = BonjourSpamUtil;
  _state = ST_IDLE;

  // Populate categories first (indices 0 to CAT_COUNT - 1)
  for (uint8_t i = 0; i < B::CAT_COUNT; i++) {
    _itemSubs[i] = B::isEnabled((B::Category)i) ? "On" : "Off";
    _items[i]    = { B::categoryLabel((B::Category)i), _itemSubs[i].c_str() };
  }
  // Place "Start Spam" as the last item in the list
  _items[B::CAT_COUNT] = {"Start"};

  setItems(_items, IDLE_ROWS);
}

// Refresh sublabels in place without resetting the highlighted row.
void BonjourSpamScreen::_refreshIdleLabels() {
  using B = BonjourSpamUtil;
  for (uint8_t i = 0; i < B::CAT_COUNT; i++) {
    _itemSubs[i] = B::isEnabled((B::Category)i) ? "On" : "Off";
    _items[i].sublabel = _itemSubs[i].c_str();
  }
  render();
}

void BonjourSpamScreen::_start() {
  if (!BonjourSpamUtil::begin()) {
    ShowStatusAction::show("Failed", 1500);
    return;
  }
  _running       = true;
  _state         = ST_RUNNING;
  _spamStartMs   = millis();
  _lastTickMs    = 0;          // fire immediately on first onUpdate
  _lastRenderMs  = 0;
  _spam1minFired = false;
  _statusChrome  = false;

  int n = Achievement.inc("wifi_bonjour_spam_first");
  if (n == 1) Achievement.unlock("wifi_bonjour_spam_first");

  render();
}

void BonjourSpamScreen::_stop() {
  BonjourSpamUtil::end();
  _running = false;
  _showIdle();
}

void BonjourSpamScreen::_toggleCategory(uint8_t idx) {
  using B = BonjourSpamUtil;
  bool was = B::isEnabled((B::Category)idx);
  B::setEnabled((B::Category)idx, !was);
  _refreshIdleLabels();   // preserve highlight position
}

void BonjourSpamScreen::_drawStatus() {
  using B = BonjourSpamUtil;
  auto& lcd = Uni.Lcd;

  const int W = bodyW();
  const int H = bodyH();

  const int contentTop = 9 + ((H - 9 - 62) / 2);
  const int contentDy  = contentTop - 14;

  if (!_statusChrome) {
    lcd.fillRect(bodyX(), bodyY(), W, H, TFT_BLACK);

    lcd.setTextSize(1);
    lcd.setTextDatum(TL_DATUM);
    lcd.setTextColor(TFT_GREEN, TFT_BLACK);
    lcd.fillCircle(bodyX() + 8, bodyY() + 2, 3, TFT_GREEN);
    lcd.drawString("Broadcasting...", bodyX() + 16, bodyY());

    lcd.setTextDatum(TC_DATUM);
    lcd.setTextColor(0x7BEF, TFT_BLACK);
    lcd.drawString("packets sent", bodyX() + W / 2, bodyY() + 42 + contentDy);

    _statusChrome = true;
  }

  uint32_t elapsedSec = (millis() - _spamStartMs) / 1000;
  char tbuf[12];
  snprintf(tbuf, sizeof(tbuf), "%02lu:%02lu",
           (unsigned long)(elapsedSec / 60),
           (unsigned long)(elapsedSec % 60));
  lcd.fillRect(bodyX() + W - 42, bodyY(), 42, 9, TFT_BLACK);
  lcd.setTextSize(1);
  lcd.setTextDatum(TR_DATUM);
  lcd.setTextColor(TFT_WHITE, TFT_BLACK);
  lcd.drawString(tbuf, bodyX() + W - 2, bodyY());

  uint32_t total = B::packetsSent();
  char nbuf[16];
  snprintf(nbuf, sizeof(nbuf), "%lu", (unsigned long)total);
  lcd.fillRect(bodyX(), bodyY() + 14 + contentDy, W, 25, TFT_BLACK);
  lcd.setTextSize(3);
  lcd.setTextDatum(TC_DATUM);
  lcd.setTextColor(Config.getThemeColor(), TFT_BLACK);
  lcd.drawString(nbuf, bodyX() + W / 2, bodyY() + 14 + contentDy);

  const int instTop = bodyY() + 56 + contentDy;
  const int catTop  = bodyY() + 67 + contentDy;
  lcd.fillRect(bodyX(), instTop, W, (catTop - instTop) + 9, TFT_BLACK);

  String lastInst = B::lastInstance();
  if (lastInst.isEmpty()) lastInst = "(waiting...)";
  lcd.setTextSize(1);
  while (lcd.textWidth(lastInst.c_str()) > W - 8 && lastInst.length() > 1) {
    lastInst.remove(lastInst.length() - 1);
  }

  lcd.setTextDatum(TC_DATUM);
  lcd.setTextColor(TFT_CYAN, TFT_BLACK);
  lcd.drawString(lastInst.c_str(), bodyX() + W / 2, instTop);

  lcd.setTextColor(0x7BEF, TFT_BLACK);
  lcd.drawString(B::categoryLabel(B::lastCategory()), bodyX() + W / 2, catTop);
}
