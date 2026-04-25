#include "screens/setting/PinSettingScreen.h"
#include "core/Device.h"
#include "core/ScreenManager.h"
#include "core/PinConfigManager.h"
#include "core/AchievementManager.h"
#include "screens/setting/SettingScreen.h"
#include "ui/actions/InputNumberAction.h"
#include "ui/actions/InputSelectOption.h"


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

  // CC1101
  _items[_itemCount] = {"CC1101 CS Pin", ""};
  _map[_itemCount] = PIN_CC1101_CS;
  _itemCount++;

  _items[_itemCount] = {"CC1101 GDO0 Pin", ""};
  _map[_itemCount] = PIN_CC1101_GDO0;
  _itemCount++;

  _items[_itemCount] = {"NRF24 CS Pin", ""};
  _map[_itemCount] = PIN_NRF24_CE;
  _itemCount++;

  _items[_itemCount] = {"NRF24 GDO0 Pin", ""};
  _map[_itemCount] = PIN_NRF24_CSN;
  _itemCount++;

  // PN532 (HSU/UART) — always shown; menu hides itself when TX < 0
  _items[_itemCount] = {"PN532 TX Pin", ""};
  _map[_itemCount] = PIN_PN532_TX;
  _itemCount++;

  _items[_itemCount] = {"PN532 RX Pin", ""};
  _map[_itemCount] = PIN_PN532_RX;
  _itemCount++;

  _items[_itemCount] = {"PN532 Baud Rate", ""};
  _map[_itemCount] = PIN_PN532_BAUD;
  _itemCount++;

#if defined(DEVICE_M5_CORES3)
  // Bare CoreS3 only — Grove Port A 5V direction (input/output)
  _items[_itemCount] = {"Grove A 5V", ""};
  _map[_itemCount] = PIN_CORES3_GROVE_5V;
  _itemCount++;
#endif

  setItems(_items, _itemCount);
  _refresh();
}

void PinSettingScreen::_refresh() {
  _gpsTxSub = PinConfig.get(PIN_CONFIG_GPS_TX, PIN_CONFIG_GPS_TX_DEFAULT);
  _gpsRxSub = PinConfig.get(PIN_CONFIG_GPS_RX, PIN_CONFIG_GPS_RX_DEFAULT);
  _gpsBaudSub = PinConfig.get(PIN_CONFIG_GPS_BAUD, PIN_CONFIG_GPS_BAUD_DEFAULT);
  _sdaSub = PinConfig.get(PIN_CONFIG_EXT_SDA, PIN_CONFIG_EXT_SDA_DEFAULT);
  _sclSub = PinConfig.get(PIN_CONFIG_EXT_SCL, PIN_CONFIG_EXT_SCL_DEFAULT);
  _cc1101CsSub = PinConfig.get(PIN_CONFIG_CC1101_CS, PIN_CONFIG_CC1101_CS_DEFAULT);
  _cc1101Gdo0Sub = PinConfig.get(PIN_CONFIG_CC1101_GDO0, PIN_CONFIG_CC1101_GDO0_DEFAULT);
  _nrf24CeSub = PinConfig.get(PIN_CONFIG_NRF24_CE, PIN_CONFIG_NRF24_CE_DEFAULT);
  _nrf24CsnSub = PinConfig.get(PIN_CONFIG_NRF24_CSN, PIN_CONFIG_NRF24_CSN_DEFAULT);
  _pn532TxSub = PinConfig.get(PIN_CONFIG_PN532_TX, PIN_CONFIG_PN532_TX_DEFAULT);
  _pn532RxSub = PinConfig.get(PIN_CONFIG_PN532_RX, PIN_CONFIG_PN532_RX_DEFAULT);
  _pn532BaudSub = PinConfig.get(PIN_CONFIG_PN532_BAUD, PIN_CONFIG_PN532_BAUD_DEFAULT);
  _grove5VSub = PinConfig.get(PIN_CONFIG_CORES3_GROVE_5V, PIN_CONFIG_CORES3_GROVE_5V_DEFAULT);

  for (uint8_t i = 0; i < _itemCount; i++) {
    switch (_map[i]) {
      case PIN_GPS_TX:      _items[i].sublabel = _gpsTxSub.c_str(); break;
      case PIN_GPS_RX:      _items[i].sublabel = _gpsRxSub.c_str(); break;
      case PIN_GPS_BAUD:    _items[i].sublabel = _gpsBaudSub.c_str(); break;
      case PIN_EXT_SDA:     _items[i].sublabel = _sdaSub.c_str(); break;
      case PIN_EXT_SCL:     _items[i].sublabel = _sclSub.c_str(); break;
      case PIN_CC1101_CS:   _items[i].sublabel = _cc1101CsSub.c_str(); break;
      case PIN_CC1101_GDO0: _items[i].sublabel = _cc1101Gdo0Sub.c_str(); break;
      case PIN_NRF24_CE:    _items[i].sublabel = _nrf24CeSub.c_str(); break;
      case PIN_NRF24_CSN:   _items[i].sublabel = _nrf24CsnSub.c_str(); break;
      case PIN_PN532_TX:    _items[i].sublabel = _pn532TxSub.c_str(); break;
      case PIN_PN532_RX:    _items[i].sublabel = _pn532RxSub.c_str(); break;
      case PIN_PN532_BAUD:  _items[i].sublabel = _pn532BaudSub.c_str(); break;
      case PIN_CORES3_GROVE_5V: _items[i].sublabel = _grove5VSub.c_str(); break;
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
      if (!InputNumberAction::wasCancelled()) {
        PinConfig.set(PIN_CONFIG_GPS_TX, String(val));
        PinConfig.save(Uni.Storage);
      }
      break;
    }
    case PIN_GPS_RX: {
      int cur = PinConfig.getInt(PIN_CONFIG_GPS_RX, PIN_CONFIG_GPS_RX_DEFAULT);
      int val = InputNumberAction::popup("GPS RX Pin", 0, 48, cur);
      if (!InputNumberAction::wasCancelled()) {
        PinConfig.set(PIN_CONFIG_GPS_RX, String(val));
        PinConfig.save(Uni.Storage);
      }
      break;
    }
    case PIN_GPS_BAUD: {
      int cur = PinConfig.getInt(PIN_CONFIG_GPS_BAUD, PIN_CONFIG_GPS_BAUD_DEFAULT);
      int val = InputNumberAction::popup("GPS Baud Rate", 300, 115200, cur);
      if (!InputNumberAction::wasCancelled()) {
        PinConfig.set(PIN_CONFIG_GPS_BAUD, String(val));
        PinConfig.save(Uni.Storage);
      }
      break;
    }
    case PIN_EXT_SDA: {
      int cur = PinConfig.getInt(PIN_CONFIG_EXT_SDA, PIN_CONFIG_EXT_SDA_DEFAULT);
      int val = InputNumberAction::popup("External SDA Pin", 0, 48, cur);
      if (!InputNumberAction::wasCancelled()) {
        PinConfig.set(PIN_CONFIG_EXT_SDA, String(val));
        PinConfig.save(Uni.Storage);
      }
      break;
    }
    case PIN_EXT_SCL: {
      int cur = PinConfig.getInt(PIN_CONFIG_EXT_SCL, PIN_CONFIG_EXT_SCL_DEFAULT);
      int val = InputNumberAction::popup("External SCL Pin", 0, 48, cur);
      if (!InputNumberAction::wasCancelled()) {
        PinConfig.set(PIN_CONFIG_EXT_SCL, String(val));
        PinConfig.save(Uni.Storage);
      }
      break;
    }
    case PIN_CC1101_CS: {
      int cur = PinConfig.getInt(PIN_CONFIG_CC1101_CS, PIN_CONFIG_CC1101_CS_DEFAULT);
      int val = InputNumberAction::popup("CC1101 CS Pin", 0, 48, cur);
      if (!InputNumberAction::wasCancelled()) {
        PinConfig.set(PIN_CONFIG_CC1101_CS, String(val));
        PinConfig.save(Uni.Storage);
      }
      break;
    }
    case PIN_CC1101_GDO0: {
      int cur = PinConfig.getInt(PIN_CONFIG_CC1101_GDO0, PIN_CONFIG_CC1101_GDO0_DEFAULT);
      int val = InputNumberAction::popup("CC1101 GDO0 Pin", 0, 48, cur);
      if (!InputNumberAction::wasCancelled()) {
        PinConfig.set(PIN_CONFIG_CC1101_GDO0, String(val));
        PinConfig.save(Uni.Storage);
      }
      break;
    }
    case PIN_NRF24_CE: {
      int cur = PinConfig.getInt(PIN_CONFIG_NRF24_CE, PIN_CONFIG_NRF24_CE_DEFAULT);
      int val = InputNumberAction::popup("NRF24 CS Pin", 0, 48, cur);
      if (!InputNumberAction::wasCancelled()) {
        PinConfig.set(PIN_CONFIG_NRF24_CE, String(val));
        PinConfig.save(Uni.Storage);
      }
      break;
    }
    case PIN_NRF24_CSN: {
      int cur = PinConfig.getInt(PIN_CONFIG_NRF24_CSN, PIN_CONFIG_NRF24_CSN_DEFAULT);
      int val = InputNumberAction::popup("NRF24 GDO0 Pin", 0, 48, cur);
      if (!InputNumberAction::wasCancelled()) {
        PinConfig.set(PIN_CONFIG_NRF24_CSN, String(val));
        PinConfig.save(Uni.Storage);
      }
      break;
    }
    case PIN_PN532_TX: {
      int cur = PinConfig.getInt(PIN_CONFIG_PN532_TX, PIN_CONFIG_PN532_TX_DEFAULT);
      int val = InputNumberAction::popup("PN532 TX Pin", -1, 48, cur);
      if (!InputNumberAction::wasCancelled()) {
        PinConfig.set(PIN_CONFIG_PN532_TX, String(val));
        PinConfig.save(Uni.Storage);
      }
      break;
    }
    case PIN_PN532_RX: {
      int cur = PinConfig.getInt(PIN_CONFIG_PN532_RX, PIN_CONFIG_PN532_RX_DEFAULT);
      int val = InputNumberAction::popup("PN532 RX Pin", -1, 48, cur);
      if (!InputNumberAction::wasCancelled()) {
        PinConfig.set(PIN_CONFIG_PN532_RX, String(val));
        PinConfig.save(Uni.Storage);
      }
      break;
    }
    case PIN_PN532_BAUD: {
      int cur = PinConfig.getInt(PIN_CONFIG_PN532_BAUD, PIN_CONFIG_PN532_BAUD_DEFAULT);
      int val = InputNumberAction::popup("PN532 Baud Rate", 9600, 921600, cur);
      if (!InputNumberAction::wasCancelled()) {
        PinConfig.set(PIN_CONFIG_PN532_BAUD, String(val));
        PinConfig.save(Uni.Storage);
      }
      break;
    }
    case PIN_CORES3_GROVE_5V: {
      static const InputSelectAction::Option opts[] = {
        { "Power Out", "output" },
        { "Power In",  "input"  },
      };
      String cur = PinConfig.get(PIN_CONFIG_CORES3_GROVE_5V, PIN_CONFIG_CORES3_GROVE_5V_DEFAULT);
      const char* sel = InputSelectAction::popup("Grove A 5V", opts, 2, cur.c_str());
      if (sel) {
        PinConfig.set(PIN_CONFIG_CORES3_GROVE_5V, String(sel));
        PinConfig.save(Uni.Storage);
        Uni.onPinConfigApply();
      }
      break;
    }
  }
  int n = Achievement.inc("settings_pin_configured");
  if (n == 1) Achievement.unlock("settings_pin_configured");

  _refresh();
}

void PinSettingScreen::onBack() {
  Screen.goBack();
}
