#include "AchievementScreen.h"
#include "core/ScreenManager.h"
#include "core/ConfigManager.h"
#include "screens/utility/UtilityMenuScreen.h"
#include "ui/actions/ShowStatusAction.h"

// ── Lifecycle ────────────────────────────────────────────────────────────────

void AchievementScreen::onInit()
{
  _showDomains();
}

void AchievementScreen::onRender()
{
  TFT_eSprite sp(&Uni.Lcd);
  sp.createSprite(bodyW(), bodyH());
  sp.fillSprite(TFT_BLACK);

  if (_state == STATE_DOMAINS) _renderDomainsView(sp);
  else                         _renderDomainView(sp);

  sp.pushSprite(bodyX(), bodyY());
  sp.deleteSprite();
}

void AchievementScreen::onItemSelected(uint8_t index)
{
  if (_state == STATE_DOMAINS) {
    _activeDomain = index;
    _showDomain(index);
  } else if (_state == STATE_DOMAIN && index < _achCount) {
    const AchievementManager::AchDef* cat = Achievement.catalog();
    uint8_t ci   = _achCatIdx[index];
    bool    done = Achievement.isUnlocked(cat[ci].id);
    bool    hidden = (cat[ci].tier == 3 && !done);
    char buf[96];
    if (hidden) {
      snprintf(buf, sizeof(buf), "??? Unlock to reveal the secret");
    } else {
      snprintf(buf, sizeof(buf), "%s%s", done ? "[UNLOCKED] " : "", cat[ci].desc);
    }
    ShowStatusAction::show(buf);
    render();
  }
}

void AchievementScreen::onBack()
{
  if (_state == STATE_DOMAIN) _showDomains();
  else                        Screen.setScreen(new UtilityMenuScreen());
}

void AchievementScreen::onUpdate()
{
  // Long-press: set achievement title (STATE_DOMAIN only)
  if (_state == STATE_DOMAIN && !_holdFired && Uni.Nav->heldDuration() > 600) {
    _holdFired = true;
    const AchievementManager::AchDef* cat = Achievement.catalog();
    uint8_t ci   = _achCatIdx[_selectedIndex];
    bool    done = Achievement.isUnlocked(cat[ci].id);
    if (!done) {
      ShowStatusAction::show("Unlock first", 1500);
    } else {
      Config.set(APP_CONFIG_AGENT_TITLE, cat[ci].title);
      Config.save(Uni.Storage);
      ShowStatusAction::show("Title saved", 1500);
    }
    render();
    return;
  }
  // Suppress release event after hold
  if (_holdFired && Uni.Nav->wasPressed()) {
    Uni.Nav->readDirection();
    _holdFired = false;
    return;
  }
  ListScreen::onUpdate();
}

// ── Data helpers ─────────────────────────────────────────────────────────────

void AchievementScreen::_showDomains()
{
  _state = STATE_DOMAINS;

  const AchievementManager::AchDef* cat = Achievement.catalog();

  for (uint8_t d = 0; d < AchievementManager::kDomainCount; d++) {
    uint8_t  total    = 0;
    uint8_t  unlocked = 0;
    uint16_t exp      = 0;
    for (uint8_t i = 0; i < AchievementManager::kAchCount; i++) {
      if (cat[i].domain != d) continue;
      total++;
      if (Achievement.isUnlocked(cat[i].id)) {
        unlocked++;
        exp += AchievementManager::tierExp(cat[i].tier);
      }
    }
    snprintf(_domainSubs[d], sizeof(_domainSubs[d]), "%u/%u", unlocked, total);
    _domainExp[d]  = exp;
    _domainItems[d] = { AchievementManager::domainName(d), _domainSubs[d] };
  }

  setItems(_domainItems, AchievementManager::kDomainCount);
}

void AchievementScreen::_showDomain(uint8_t domain)
{
  _state    = STATE_DOMAIN;
  _achCount = 0;

  const AchievementManager::AchDef* cat = Achievement.catalog();

  for (uint8_t i = 0; i < AchievementManager::kAchCount && _achCount < kMaxPerDomain; i++) {
    if (cat[i].domain != domain) continue;
    uint8_t n    = _achCount;
    _achItems[n] = { cat[i].title, nullptr };
    _achCatIdx[n] = i;
    _achCount++;
  }

  setItems(_achItems, _achCount);
}

// ── Shared item renderer ─────────────────────────────────────────────────────

void AchievementScreen::_renderListItem(TFT_eSprite& sp, int16_t y, bool sel,
                                        const char* l1Left,  uint16_t l1LeftCol,
                                        const char* l1Right, uint16_t l1RightCol,
                                        const char* l2,      uint16_t l2Col)
{
  const uint16_t themeC = Config.getThemeColor();
  const uint8_t  rowH   = kRowHAch;
  uint16_t bg = sel ? themeC : TFT_BLACK;

  if (sel)
    sp.fillRoundRect(0, y + 2, bodyW(), rowH - 4, 3, themeC);

  // Line 1: left label + right label (right-aligned)
  int16_t rightW = (int16_t)sp.textWidth(l1Right, 1);
  int16_t rightX = (int16_t)(bodyW() - 2 - rightW);

  sp.setTextColor(l1LeftCol,  bg);
  sp.drawString(l1Left,  3,      y + 4, 1);
  sp.setTextColor(l1RightCol, bg);
  sp.drawString(l1Right, rightX, y + 4, 1);

  // Line 2: sub-label, truncated with ellipsis to full right boundary
  const int16_t maxW = (int16_t)(bodyW() - 2);

  char buf[64];
  strncpy(buf, l2, sizeof(buf) - 1);
  buf[sizeof(buf) - 1] = '\0';

  if ((int16_t)sp.textWidth(buf, 1) > maxW) {
    uint8_t len = (uint8_t)strlen(buf);
    while (len > 3 && (int16_t)sp.textWidth(buf, 1) > maxW)
      buf[--len] = '\0';
    if (len >= 3) { buf[len-3] = '.'; buf[len-2] = '.'; buf[len-1] = '.'; }
  }

  sp.setTextColor(l2Col, bg);
  sp.drawString(buf, 3, y + 14, 1);
}

// ── Custom renderers ─────────────────────────────────────────────────────────

void AchievementScreen::_renderDomainsView(TFT_eSprite& sp)
{
  const uint8_t  rowH    = kRowHDom;
  const uint8_t  visible = bodyH() / rowH;
  const uint8_t  total   = AchievementManager::kDomainCount;
  const uint16_t themeC  = Config.getThemeColor();

  // Bidirectional scroll clamp — persists between renders
  if (_selectedIndex < _domScrollOff)
    _domScrollOff = _selectedIndex;
  else if (_selectedIndex >= _domScrollOff + visible)
    _domScrollOff = _selectedIndex - visible + 1;

  sp.setTextDatum(TL_DATUM);

  for (uint8_t i = 0; i < visible; i++) {
    uint8_t idx = i + _domScrollOff;
    if (idx >= total) break;

    bool    sel = (idx == _selectedIndex);
    int16_t y   = (int16_t)(i * rowH);

    char expBuf[12];
    snprintf(expBuf, sizeof(expBuf), "%u EXP", _domainExp[idx]);

    _renderListItem(sp, y, sel,
      AchievementManager::domainName(idx), TFT_WHITE,
      expBuf,                              sel ? TFT_WHITE : themeC,
      _domainSubs[idx],                    sel ? TFT_WHITE : TFT_DARKGREY);
  }
}

void AchievementScreen::_renderDomainView(TFT_eSprite& sp)
{
  static constexpr uint16_t    kTierColors[4] = { 0xC440, TFT_LIGHTGREY, TFT_YELLOW, TFT_CYAN };
  static constexpr const char* kTierNames[4]  = { "BRONZE", "SILVER", "GOLD", "PLAT" };

  const uint8_t  rowH    = kRowHAch;
  const uint8_t  visible = bodyH() / rowH;

  const AchievementManager::AchDef* cat = Achievement.catalog();

  // Bidirectional scroll clamp — persists between renders
  if (_selectedIndex < _achScrollOff)
    _achScrollOff = _selectedIndex;
  else if (_selectedIndex >= _achScrollOff + visible)
    _achScrollOff = _selectedIndex - visible + 1;

  sp.setTextDatum(TL_DATUM);

  for (uint8_t i = 0; i < visible; i++) {
    uint8_t idx = i + _achScrollOff;
    if (idx >= _achCount) break;

    uint8_t  ci     = _achCatIdx[idx];
    bool     done   = Achievement.isUnlocked(cat[ci].id);
    bool     hidden = (cat[ci].tier == 3 && !done);
    bool     sel    = (idx == _selectedIndex);
    int16_t  y      = (int16_t)(i * rowH);

    uint16_t titleFg = sel ? TFT_WHITE : (done ? TFT_WHITE : 0x4208);
    uint16_t descFg  = sel ? TFT_WHITE : TFT_DARKGREY;
    uint16_t tierFg  = sel ? TFT_WHITE : (done ? kTierColors[cat[ci].tier] : 0x4208);

    const char* tierName = hidden ? "???" : kTierNames[cat[ci].tier];
    const char* descSrc  = hidden ? "Unlock to reveal" : cat[ci].desc;

    _renderListItem(sp, y, sel,
      cat[ci].title, titleFg,
      tierName,      tierFg,
      descSrc,       descFg);
  }
}