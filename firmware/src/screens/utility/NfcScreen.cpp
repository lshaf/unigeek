#include "NfcScreen.h"
#include "core/ScreenManager.h"
#include "screens/utility/NdefGeneratorScreen.h"
#include "screens/utility/NdefEditorScreen.h"
#include "screens/utility/NfcDumpGeneratorScreen.h"

void NfcScreen::onInit() {
  setItems(_items);
}

void NfcScreen::onItemSelected(uint8_t index) {
  switch (index) {
    case 0:
      Screen.push(new NdefGeneratorScreen());
      break;
    case 1:
      Screen.push(new NdefEditorScreen());
      break;
    case 2:
      Screen.push(new NfcDumpGeneratorScreen());
      break;
  }
}
