#include "ChameleonDeviceScreen.h"
#include "utils/ble/ChameleonClient.h"
#include "ChameleonMenuScreen.h"
#include "core/Device.h"
#include "core/ScreenManager.h"
#include "core/AchievementManager.h"
#include "core/ConfigManager.h"
#include "ui/actions/ShowStatusAction.h"

void ChameleonDeviceScreen::_load() {
  static constexpr const char* kLabels[kFields] = {
    "Device", "Firmware", "Chip ID", "Battery", "Mode", "Active Slot"
  };

  for (int i = 0; i < kFields; i++) {
    _rowLabels[i] = kLabels[i];
    _rowValues[i] = "--";
    _rows[i] = {_rowLabels[i].c_str(), _rowValues[i]};
  }

  auto& c = ChameleonClient::get();
  if (!c.isConnected()) {
    _rowValues[0] = "Not connected";
    _rows[0] = {_rowLabels[0].c_str(), _rowValues[0]};
    _scrollView.setRows(_rows, kFields);
    return;
  }

  uint8_t type = 0;
  if (c.getDeviceType(&type))
    _rowValues[0] = (type == 0 ? "Chameleon Ultra" : "Chameleon Lite");

  char buf[32] = {};
  if (c.getVersion(buf, sizeof(buf)))   _rowValues[1] = buf;

  memset(buf, 0, sizeof(buf));
  if (c.getChipId(buf, sizeof(buf)))    _rowValues[2] = buf;

  uint8_t pct = 0; uint16_t mv = 0;
  if (c.getBattery(&pct, &mv))          _rowValues[3] = String(pct) + "% (" + mv + "mV)";

  uint8_t mode = 0;
  if (c.getMode(&mode))                 _rowValues[4] = (mode == 1 ? "Reader" : "Emulator");

  uint8_t slot = 0;
  if (c.getActiveSlot(&slot))           _rowValues[5] = String(slot + 1);

  for (int i = 0; i < kFields; i++)
    _rows[i] = {_rowLabels[i].c_str(), _rowValues[i]};

  _scrollView.setRows(_rows, kFields);

  int n = Achievement.inc("chameleon_device_info");
  if (n == 1) Achievement.unlock("chameleon_device_info");
}

void ChameleonDeviceScreen::onInit() {
  ShowStatusAction::show("Loading...", 0);
  _load();
}

void ChameleonDeviceScreen::onUpdate() {
  if (Uni.Nav->wasPressed()) {
    auto dir = Uni.Nav->readDirection();
    if (dir == INavigation::DIR_BACK) {
      Screen.goBack();
      return;
    }
    _scrollView.onNav(dir);
  }
}

void ChameleonDeviceScreen::onRender() {
  _scrollView.render(bodyX(), bodyY(), bodyW(), bodyH());
}
