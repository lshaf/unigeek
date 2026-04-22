#include "UtilityMenuScreen.h"
#include "core/ScreenManager.h"
#include "screens/MainMenuScreen.h"
#include "screens/utility/I2CDetectorScreen.h"
#include "screens/utility/QRCodeScreen.h"
#include "screens/utility/BarcodeScreen.h"
#include "screens/utility/FileManagerScreen.h"
#include "screens/utility/AchievementScreen.h"
#include "screens/utility/TotpScreen.h"
#include "screens/utility/UartTerminalScreen.h"

void UtilityMenuScreen::onInit() {
  setItems(_items);
}

void UtilityMenuScreen::onBack() {
  Screen.setScreen(new MainMenuScreen());
}

void UtilityMenuScreen::onItemSelected(uint8_t index) {
  switch (index) {
    case 0: Screen.setScreen(new I2CDetectorScreen());  break;
    case 1: Screen.setScreen(new QRCodeScreen());        break;
    case 2: Screen.setScreen(new BarcodeScreen());       break;
    case 3: Screen.setScreen(new FileManagerScreen());   break;
    case 4: Screen.setScreen(new AchievementScreen());   break;
    case 5: Screen.setScreen(new TotpScreen());          break;
    case 6: Screen.setScreen(new UartTerminalScreen());  break;
  }
}