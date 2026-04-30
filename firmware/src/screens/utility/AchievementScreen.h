#pragma once
#include "ui/templates/ListScreen.h"
#include "core/AchievementManager.h"

class AchievementScreen : public ListScreen {
public:
  const char* title() override { return "Achievements"; }

  void onInit()                      override;
  void onUpdate()                    override;
  void onRender()                    override;
  void onItemSelected(uint8_t index) override;
  void onBack()                      override;

private:
  enum State { STATE_DOMAINS, STATE_DOMAIN } _state = STATE_DOMAINS;
  uint8_t _activeDomain = 0;

  static constexpr uint8_t  kMaxPerDomain = 64;
  static constexpr uint8_t kRowHAch = 25;  // 2-line achievement row height
  static constexpr uint8_t kRowHDom = 25;  // 2-line domain row height

  // Domain list
  ListItem _domainItems[AchievementManager::kDomainCount];
  char     _domainSubs[AchievementManager::kDomainCount][8];
  uint16_t _domainExp[AchievementManager::kDomainCount];

  // Per-domain achievement list
  ListItem _achItems[kMaxPerDomain];
  uint8_t  _achCatIdx[kMaxPerDomain];
  uint8_t  _achCount     = 0;
  uint8_t  _domScrollOff = 0;
  uint8_t  _achScrollOff = 0;
  bool     _holdFired    = false;

  void _showDomains();
  void _showDomain(uint8_t domain);
  void _renderDomainsView();
  void _renderDomainView();
  void _renderListItem(int16_t y, bool sel,
                       const char* l1Left,  uint16_t l1LeftCol,
                       const char* l1Right, uint16_t l1RightCol,
                       const char* l2,      uint16_t l2Col);
};