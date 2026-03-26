#include "screens/setting/PinSettingScreen.h"
#include "core/Device.h"
#include "core/ScreenManager.h"
#include "core/PinConfigManager.h"
#include "screens/setting/SettingScreen.h"
#include "ui/actions/InputNumberAction.h"

void PinSettingScreen::onInit() {
  _itemCount = 0;

  // GPS pins — always shown
  _items[_itemCount] = {"GPS TX Pin", ""};
  _map[_itemCount] = PIN_GPS_TX;
  _itemCount++;

  _items[_itemCount] = {"GPS RX Pin", ""};
  _map[_itemCount] = PIN_GPS_RX;
  _itemCount++;

  _items[_itemCount] = {"GPS Baud Rate", ""};
  _map[_itemCount] = PIN_GPS_BAUD;
  _itemCount++;

  // External I2C — only when device has ExI2C
  if (Uni.ExI2C) {
    _items[_itemCount] = {"External SDA", ""};
    _map[_itemCount] = PIN_EXT_SDA;
    _itemCount++;

    _items[_itemCount] = {"External SCL", ""};
    _map[_itemCount] = PIN_EXT_SCL;
    _itemCount++;
  }

  setItems(_items, _itemCount);
  _refresh();
}

void PinSettingScreen::_refresh() {
  _gpsTxSub = PinConfig.get(PIN_CONFIG_GPS_TX, PIN_CONFIG_GPS_TX_DEFAULT);
  _gpsRxSub = PinConfig.get(PIN_CONFIG_GPS_RX, PIN_CONFIG_GPS_RX_DEFAULT);
  _gpsBaudSub = PinConfig.get(PIN_CONFIG_GPS_BAUD, PIN_CONFIG_GPS_BAUD_DEFAULT);
  _sdaSub = PinConfig.get(PIN_CONFIG_EXT_SDA, PIN_CONFIG_EXT_SDA_DEFAULT);
  _sclSub = PinConfig.get(PIN_CONFIG_EXT_SCL, PIN_CONFIG_EXT_SCL_DEFAULT);

  for (uint8_t i = 0; i < _itemCount; i++) {
    switch (_map[i]) {
      case PIN_GPS_TX:   _items[i].sublabel = _gpsTxSub.c_str(); break;
      case PIN_GPS_RX:   _items[i].sublabel = _gpsRxSub.c_str(); break;
      case PIN_GPS_BAUD: _items[i].sublabel = _gpsBaudSub.c_str(); break;
      case PIN_EXT_SDA:  _items[i].sublabel = _sdaSub.c_str(); break;
      case PIN_EXT_SCL:  _items[i].sublabel = _sclSub.c_str(); break;
    }
  }

  render();
}

void PinSettingScreen::onItemSelected(uint8_t index) {
  if (index >= _itemCount) return;

  switch (_map[index]) {
    case PIN_GPS_TX: {
      int cur = PinConfig.getInt(PIN_CONFIG_GPS_TX, PIN_CONFIG_GPS_TX_DEFAULT);
      int val = InputNumberAction::popup("GPS TX Pin", 0, 48, cur);
      PinConfig.set(PIN_CONFIG_GPS_TX, String(val));
      PinConfig.save(Uni.Storage);
      break;
    }
    case PIN_GPS_RX: {
      int cur = PinConfig.getInt(PIN_CONFIG_GPS_RX, PIN_CONFIG_GPS_RX_DEFAULT);
      int val = InputNumberAction::popup("GPS RX Pin", 0, 48, cur);
      PinConfig.set(PIN_CONFIG_GPS_RX, String(val));
      PinConfig.save(Uni.Storage);
      break;
    }
    case PIN_GPS_BAUD: {
      int cur = PinConfig.getInt(PIN_CONFIG_GPS_BAUD, PIN_CONFIG_GPS_BAUD_DEFAULT);
      int val = InputNumberAction::popup("GPS Baud Rate", 300, 115200, cur);
      if (val > 0) {
        PinConfig.set(PIN_CONFIG_GPS_BAUD, String(val));
        PinConfig.save(Uni.Storage);
      }
      break;
    }
    case PIN_EXT_SDA: {
      int cur = PinConfig.getInt(PIN_CONFIG_EXT_SDA, PIN_CONFIG_EXT_SDA_DEFAULT);
      int val = InputNumberAction::popup("External SDA Pin", 0, 48, cur);
      PinConfig.set(PIN_CONFIG_EXT_SDA, String(val));
      PinConfig.save(Uni.Storage);
      break;
    }
    case PIN_EXT_SCL: {
      int cur = PinConfig.getInt(PIN_CONFIG_EXT_SCL, PIN_CONFIG_EXT_SCL_DEFAULT);
      int val = InputNumberAction::popup("External SCL Pin", 0, 48, cur);
      PinConfig.set(PIN_CONFIG_EXT_SCL, String(val));
      PinConfig.save(Uni.Storage);
      break;
    }
  }
  _refresh();
}

void PinSettingScreen::onBack() {
  Screen.setScreen(new SettingScreen());
}
