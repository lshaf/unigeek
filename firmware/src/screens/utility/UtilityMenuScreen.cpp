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
#include "screens/utility/PomodoroScreen.h"
#include "screens/utility/RandomLinePickerScreen.h"
#include "screens/utility/LuaRunnerScreen.h"

void UtilityMenuScreen::onInit() {
  setItems(_items);
}

void UtilityMenuScreen::onBack() {
  Screen.goBack();
}

void UtilityMenuScreen::onItemSelected(uint8_t index) {
  switch (index) {
    case 0: Screen.push(new I2CDetectorScreen());  break;
    case 1: Screen.push(new QRCodeScreen());        break;
    case 2: Screen.push(new BarcodeScreen());       break;
    case 3: Screen.push(new FileManagerScreen());   break;
    case 4: Screen.push(new AchievementScreen());   break;
    case 5: Screen.push(new TotpScreen());          break;
    case 6: Screen.push(new UartTerminalScreen());  break;
    case 7: Screen.push(new PomodoroScreen());           break;
    case 8: Screen.push(new RandomLinePickerScreen());  break;
    case 9: Screen.push(new LuaRunnerScreen());         break;
  }
}