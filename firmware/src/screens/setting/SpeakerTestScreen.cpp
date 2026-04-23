#include "screens/setting/SpeakerTestScreen.h"
#include "screens/setting/SettingScreen.h"
#include "core/Device.h"
#include "core/ScreenManager.h"

void SpeakerTestScreen::onInit() {
  setItems(_items);
}

void SpeakerTestScreen::onItemSelected(uint8_t index) {
  if (!Uni.Speaker) return;
  switch (index) {
    case 0: Uni.Speaker->playWin();          break;
    case 1: Uni.Speaker->playLose();         break;
    case 2: Uni.Speaker->playNotification(); break;
    case 3: Uni.Speaker->playRandomTone();   break;
  }
}

void SpeakerTestScreen::onBack() {
  Screen.goBack();
}