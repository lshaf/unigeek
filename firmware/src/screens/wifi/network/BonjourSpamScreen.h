#pragma once
#include "ui/templates/ListScreen.h"
#include "utils/network/BonjourSpamUtil.h"

class BonjourSpamScreen : public ListScreen
{
public:
  BonjourSpamScreen();
  ~BonjourSpamScreen() override;

  const char* title()       override { return "Bonjour Spam"; }
  bool inhibitPowerOff()    override { return _running; }
  bool inhibitPowerSave()   override { return _running; }

  void onInit()                       override;
  void onUpdate()                     override;
  void onRender()                     override;
  void onBack()                       override;
  void onItemSelected(uint8_t index)  override;

private:
  enum State { ST_IDLE, ST_RUNNING };

  // Idle list rows: [0..CAT_COUNT-1] category toggles, [CAT_COUNT] Start
  static constexpr uint8_t IDLE_ROWS = 1 + BonjourSpamUtil::CAT_COUNT;

  State    _state          = ST_IDLE;
  bool     _running        = false;
  uint32_t _spamStartMs    = 0;
  uint32_t _lastTickMs     = 0;
  uint32_t _lastRenderMs   = 0;
  bool     _spam1minFired  = false;
  bool     _statusChrome   = false;

  ListItem _items[IDLE_ROWS];
  String   _itemSubs[BonjourSpamUtil::CAT_COUNT]; // Stores sublabels only for categories

  void _showIdle();
  void _refreshIdleLabels();
  void _start();
  void _stop();
  void _toggleCategory(uint8_t idx);
  void _drawStatus();
};
