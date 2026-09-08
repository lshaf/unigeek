#include "WifiCaptureMenuScreen.h"
#include "core/ScreenManager.h"
#include "WifiEapolScreen.h"
void WifiCaptureMenuScreen::onInit() { setItems(_items); }
void WifiCaptureMenuScreen::onItemSelected(uint8_t index) {
  switch (index) {
    case 0: break; // TODO: Raw Packets
    case 1: Screen.push(new WifiEapolScreen()); break;
  }
}
void WifiCaptureMenuScreen::onBack() { Screen.goBack(); }
