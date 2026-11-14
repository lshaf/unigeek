#include "NetworkAttacksPranksMenuScreen.h"
#include "core/ScreenManager.h"
#include "NetworkMitmScreen.h"
#include "DhcpAttackScreen.h"
#include "CastBombScreen.h"
#include "BonjourSpamScreen.h"
#include "PrinterPrankScreen.h"
#ifdef HAS_NET_TOOLS
#include "ResponderScreen.h"
#endif

void NetworkAttacksPranksMenuScreen::onInit() {
  setItems(_items);
}

void NetworkAttacksPranksMenuScreen::onItemSelected(uint8_t index) {
#ifdef HAS_NET_TOOLS
  switch (index) {
    case 0: Screen.push(new NetworkMitmScreen());  break;
    case 1: Screen.push(new DhcpAttackScreen());   break;
    case 2: Screen.push(new ResponderScreen());    break;
    case 3: Screen.push(new CastBombScreen());     break;
    case 4: Screen.push(new BonjourSpamScreen());  break;
    case 5: Screen.push(new PrinterPrankScreen()); break;
  }
#else
  switch (index) {
    case 0: Screen.push(new NetworkMitmScreen());  break;
    case 1: Screen.push(new DhcpAttackScreen());   break;
    case 2: Screen.push(new CastBombScreen());     break;
    case 3: Screen.push(new BonjourSpamScreen());  break;
    case 4: Screen.push(new PrinterPrankScreen()); break;
  }
#endif
}

void NetworkAttacksPranksMenuScreen::onBack() {
  Screen.goBack();
}
