#include "GameMenuScreen.h"
#include "core/ScreenManager.h"
#include "screens/MainMenuScreen.h"
#include "screens/game/GameDecoderScreen.h"
#include "screens/game/GameWordleScreen.h"
#include "screens/game/GameFlappyScreen.h"
#include "screens/game/GameMemoryScreen.h"
#include "screens/game/GameNumberGuessScreen.h"
#include "screens/game/GameFishingScreen.h"

void GameMenuScreen::onInit()
{
  setItems(_items);
}

void GameMenuScreen::onBack()
{
  Screen.goBack();
}

void GameMenuScreen::onItemSelected(uint8_t index)
{
  switch (index) {
    case 0: Screen.push(new GameDecoderScreen()); break;
    case 1: Screen.push(new GameWordleScreen(GameWordleScreen::LANG_EN)); break;
    case 2: Screen.push(new GameWordleScreen(GameWordleScreen::LANG_ID)); break;
    case 3: Screen.push(new GameFlappyScreen()); break;
    case 4: Screen.push(new GameMemoryScreen()); break;
    case 5: Screen.push(new GameNumberGuessScreen()); break;
    case 6: Screen.push(new GameFishingScreen()); break;
  }
}