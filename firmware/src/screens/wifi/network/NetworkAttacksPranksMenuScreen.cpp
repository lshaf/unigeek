#include "NetworkAttacksPranksMenuScreen.h"
#include "core/ScreenManager.h"
#include "NetworkAttacksScreen.h"
#include "NetworkPranksScreen.h"

void NetworkAttacksPranksMenuScreen::onInit() {
  setItems(_items);
}

void NetworkAttacksPranksMenuScreen::onItemSelected(uint8_t index) {
  switch (index) {
    case 0: Screen.push(new NetworkAttacksScreen()); break;
    case 1: Screen.push(new NetworkPranksScreen());  break;
  }
}

void NetworkAttacksPranksMenuScreen::onBack() {
  Screen.goBack();
}
