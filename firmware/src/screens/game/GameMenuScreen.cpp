#include "GameMenuScreen.h"
#include "core/ScreenManager.h"
#include "screens/MainMenuScreen.h"
#include "screens/game/GameDecoderScreen.h"
#include "screens/game/GameWordleScreen.h"
#include "screens/game/GameFlappyScreen.h"

void GameMenuScreen::onInit()
{
  setItems(_items);
}

void GameMenuScreen::onBack()
{
  Screen.setScreen(new MainMenuScreen());
}

void GameMenuScreen::onItemSelected(uint8_t index)
{
  switch (index) {
    case 0: Screen.setScreen(new GameDecoderScreen()); break;
    case 1: Screen.setScreen(new GameWordleScreen(GameWordleScreen::LANG_EN)); break;
    case 2: Screen.setScreen(new GameWordleScreen(GameWordleScreen::LANG_ID)); break;
    case 3: Screen.setScreen(new GameFlappyScreen()); break;
  }
}