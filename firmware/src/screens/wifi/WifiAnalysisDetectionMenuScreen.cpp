#include "WifiAnalysisDetectionMenuScreen.h"
#include "core/ScreenManager.h"
#include "WifiAnalyzerScreen.h"
#include "WifiWatchdogScreen.h"
#include "karma/WifiKarmaDetectorScreen.h"

void WifiAnalysisDetectionMenuScreen::onInit() {
  setItems(_items);
}

void WifiAnalysisDetectionMenuScreen::onItemSelected(uint8_t index) {
  switch (index) {
    case 0: Screen.push(new WifiAnalyzerScreen()); break;
    case 1: Screen.push(new WifiWatchdogScreen()); break;
    case 2: Screen.push(new WifiWatchdogScreen(WifiWatchdogScreen::InitialView::Deauth)); break;
    case 3: Screen.push(new WifiWatchdogScreen(WifiWatchdogScreen::InitialView::Flood)); break;
    case 4: Screen.push(new WifiWatchdogScreen(WifiWatchdogScreen::InitialView::EvilTwin)); break;
    case 5: Screen.push(new WifiKarmaDetectorScreen()); break;
  }
}

void WifiAnalysisDetectionMenuScreen::onBack() {
  Screen.goBack();
}
