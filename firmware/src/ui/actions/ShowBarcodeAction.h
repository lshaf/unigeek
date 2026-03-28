//
// ShowBarcodeAction.h — render a Code 128 barcode and block until dismissed.
//

#pragma once

#include "core/Device.h"
#include "ui/components/BarcodeRenderer.h"

class ShowBarcodeAction
{
public:
  // Renders a barcode for `content` with `label` above it.
  // Blocks until BACK/PRESS. DOWN/UP toggles inverted colors.
  // Wipes the area on exit.
  static void show(const char* label, const char* content, bool inverted = false) {
    BarcodeRenderer::draw(label, content, inverted);

    for (;;) {
      Uni.update();
#ifdef DEVICE_HAS_KEYBOARD
      if (Uni.Keyboard && Uni.Keyboard->available()) {
        Uni.Keyboard->getKey();
        break;
      }
#endif
      if (Uni.Nav->wasPressed()) {
        auto dir = Uni.Nav->readDirection();
        if (dir == INavigation::DIR_DOWN || dir == INavigation::DIR_UP) {
          inverted = !inverted;
          BarcodeRenderer::draw(label, content, inverted);
        } else {
          break;
        }
      }
      delay(10);
    }

    BarcodeRenderer::clear();
  }
};

