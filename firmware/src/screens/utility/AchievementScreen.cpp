#include "AchievementScreen.h"
#include "core/ScreenManager.h"
#include "screens/utility/UtilityMenuScreen.h"
#include "ui/actions/ShowStatusAction.h"

void AchievementScreen::onInit()
{
  _showDomains();
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
    char buf[80];
    if (hidden) {
      snprintf(buf, sizeof(buf), "??? Unlock to reveal the secret");
    } else {
      snprintf(buf, sizeof(buf), "%s%s",
               done ? "[UNLOCKED] " : "", cat[ci].desc);
    }
    ShowStatusAction::show(buf);
    render();
  }
}

void AchievementScreen::onBack()
{
  if (_state == STATE_DOMAIN) {
    _showDomains();
  } else {
    Screen.setScreen(new UtilityMenuScreen());
  }
}

void AchievementScreen::_showDomains()
{
  _state = STATE_DOMAINS;

  const AchievementManager::AchDef* cat = Achievement.catalog();

  for (uint8_t d = 0; d < AchievementManager::kDomainCount; d++) {
    uint8_t total    = 0;
    uint8_t unlocked = 0;
    for (uint8_t i = 0; i < AchievementManager::kAchCount; i++) {
      if (cat[i].domain != d) continue;
      total++;
      if (Achievement.isUnlocked(cat[i].id)) unlocked++;
    }
    snprintf(_domainSubs[d], sizeof(_domainSubs[d]), "%u/%u", unlocked, total);
    _domainItems[d] = { AchievementManager::domainName(d), _domainSubs[d] };
  }

  setItems(_domainItems, AchievementManager::kDomainCount);
}

void AchievementScreen::_showDomain(uint8_t domain)
{
  _state    = STATE_DOMAIN;
  _achCount = 0;

  const AchievementManager::AchDef* cat = Achievement.catalog();

  static constexpr const char* kTierExp[] = { "+100", "+300", "+600", "+1000" };

  for (uint8_t i = 0; i < AchievementManager::kAchCount && _achCount < kMaxPerDomain; i++) {
    if (cat[i].domain != domain) continue;
    uint8_t n    = _achCount;
    bool    done = Achievement.isUnlocked(cat[i].id);
    snprintf(_achLabels[n], sizeof(_achLabels[n]), "%s %s",
             done ? "*" : " ", cat[i].title);
    snprintf(_achSubs[n], sizeof(_achSubs[n]), "%s%s",
             AchievementManager::tierLabel(cat[i].tier), kTierExp[cat[i].tier]);
    _achItems[n]  = { _achLabels[n], _achSubs[n] };
    _achCatIdx[n] = i;
    _achCount++;
  }

  setItems(_achItems, _achCount);
}