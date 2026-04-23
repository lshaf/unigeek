#include "ChameleonSlotsScreen.h"
#include "utils/chameleon/ChameleonClient.h"
#include "ChameleonMenuScreen.h"
#include "ChameleonSlotEditScreen.h"
#include "core/Device.h"
#include "core/ScreenManager.h"
#include "core/AchievementManager.h"
#include <string.h>

void ChameleonSlotsScreen::_load() {
  auto& c = ChameleonClient::get();

  c.getActiveSlot(&_activeSlot);

  ChameleonClient::SlotTypes types[8] = {};
  bool hfEn[8] = {}, lfEn[8] = {};
  c.getSlotTypes(types);
  c.getEnabledSlots(hfEn, lfEn);

  for (int i = 0; i < kSlots; i++) {
    bool active = (i == (int)_activeSlot);
    snprintf(_labels[i], sizeof(_labels[i]), "Slot %d%s", i + 1, active ? " [*]" : "");

    const char* hf = ChameleonClient::tagTypeName(types[i].hfType);
    const char* lf = ChameleonClient::tagTypeName(types[i].lfType);
    snprintf(_sublabels[i], sizeof(_sublabels[i]), "HF:%s  LF:%s", hf, lf);

    _items[i].label    = _labels[i];
    _items[i].sublabel = _sublabels[i];
  }
}

void ChameleonSlotsScreen::onInit() {
  _load();
  setItems(_items);

  int n = Achievement.inc("chameleon_slots_viewed");
  if (n == 1) Achievement.unlock("chameleon_slots_viewed");
}

void ChameleonSlotsScreen::onItemSelected(uint8_t index) {
  Screen.push(new ChameleonSlotEditScreen(index));
}

void ChameleonSlotsScreen::onBack() {
  Screen.goBack();
}
