#include "screens/setting/HideModuleScreen.h"
#include "core/Device.h"
#include "core/ConfigManager.h"
#include "core/ScreenManager.h"

void HideModuleScreen::onInit() {
  for (uint8_t i = 0; i < ModuleRegistry::MOD_COUNT; i++) {
    _items[i].label    = ModuleRegistry::LABELS[i];
    _items[i].sublabel = nullptr;
  }
  setItems(_items);
  _refresh();
}

void HideModuleScreen::_refresh() {
  for (uint8_t i = 0; i < ModuleRegistry::MOD_COUNT; i++) {
    _subs[i]           = ModuleRegistry::isHidden(i) ? "Hidden" : "Shown";
    _items[i].sublabel = _subs[i].c_str();
  }
  render();
}

void HideModuleScreen::onItemSelected(uint8_t index) {
  if (index >= ModuleRegistry::MOD_COUNT) return;
  ModuleRegistry::setHidden(index, !ModuleRegistry::isHidden(index));
  Config.save(Uni.Storage);
  _refresh();
}

void HideModuleScreen::onBack() {
  Screen.goBack();
}
