// cpp
#include <Arduino.h>
#include <esp_heap_caps.h>
#include <esp32-hal-psram.h>
extern "C" {
  #include "mbedtls/platform.h"
}

#include "core/Device.h"
#include "core/ScreenManager.h"
#include "core/ConfigManager.h"
#include "core/PinConfigManager.h"
#include "core/AchievementStorage.h"
#include "core/AchievementManager.h"
#include "core/RtcManager.h"
#include "core/RandomSeed.h"
#include "utils/uart/UartFileManager.h"
#include "utils/uart/BleFileManager.h"

#include "screens/CharacterScreen.h"
#ifdef DEVICE_HAS_TOUCH_NAV
#include "screens/setting/TouchGuideScreen.h"
#endif
#ifdef DEVICE_CYD
#include "screens/setting/CYDTouchCalScreen.h"
#endif

void _bootSplash() {
  // ── Pre-draw init (config needed for theme colour) ────────────────────────
  Config.load(Uni.Storage);
  PinConfig.load(Uni.Storage);
  Uni.onPinConfigApply();
#ifdef DEVICE_CYD
  if (Uni.Nav)
    Uni.Nav->setTouchSwapXY(Config.get(APP_CONFIG_TOUCH_SWAP_XY, APP_CONFIG_TOUCH_SWAP_XY_DEFAULT) == "1");
#endif
  Uni.applyOrientation();   // rotate screen + flip nav before any drawing
  Uni.Lcd.setBrightness((uint8_t)Config.get(APP_CONFIG_BRIGHTNESS, APP_CONFIG_BRIGHTNESS_DEFAULT).toInt());

  // ── Splash UI ─────────────────────────────────────────────────────────────
  // A pixel-art UniGeek "U" rendered from a 32×32 base. The body is solid (no
  // gaps), built into a 32×32 sprite then nearest-neighbour scaled into a
  // screen-fit sprite. The detached top-right blocks (a 2×3 zone) stay separated
  // and blink between two diagonal phases:  .# / #. / .#  ⇄  #. / .# / #.
  // A progress bar + "loading" label sit below. Shows for exactly 2 s.
  auto& lcd = Uni.Lcd;
  const uint16_t w = lcd.width();
  const uint16_t h = lcd.height();
  const uint16_t accent = Config.getThemeColor();

  lcd.fillScreen(TFT_BLACK);

  // 32×32 U body (bit 31 = leftmost column). The top-right zone x[20,30) y[0,15)
  // is cleared here — it's owned by the animated separated blocks.
  static const uint32_t U_BODY[32] = {
    0x00000000, 0x00000000, 0x00000000, 0x00000000,
    0x3FC00000, 0x3FC00000, 0x3FC00000, 0x3FC00000,
    0x3FC00000, 0x3FC00000, 0x3FC00000, 0x3FC00000,
    0x3FC007F8, 0x3FC007F8, 0x3FC007F8, 0x3FC007F8,
    0x3FC007F8, 0x3FC007F8, 0x3FC007F8, 0x3FC007F8,
    0x3FC007F8, 0x3FC007F8, 0x3FC007F8, 0x3FF83FF8,
    0x3FF83FF8, 0x07FFFFC0, 0x07FFFFC0, 0x07FFFFC0,
    0x00FFFE00, 0x007FFC00, 0x007FFC00, 0x007FFC00,
  };

  // baseOn(bx,by,phase) — is base pixel (bx,by) lit for this blink phase?
  // The top-right zigzag is 3 blocks of 4×4 at the mark's real positions:
  // cols 21-24 (left) / 25-28 (right), rows 0-3 / 4-7 / 8-11.
  //   phase 0 (matches the mark): right / left / right
  //   phase 1 (mirror):           left  / right / left
  auto baseOn = [&](int bx, int by, uint8_t phase) -> bool {
    if (bx >= 21 && bx < 29 && by < 12) {
      int litCol = (((by / 4) & 1) == 0) ? 1 : 0;
      if (phase) litCol ^= 1;
      return ((bx - 21) / 4) == litCol;
    }
    return (U_BODY[by] & (0x80000000u >> bx)) != 0;
  };

  // Screen-fit square; capped so the view sprite stays a sane size on big panels.
  const int16_t grid = constrain(min((int)(h * 0.40f), (int)(w * 0.60f)), 48, 120);
  const int16_t gx   = (w - grid) / 2;
  const int16_t gy   = (int16_t)(h * 0.38f) - grid / 2;

  Sprite base(&lcd), view(&lcd);
  const bool haveBase = base.createSprite(32, 32);
  const bool haveView = haveBase && view.createSprite(grid, grid);

  // showLogo(phase) — paint the 32×32 base, NN-scale it to the view, push it.
  auto showLogo = [&](uint8_t phase) {
    if (haveView) {
      base.fillSprite(TFT_BLACK);
      for (int by = 0; by < 32; by++)
        for (int bx = 0; bx < 32; bx++)
          if (baseOn(bx, by, phase)) base.drawPixel(bx, by, accent);
      for (int16_t y = 0; y < grid; y++)
        for (int16_t x = 0; x < grid; x++)
          view.drawPixel(x, y, base.readPixel(x * 32 / grid, y * 32 / grid));
      view.pushSprite(gx, gy);
    } else {  // low-memory fallback: tile straight to the LCD (still no gaps)
      for (int by = 0; by < 32; by++) {
        int16_t y0 = gy + by * grid / 32, y1 = gy + (by + 1) * grid / 32;
        for (int bx = 0; bx < 32; bx++) {
          int16_t x0 = gx + bx * grid / 32, x1 = gx + (bx + 1) * grid / 32;
          lcd.fillRect(x0, y0, x1 - x0, y1 - y0, baseOn(bx, by, phase) ? accent : TFT_BLACK);
        }
      }
    }
  };

  // ── Progress bar + "loading" label below the mark ──────────────────────────
  const int16_t barW  = constrain((int)(w * 0.50f), 60, 220);
  const int16_t barH  = 4;
  const int16_t barX  = (w - barW) / 2;
  const int16_t barY  = gy + grid + constrain((int)(h * 0.08f), 12, 36);
  const int16_t fillW = barW - 2;
  const int16_t lblY  = barY + barH + 6;
  lcd.drawRoundRect(barX, barY, barW, barH, 1, TFT_DARKGREY);

  // ── Animate for 2 s, gating the real init steps onto the timeline ───────────
  const uint32_t SPLASH_MS = 2000;
  const uint32_t BLINK_MS  = 320;            // top-right phase toggle interval
  int8_t      initStep  = 0;
  uint8_t     pctPrev   = 255;
  int8_t      phasePrev = -1;
  const char* label     = "Config loaded";   // config is already loaded pre-draw
  const char* labelPrev = nullptr;
  const uint32_t t0 = millis();
  for (;;) {
    uint32_t el = millis() - t0;
    const bool done = el >= SPLASH_MS;
    if (done) el = SPLASH_MS;

    // Real init work — same steps/order as before, paced across the splash; the
    // label under the bar tracks the step that just completed.
    if (initStep == 0 && el >= 150)  { AchStore.load(Uni.Storage);                 initStep = 1; label = "Achievements loaded"; }
    else if (initStep == 1 && el >= 650)  { Achievement.recalibrate(Uni.Storage);  initStep = 2; label = "EXP calibrated"; }
    else if (initStep == 2 && el >= 1100) { RandomSeed::init(); Uni.applyNavMode(); initStep = 3; label = "System ready"; }
    else if (initStep == 3 && el >= 1550) {
      if (Uni.Speaker) {
        Uni.Speaker->setVolume((uint8_t)Config.get(APP_CONFIG_VOLUME, APP_CONFIG_VOLUME_DEFAULT).toInt());
        Uni.Speaker->playWin();
      }
      initStep = 4;
      label = "Starting...";
    }

    // Status label — clear the row and redraw when the step changes.
    if (label != labelPrev) {
      lcd.fillRect(0, lblY, w, 12, TFT_BLACK);
      lcd.setTextDatum(TC_DATUM);
      lcd.setTextSize(1);
      lcd.setTextColor(TFT_DARKGREY, TFT_BLACK);
      lcd.drawString(label, w / 2, lblY + 2);
      labelPrev = label;
    }

    // Blink the top-right blocks between their two phases (re-scale + push).
    int8_t phase = (int8_t)((el / BLINK_MS) & 1);
    if (phase != phasePrev) { showLogo((uint8_t)phase); phasePrev = phase; }

    // Progress bar tracks the 2 s timeline.
    uint8_t pct = (uint8_t)(el * 100UL / SPLASH_MS);
    if (pct != pctPrev) {
      lcd.fillRect(barX + 1, barY + 1, (int32_t)fillW * pct / 100, barH - 2, accent);
      pctPrev = pct;
    }

    if (done) break;
    delay(16);
  }

  if (haveView) view.deleteSprite();
  if (haveBase) base.deleteSprite();
}

// ── mbedTLS allocator override (PSRAM boards only) ───────────────────────────
//
// mbedTLS's rx buffer wants a contiguous ~16 KB block. Once the Lua VM (or any
// busy subsystem) has run, internal SRAM is fragmented even though total free
// is plenty — TLS handshake fails with -32512 (MBEDTLS_ERR_SSL_ALLOC_FAILED).
//
// On PSRAM boards (cores3, sticks3) we route every mbedTLS alloc to PSRAM.
// 8 MB of empty external RAM dodges fragmentation entirely; PSRAM cache
// contention with WiFi isn't latency-critical for TLS.
//
// On no-PSRAM boards (cardputer_adv, cardputer, stickcplus_*) there's no
// clean fix: any reservation we make in internal SRAM competes with the
// WiFi driver's own 40 KB DMA-capable allocation, and starving WiFi is
// worse than failing TLS. HTTPS from Lua scripts on no-PSRAM boards may
// fail under heap pressure — see lua-runner.md.

static void* _mbedtlsPsramCalloc(size_t n, size_t size) {
  void* p = heap_caps_calloc(n, size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (!p) p = heap_caps_calloc(n, size, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  return p;
}
static void _mbedtlsPsramFree(void* p) {
  heap_caps_free(p);
}

void setup() {
  // Default 256-byte RX FIFO for now — only grown below if the Serial File
  // Manager is enabled, since that's the only consumer that needs it.
  Serial.begin(115200);

  if (psramFound()) {
    mbedtls_platform_set_calloc_free(_mbedtlsPsramCalloc, _mbedtlsPsramFree);
  }

  Uni.begin();
  Uni.initStorage();
  Config.load(Uni.Storage);   // load early so optional services can be gated

  // Optional always-on USB serial services, each disabled by default-aware
  // config so its SRAM is only claimed when on (meaningful on no-PSRAM boards):
  //   Serial File Manager (ctx 'F') — ~8 KB core + needs a 4 KB RX FIFO because
  //     it streams up to ~1 KB per frame; the 256-byte default would overflow
  //     before loop() drains it. Resizing needs a Serial restart (setRxBufferSize
  //     is ignored once the driver is installed).
  //   Screen Mirror (ctx 'S') — off by default; tiny RX, +~8 KB band only while
  //     actively streaming. Only receives short commands, so no RX growth needed.
  bool fmOn     = Config.get(APP_CONFIG_SERIAL_FM, APP_CONFIG_SERIAL_FM_DEFAULT).toInt();
  bool mirrorOn = Config.get(APP_CONFIG_SCREEN_MIRROR, APP_CONFIG_SCREEN_MIRROR_DEFAULT).toInt();
  Mirror.setEnabled(mirrorOn); // master gate: off ⇒ no codec, no buffer, taps all no-op
  if (fmOn) {
    Serial.end();
    Serial.setRxBufferSize(4096);
    Serial.begin(115200);
  }
  if (fmOn || mirrorOn) {
    UartFM.begin(fmOn, mirrorOn);
  }
#ifdef DEVICE_HAS_RTC
  RtcManager::syncSystemFromRtc();
#endif
  _bootSplash();
#ifdef DEVICE_CYD
  if (Config.get(APP_CONFIG_TOUCH_CALIBRATED, APP_CONFIG_TOUCH_CALIBRATED_DEFAULT) == "0")
    Screen.setScreen(new CYDTouchCalScreen());
  else
#endif
#ifdef DEVICE_HAS_TOUCH_NAV
  if (Config.get(APP_CONFIG_TOUCH_GUIDE_SHOWN, APP_CONFIG_TOUCH_GUIDE_SHOWN_DEFAULT) == "0")
    Screen.setScreen(new TouchGuideScreen(false));
  else
#endif
  Screen.setScreen(new CharacterScreen());
}

void loop() {
  Uni.update();
  UartFM.update();
  BleFM.update();   // background BLE Remote Device (no-op while toggled off)

  // ── Power saving ──────────────────────────────────────────────────────────
  IScreen* _cur       = Screen.current();
  bool _psInhibit     = _cur && _cur->inhibitPowerSave();
#ifdef APP_MENU_POWER_OFF
  bool _poInhibit     = _psInhibit || (_cur && _cur->inhibitPowerOff());
#endif

  if (!_psInhibit && Config.get(APP_CONFIG_ENABLE_POWER_SAVING, APP_CONFIG_ENABLE_POWER_SAVING_DEFAULT).toInt()) {
    unsigned long idle   = millis() - Uni.lastActiveMs;
    unsigned long dispMs = (unsigned long)Config.get(APP_CONFIG_INTERVAL_DISPLAY_OFF, APP_CONFIG_INTERVAL_DISPLAY_OFF_DEFAULT).toInt() * 1000UL;

    if (!Uni.lcdOff && idle > dispMs) {
      Uni.Lcd.setBrightness(0);
      Uni.lcdOff = true;
    } else if (Uni.lcdOff && idle <= dispMs) {
      Uni.Lcd.setBrightness((uint8_t)Config.get(APP_CONFIG_BRIGHTNESS, APP_CONFIG_BRIGHTNESS_DEFAULT).toInt());
      Uni.lcdOff = false;
      // The press that woke the screen only turns the display on — it must
      // never propagate as an action. Drop any already-released wake event and
      // suppress the future release of an in-progress press.
      Uni.Nav->suppressCurrentPress();
#ifdef DEVICE_HAS_KEYBOARD
      if (Uni.Keyboard && Uni.Keyboard->available()) Uni.Keyboard->getKey();
#endif
      if (Screen.current()) Screen.current()->render();
      return;
    }

#ifdef APP_MENU_POWER_OFF
    if (!_poInhibit && Uni.lcdOff && Config.get(APP_CONFIG_ENABLE_POWER_OFF, APP_CONFIG_ENABLE_POWER_OFF_DEFAULT).toInt()) {
      unsigned long powerMs = dispMs + (unsigned long)Config.get(APP_CONFIG_INTERVAL_POWER_OFF, APP_CONFIG_INTERVAL_POWER_OFF_DEFAULT).toInt() * 1000UL;
      if (idle > powerMs) {
        Uni.Power.powerOff();
      }
    }
#endif
  } else if (Uni.lcdOff) {
    // auto display off disabled or inhibited while display was sleeping — restore brightness
    Uni.Lcd.setBrightness((uint8_t)Config.get(APP_CONFIG_BRIGHTNESS, APP_CONFIG_BRIGHTNESS_DEFAULT).toInt());
    Uni.lcdOff = false;
  }

  // ── Screen update ─────────────────────────────────────────────────────────
  Screen.update();

  // ── Live touch overlay (touch-nav boards only) ───────────────────────────
#ifdef DEVICE_HAS_TOUCH_NAV
  if (Uni.Nav && Config.get(APP_CONFIG_SHOW_OVERLAY, APP_CONFIG_SHOW_OVERLAY_DEFAULT).toInt())
    Uni.Nav->drawOverlay();
#endif
}
