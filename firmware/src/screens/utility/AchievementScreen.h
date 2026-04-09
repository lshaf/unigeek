#pragma once
#include "ui/templates/ListScreen.h"
#include "core/AchievementManager.h"

class AchievementScreen : public ListScreen {
public:
  const char* title() override { return "Achievements"; }

  void onInit()               override;
  void onItemSelected(uint8_t index) override;
  void onBack()               override;

private:
  enum State { STATE_DOMAINS, STATE_DOMAIN } _state = STATE_DOMAINS;
  uint8_t _activeDomain = 0;

  static constexpr uint8_t kMaxPerDomain  = 32;

  // Domain list
  ListItem _domainItems[AchievementManager::kDomainCount];
  char     _domainSubs[AchievementManager::kDomainCount][8];

  // Per-domain achievement list
  ListItem _achItems[kMaxPerDomain];
  char     _achLabels[kMaxPerDomain][28];
  char     _achSubs[kMaxPerDomain][8];
  uint8_t  _achCatIdx[kMaxPerDomain]; // catalog index for each row
  uint8_t  _achCount = 0;

  void _showDomains();
  void _showDomain(uint8_t domain);
};