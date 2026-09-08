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
    ShowStatusAction::show("Not connected to WiFi", 1500);
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
  if (index == 0) {
    _start();
  } else if (index >= 1 && index <= BonjourSpamUtil::CAT_COUNT) {
    _toggleCategory(index - 1);
  }
}

void BonjourSpamScreen::_showIdle() {
  using B = BonjourSpamUtil;
  _state = ST_IDLE;

  _items[0] = {"Start Spam"};
  for (uint8_t i = 0; i < B::CAT_COUNT; i++) {
    _itemSubs[i + 1] = B::isEnabled((B::Category)i) ? "ON" : "OFF";
    _items[i + 1]    = { B::categoryLabel((B::Category)i), _itemSubs[i + 1].c_str() };
  }
  setItems(_items, IDLE_ROWS);
}

// Refresh sublabels in place without resetting the highlighted row.
void BonjourSpamScreen::_refreshIdleLabels() {
  using B = BonjourSpamUtil;
  for (uint8_t i = 0; i < B::CAT_COUNT; i++) {
    _itemSubs[i + 1] = B::isEnabled((B::Category)i) ? "ON" : "OFF";
    _items[i + 1].sublabel = _itemSubs[i + 1].c_str();
  }
  render();
}

void BonjourSpamScreen::_start() {
  if (!BonjourSpamUtil::begin()) {
    ShowStatusAction::show("Failed to start", 1500);
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

// ── Full-screen status dashboard ─────────────────────────────────────────────
//
// Only the default GLCD font (6×8) is loaded by this project's TFT_eSPI
// build — fonts 2/4/6/7/8 are NOT compiled in. Sizing is achieved with
// setTextSize(N): N=1 → 6×8, N=2 → 12×16, N=3 → 18×24, N=4 → 24×32.
//
// Body 204 × 111 (StickS3 worst case):
//
//   y=0..7      ● BROADCASTING                  00:42       size 1 (6×8)
//   y=14..37    [counter]                                   size 3 (18×24)
//   y=42..49    "packets sent"                              size 1, dim
//   y=56..63    Last instance (cyan, centered)             size 1
//   y=67..74    Last category (dim, centered)               size 1

void BonjourSpamScreen::_drawStatus() {
  using B = BonjourSpamUtil;
  auto& lcd = Uni.Lcd;

  const int W = bodyW();
  const int H = bodyH();

  // Center the approved content block as a unit. Internal spacing stays fixed:
  // counter y=14, label y=42, instance y=56, category y=67.
  const int contentTop = 9 + ((H - 9 - 62) / 2);
  const int contentDy  = contentTop - 14;

  // Avoid a full-body 16-bit sprite here: on tighter devices (notably the
  // Cardputer ADV) that allocation can fail once WiFi is active, leaving the
  // status screen black.  Draw the small dynamic regions directly instead.
  if (!_statusChrome) {
    lcd.fillRect(bodyX(), bodyY(), W, H, TFT_BLACK);

    lcd.setTextSize(1);
    lcd.setTextDatum(TL_DATUM);
    lcd.setTextColor(TFT_GREEN, TFT_BLACK);
    lcd.fillCircle(bodyX() + 8, bodyY() + 2, 3, TFT_GREEN);
    lcd.drawString("BROADCASTING", bodyX() + 16, bodyY());

    lcd.setTextDatum(TC_DATUM);
    lcd.setTextColor(0x7BEF, TFT_BLACK);
    lcd.drawString("packets sent", bodyX() + W / 2, bodyY() + 42 + contentDy);

    _statusChrome = true;
  }

  // Elapsed time.
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

  // Packet counter.
  uint32_t total = B::packetsSent();
  char nbuf[16];
  snprintf(nbuf, sizeof(nbuf), "%lu", (unsigned long)total);
  lcd.fillRect(bodyX(), bodyY() + 14 + contentDy, W, 25, TFT_BLACK);
  lcd.setTextSize(3);
  lcd.setTextDatum(TC_DATUM);
  lcd.setTextColor(Config.getThemeColor(), TFT_BLACK);
  lcd.drawString(nbuf, bodyX() + W / 2, bodyY() + 14 + contentDy);

  // Last broadcast. Keep this block close to the packet counter instead of
  // anchoring it to the bottom, which leaves a large visual gap on short/wide
  // displays such as the Cardputer ADV.
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
