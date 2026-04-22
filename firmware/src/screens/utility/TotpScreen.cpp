#include "TotpScreen.h"
#include "core/Device.h"
#include "core/INavigation.h"
#include "core/IStorage.h"
#include "core/ScreenManager.h"
#include "core/ConfigManager.h"
#include "core/AchievementManager.h"
#include "screens/utility/UtilityMenuScreen.h"
#include "ui/actions/InputSelectOption.h"
#include "ui/actions/InputTextAction.h"
#include "ui/actions/ShowStatusAction.h"

// ── Overrides ─────────────────────────────────────────────────────────────────

const char* TotpScreen::title() {
  if (_state == STATE_VIEW) return "TOTP";
  if (_state == STATE_ADD)  return "New TOTP";
  return "TOTP Auth";
}

void TotpScreen::onInit() {
  _loadAccounts();
  for (uint8_t i = 0; i < _accountCount; i++) _items[i] = { _accounts[i].name, nullptr };
  _items[_accountCount] = { "Add New", nullptr };
  setItems(_items, _accountCount + 1);
}

void TotpScreen::onUpdate() {
  if (_state == STATE_MENU || _state == STATE_ADD) {
    if (_state == STATE_MENU && _selectedIndex < _accountCount) {
      if (!Uni.Nav->isPressed()) _holdFired = false;
      if (!_holdFired && Uni.Nav->isPressed() && Uni.Nav->heldDuration() >= 700) {
        _holdFired = true;
        Uni.Nav->suppressCurrentPress();
        _showAccountMenu(_selectedIndex);
        return;
      }
    }
    ListScreen::onUpdate();
    return;
  }

  // STATE_VIEW: refresh every second, draw only changed body regions
  if (millis() - _lastRefMs >= 1000) {
    _lastRefMs = millis();
    _refreshCode();
    if (!Uni.lcdOff) _renderView();
  }

  if (!Uni.Nav->wasPressed()) return;
  auto dir = Uni.Nav->readDirection();
  if (dir == INavigation::DIR_BACK || dir == INavigation::DIR_PRESS) {
    _state = STATE_MENU;
    setItems(_items, _accountCount + 1);
    render();
  }
}

void TotpScreen::onRender() {
  if (_state == STATE_MENU || _state == STATE_ADD) {
    ListScreen::onRender();
    return;
  }
  _renderView();
}

void TotpScreen::onBack() {
  if (_state == STATE_ADD) {
    _state = STATE_MENU;
    setItems(_items, _accountCount + 1);
    render();
    return;
  }
  Screen.setScreen(new UtilityMenuScreen());
}

void TotpScreen::onItemSelected(uint8_t index) {
  if (_state == STATE_ADD) {
    switch (index) {
      case 0: {
        String v = InputTextAction::popup("Account Name", _pendingName.c_str());
        if (!InputTextAction::wasCancelled()) _pendingName = v;
        _updateAddLabels(); render();
        break;
      }
      case 1: {
        String v = InputTextAction::popup("Base32 Secret", _pendingSecret.c_str());
        if (!InputTextAction::wasCancelled()) {
          v.replace(" ", ""); v.toUpperCase(); _pendingSecret = v;
        }
        _updateAddLabels(); render();
        break;
      }
      case 2:
        _pendingDigits = (_pendingDigits == 6) ? 8 : 6;
        _updateAddLabels(); render();
        break;
      case 3:
        _pendingPeriod = (_pendingPeriod == 30) ? 60 : 30;
        _updateAddLabels(); render();
        break;
      case 4:
        if      (_pendingAlgo == TotpAlgo::SHA1)   _pendingAlgo = TotpAlgo::SHA256;
        else if (_pendingAlgo == TotpAlgo::SHA256)  _pendingAlgo = TotpAlgo::SHA512;
        else                                        _pendingAlgo = TotpAlgo::SHA1;
        _updateAddLabels(); render();
        break;
      case 5: _saveNew(); break;
      default: break;
    }
    return;
  }

  if (index == _accountCount) { _addNew(); return; }

  int n = Achievement.inc("totp_first_view");
  if (n == 1) Achievement.unlock("totp_first_view");
  _enterView(index);
}

// ── Menu helpers ──────────────────────────────────────────────────────────────

void TotpScreen::_loadAccounts() {
  _accountCount = 0;
  if (!Uni.Storage || !Uni.Storage->isAvailable()) return;
  Uni.Storage->makeDir(DIR);

  IStorage::DirEntry entries[MAX_ACCOUNTS];
  uint8_t count = Uni.Storage->listDir(DIR, entries, MAX_ACCOUNTS);

  for (uint8_t i = 0; i < count && _accountCount < MAX_ACCOUNTS; i++) {
    if (entries[i].isDir) continue;
    if (!entries[i].name.endsWith(".key")) continue;

    String path = String(DIR) + "/" + entries[i].name;
    String line = Uni.Storage->readFile(path.c_str());
    line.trim();
    if (line.length() == 0) continue;

    // Parse: SECRET|DIGITS|PERIOD|ALGO (old files: just SECRET → defaults)
    uint8_t  digits = 6, period = 30;
    TotpAlgo algo   = TotpAlgo::SHA1;
    String   sec    = line;

    int p1 = line.indexOf('|');
    if (p1 >= 0) {
      sec         = line.substring(0, p1);
      String rest = line.substring(p1 + 1);
      int p2      = rest.indexOf('|');
      if (p2 >= 0) {
        uint8_t d = (uint8_t)rest.substring(0, p2).toInt();
        if (d == 6 || d == 8) digits = d;
        rest      = rest.substring(p2 + 1);
        int p3    = rest.indexOf('|');
        if (p3 >= 0) {
          uint8_t per = (uint8_t)rest.substring(0, p3).toInt();
          if (per > 0) period = per;
          algo = TotpUtil::algoFromStr(rest.substring(p3 + 1).c_str());
        } else {
          uint8_t per = (uint8_t)rest.toInt();
          if (per > 0) period = per;
        }
      } else {
        uint8_t d = (uint8_t)rest.toInt();
        if (d == 6 || d == 8) digits = d;
      }
    }

    String acc = entries[i].name;
    acc = acc.substring(0, acc.length() - 4);
    acc.toCharArray(_accounts[_accountCount].name,   sizeof(_accounts[0].name));
    sec.toCharArray(_accounts[_accountCount].secret, sizeof(_accounts[0].secret));
    _accounts[_accountCount].digits = digits;
    _accounts[_accountCount].period = period;
    _accounts[_accountCount].algo   = algo;
    _accountCount++;
  }
}

void TotpScreen::_addNew() {
  _pendingName   = "";
  _pendingSecret = "";
  _enterAdd();
}

void TotpScreen::_enterAdd() {
  _pendingDigits = 6;
  _pendingPeriod = 30;
  _pendingAlgo   = TotpAlgo::SHA1;
  _updateAddLabels();
  _addItems[0] = {"Name",      _addNameLabel};
  _addItems[1] = {"Secret",    _addSecretLabel};
  _addItems[2] = {"Digits",    _addDigitsLabel};
  _addItems[3] = {"Period",    _addPeriodLabel};
  _addItems[4] = {"Algorithm", _addAlgoLabel};
  _addItems[5] = {"Save",      nullptr};
  _state = STATE_ADD;
  setItems(_addItems, 6);
  render();
}

void TotpScreen::_updateAddLabels() {
  if (_pendingName.length() == 0)
    strncpy(_addNameLabel, "-", sizeof(_addNameLabel));
  else
    strncpy(_addNameLabel, _pendingName.c_str(), sizeof(_addNameLabel) - 1);
  _addNameLabel[sizeof(_addNameLabel) - 1] = '\0';

  strncpy(_addSecretLabel, _pendingSecret.length() > 0 ? "(set)" : "-", sizeof(_addSecretLabel) - 1);
  _addSecretLabel[sizeof(_addSecretLabel) - 1] = '\0';

  snprintf(_addDigitsLabel, sizeof(_addDigitsLabel), "%d", _pendingDigits);
  snprintf(_addPeriodLabel, sizeof(_addPeriodLabel), "%d", _pendingPeriod);
  strncpy(_addAlgoLabel, TotpUtil::algoName(_pendingAlgo), sizeof(_addAlgoLabel) - 1);
  _addAlgoLabel[sizeof(_addAlgoLabel) - 1] = '\0';
}

void TotpScreen::_saveNew() {
  if (_pendingName.length() == 0 || _pendingSecret.length() == 0) {
    ShowStatusAction::show("Name+Secret required", 1200);
    render();
    return;
  }
  String content = _pendingSecret + "|" + String(_pendingDigits) + "|" +
                   String(_pendingPeriod) + "|" + String(TotpUtil::algoName(_pendingAlgo));
  Uni.Storage->makeDir(DIR);
  String path = String(DIR) + "/" + _pendingName + ".key";
  bool ok = Uni.Storage->writeFile(path.c_str(), content.c_str());
  if (ok) {
    int n = Achievement.inc("totp_add_account");
    if (n == 1) Achievement.unlock("totp_add_account");
    ShowStatusAction::show("Saved!", 1000);
  } else {
    ShowStatusAction::show("Save failed", 1000);
  }
  Screen.setScreen(new TotpScreen());
}

// ── Account menu (hold) ───────────────────────────────────────────────────────

void TotpScreen::_showAccountMenu(uint8_t index) {
  static constexpr InputSelectAction::Option opts[] = {
    {"View",   "view"},
    {"Delete", "delete"},
  };
  const char* r = InputSelectAction::popup(_accounts[index].name, opts, 2, nullptr);
  if (!r) { render(); return; }
  if (strcmp(r, "view") == 0) {
    int n = Achievement.inc("totp_first_view");
    if (n == 1) Achievement.unlock("totp_first_view");
    _enterView(index);
  } else if (strcmp(r, "delete") == 0) {
    _deleteAccount(index);
  }
}

void TotpScreen::_deleteAccount(uint8_t index) {
  String path = String(DIR) + "/" + _accounts[index].name + ".key";
  bool ok = Uni.Storage->deleteFile(path.c_str());
  ShowStatusAction::show(ok ? "Deleted" : "Delete failed", 1000);
  Screen.setScreen(new TotpScreen());
}

// ── View helpers ──────────────────────────────────────────────────────────────

void TotpScreen::_enterView(uint8_t index) {
  strncpy(_viewName,   _accounts[index].name,   sizeof(_viewName)   - 1);
  strncpy(_viewSecret, _accounts[index].secret, sizeof(_viewSecret) - 1);
  _viewDigits = _accounts[index].digits;
  _viewPeriod = _accounts[index].period;
  _viewAlgo   = _accounts[index].algo;
  _state = STATE_VIEW;
  _viewFirstRender = true;
  _refreshCode();
  _lastRefMs = millis();
  render(); // draws chrome + full body for first time
}

void TotpScreen::_refreshCode() {
  time_t now = time(nullptr);
  _timeValid = (now > 1577836800L);
  _code      = _timeValid ? TotpUtil::compute(_viewSecret, now, _viewDigits, _viewPeriod, _viewAlgo) : 0;
  _secsLeft  = TotpUtil::secondsLeft(now, _viewPeriod);
}

// All regions are redrawn every call as sprites — no full-body fillRect
// after the first entry, so there is no blank-flash flicker on 1-second ticks.
void TotpScreen::_renderView() {
  auto& lcd = Uni.Lcd;
  int x = bodyX(), y = bodyY(), w = bodyW(), h = bodyH();

  // First entry only: clear body to remove list-state remnants.
  if (_viewFirstRender) {
    lcd.fillRect(x, y, w, h, TFT_BLACK);
    _viewFirstRender = false;
  }

  // Fixed region sizes
  static constexpr int NAME_H    = 10;  // account label
  static constexpr int NAME_PAD  = 3;
  static constexpr int BAR_REG_H = 21;  // label(8) + gap(3) + bar(5) + pad(5)
  static constexpr int BAR_PAD   = 3;   // gap from body bottom
  static constexpr int CLOCK_H   = 10;  // HH:MM:SS label
  static constexpr int CLOCK_GAP = 3;

  // Anchored positions (body-relative, pushed as y+offset)
  int nameTop  = NAME_PAD;
  int barTop   = h - BAR_REG_H - BAR_PAD;

  // Code + clock block centered in the middle region
  int midTop   = nameTop + NAME_H + 4;
  int midBot   = barTop  - 4;
  int midH     = midBot - midTop;
  int codeH    = (midH >= 16) ? 18 : 10;
  int blockH   = codeH + CLOCK_GAP + CLOCK_H;
  int blockTop = midTop + (midH > blockH ? (midH - blockH) / 2 : 0);
  int codeTop  = blockTop;
  int clockTop = blockTop + codeH + CLOCK_GAP;

  // 1 ── Account name sprite (redrawn every tick so no stale state after menu switch)
  {
    Sprite sp(&lcd);
    sp.createSprite(w, NAME_H);
    sp.fillSprite(TFT_BLACK);
    sp.setTextColor(TFT_DARKGREY);
    sp.setTextSize(1);
    sp.setTextDatum(TC_DATUM);
    sp.drawString(_viewName, w / 2, 1);
    sp.pushSprite(x, y + nameTop);
    sp.deleteSprite();
  }

  if (!_timeValid) {
    // Error message centered in the mid region
    int errH = max(midH, 20);
    Sprite sp(&lcd);
    sp.createSprite(w, errH);
    sp.fillSprite(TFT_BLACK);
    sp.setTextColor(TFT_RED);
    sp.setTextSize(1);
    sp.setTextDatum(TC_DATUM);
    sp.drawString("No time sync", w / 2, 2);
    sp.setTextColor(TFT_DARKGREY);
    sp.drawString("Connect WiFi for NTP", w / 2, 14);
    sp.pushSprite(x, y + midTop);
    sp.deleteSprite();
    return;
  }

  // 2 ── Code sprite ("123 456" or "1234 5678")
  char buf[10];
  if (_viewDigits == 8)
    snprintf(buf, sizeof(buf), "%04lu %04lu",
             (unsigned long)(_code / 10000), (unsigned long)(_code % 10000));
  else
    snprintf(buf, sizeof(buf), "%03lu %03lu",
             (unsigned long)(_code / 1000), (unsigned long)(_code % 1000));
  {
    uint8_t sz = (codeH >= 16) ? 2 : 1;
    Sprite sp(&lcd);
    sp.createSprite(w, codeH);
    sp.fillSprite(TFT_BLACK);
    sp.setTextColor(TFT_WHITE);
    sp.setTextSize(sz);
    sp.setTextDatum(MC_DATUM);
    sp.drawString(buf, w / 2, codeH / 2);
    sp.pushSprite(x, y + codeTop);
    sp.deleteSprite();
  }

  // 3 ── Clock sprite (HH:MM:SS)
  {
    time_t now = time(nullptr);
    struct tm* t = localtime(&now);
    char clockBuf[9];
    snprintf(clockBuf, sizeof(clockBuf), "%02d:%02d:%02d",
             t->tm_hour, t->tm_min, t->tm_sec);
    Sprite sp(&lcd);
    sp.createSprite(w, CLOCK_H);
    sp.fillSprite(TFT_BLACK);
    sp.setTextColor(TFT_DARKGREY);
    sp.setTextSize(1);
    sp.setTextDatum(TC_DATUM);
    sp.drawString(clockBuf, w / 2, 1);
    sp.pushSprite(x, y + clockTop);
    sp.deleteSprite();
  }

  // 4 ── Bar + seconds label sprite (anchored to body bottom)
  //  layout inside sprite: label at top (TC_DATUM, y=1) | gap | bar
  {
    uint16_t barColor = (_secsLeft <= 5) ? TFT_RED : Config.getThemeColor();
    char secBuf[5];
    snprintf(secBuf, sizeof(secBuf), "%ds", _secsLeft);

    Sprite sp(&lcd);
    sp.createSprite(w, BAR_REG_H);
    sp.fillSprite(TFT_BLACK);

    // Seconds label — TC_DATUM so y is the TOP of the text (8px tall)
    sp.setTextColor((_secsLeft <= 5) ? TFT_RED : TFT_DARKGREY);
    sp.setTextSize(1);
    sp.setTextDatum(TC_DATUM);
    sp.drawString(secBuf, w / 2, 1);    // occupies y=1..9

    // Bar below label
    const int barY = 13;
    const int barH = 5;
    const int barX = 6;
    const int barW = w - 12;
    int filled     = (int)((long)barW * _secsLeft / _viewPeriod);
    sp.fillRect(barX, barY, barW, barH, 0x2104);
    sp.fillRect(barX, barY, filled, barH, barColor);

    sp.pushSprite(x, y + barTop);
    sp.deleteSprite();
  }
}
