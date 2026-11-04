#include "NetworkToolboxScreen.h"
#include "core/ScreenManager.h"
#include "ConnectivityCheckScreen.h"
#include "PingScreen.h"
#include "TracerouteScreen.h"
#include "DnsLookupScreen.h"
#include "HttpRequestScreen.h"
#include "WakeOnLanScreen.h"

void NetworkToolboxScreen::onInit() {
  setItems(_items);
}

void NetworkToolboxScreen::onItemSelected(uint8_t index) {
  switch (index) {
    case 0: Screen.push(new ConnectivityCheckScreen()); break;
    case 1: Screen.push(new PingScreen()); break;
    case 2: Screen.push(new TracerouteScreen()); break;
    case 3: Screen.push(new DnsLookupScreen()); break;
    case 4: Screen.push(new HttpRequestScreen()); break;
    case 5: Screen.push(new WakeOnLanScreen()); break;
  }
}
