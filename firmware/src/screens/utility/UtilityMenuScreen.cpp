#include "UtilityMenuScreen.h"
#include "core/ScreenManager.h"
#include "screens/MainMenuScreen.h"
#include "screens/utility/I2CDetectorScreen.h"
#include "screens/utility/QRCodeScreen.h"
#include "screens/utility/BarcodeScreen.h"
#include "screens/utility/FileManagerScreen.h"
#include "screens/utility/ImgFileManagerScreen.h"
#ifndef BOARD_4MB
#include "screens/utility/NfcScreen.h"
#endif
#include "screens/utility/AchievementScreen.h"
#include "screens/utility/TotpScreen.h"
#include "screens/utility/UartTerminalScreen.h"
#include "screens/wifi/WifiESPNowChatScreen.h"
#include "screens/utility/PomodoroScreen.h"
#include "screens/utility/RandomLinePickerScreen.h"
#include "screens/wifi/network/WikipediaScreen.h"
#ifdef DEVICE_HAS_WEBAUTHN
#include "screens/utility/webauthn/WebAuthnManageScreen.h"
#endif

void UtilityMenuScreen::onInit() {
  setItems(_items);
}

void UtilityMenuScreen::onBack() {
  Screen.goBack();
}

void UtilityMenuScreen::onItemSelected(uint8_t index) {
  uint8_t i = 0;
  if (index == i++) { Screen.push(new FileManagerScreen());       return; }
  if (index == i++) { Screen.push(new ImgFileManagerScreen());    return; }
#ifndef BOARD_4MB
  if (index == i++) { Screen.push(new NfcScreen());               return; }
#endif
  if (index == i++) { Screen.push(new I2CDetectorScreen());       return; }
  if (index == i++) { Screen.push(new UartTerminalScreen());      return; }
  if (index == i++) { Screen.push(new WifiESPNowChatScreen());    return; }
  if (index == i++) { Screen.push(new QRCodeScreen());            return; }
  if (index == i++) { Screen.push(new BarcodeScreen());           return; }
  if (index == i++) { Screen.push(new TotpScreen());              return; }
#ifdef DEVICE_HAS_WEBAUTHN
  if (index == i++) { Screen.push(new WebAuthnManageScreen());    return; }
#endif
  if (index == i++) { Screen.push(new RandomLinePickerScreen());  return; }
  if (index == i++) { Screen.push(new PomodoroScreen());          return; }
  if (index == i++) { Screen.push(new WikipediaScreen(true));     return; }
  if (index == i++) { Screen.push(new AchievementScreen());       return; }
}
