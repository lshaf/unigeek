#include "ClaudeBuddyScreen.h"
#include "utils/ble/BuddyNus.h"
#include "utils/HackerHead.h"
#include "core/Device.h"
#include "core/ScreenManager.h"
#include "core/AchievementManager.h"
#include <esp_mac.h>
#include <string.h>
#include <stdlib.h>

// ── Layout ───────────────────────────────────────────────────────────────────
// Left column (char + buttons): 0..kCharColW
// Bubble tail: kCharColW+1 .. kCharColW+kTailW
// Dialog box: kCharColW+1+kTailW .. bodyW
// Footer: last kFooterH rows, separated by kGapH gap
// _cpl and _maxLines are computed at onInit() from the actual screen size.

static constexpr uint16_t kCharColW       = 56;
static constexpr uint16_t kTailW          = 5;
static constexpr uint16_t kFooterH        = 11;
static constexpr uint16_t kGapH           = 3;
static constexpr uint16_t kLineH          = 9;
static constexpr uint16_t kAnimIntervalMs = 500;
static constexpr uint16_t kRenderMs       = 200;
// Fixed entry storage: decoupled from _cpl so entries aren't truncated before
// they can word-wrap.  127 chars covers any real Claude output line.
static constexpr uint8_t  kEntryLen       = 127;

// ── Simple JSON field extractors (no external library) ───────────────────────

static bool _jStr(const char* json, const char* key, char* out, size_t outLen) {
  char pat[32];
  snprintf(pat, sizeof(pat), "\"%s\"", key);
  const char* p = strstr(json, pat);
  if (!p) return false;
  p += strlen(pat);
  while (*p == ':' || *p == ' ') p++;
  if (*p != '"') return false;
  p++;
  size_t i = 0;
  while (*p && *p != '"' && i < outLen - 1) {
    if (*p == '\\') { p++; if (*p) out[i++] = *p; }
    else out[i++] = *p;
    p++;
  }
  out[i] = 0;
  return true;
}

static bool _jInt(const char* json, const char* key, int* out) {
  char pat[32];
  snprintf(pat, sizeof(pat), "\"%s\":", key);
  const char* p = strstr(json, pat);
  if (!p) return false;
  p += strlen(pat);
  while (*p == ' ') p++;
  if (*p < '0' || *p > '9') return false;
  *out = (int)strtol(p, nullptr, 10);
  return true;
}

// ── ClaudeBuddyScreen ────────────────────────────────────────────────────────

ClaudeBuddyScreen::~ClaudeBuddyScreen() {
  _bleRunning = false;
  if (_bleTask)  { vTaskDelete(_bleTask);       _bleTask  = nullptr; }
  if (_stMutex)  { vSemaphoreDelete(_stMutex);  _stMutex  = nullptr; }
  buddyNusDeinit();
  delete[] _msg;       _msg       = nullptr;
  delete[] _lines;     _lines     = nullptr;
  delete[] _msgSnap;   _msgSnap   = nullptr;
  delete[] _linesSnap; _linesSnap = nullptr;
}

void ClaudeBuddyScreen::_bleTaskFn(void* arg) {
  ClaudeBuddyScreen* self = static_cast<ClaudeBuddyScreen*>(arg);
  while (self->_bleRunning) {
    self->_pollBle();
    vTaskDelay(pdMS_TO_TICKS(20));
  }
  vTaskDelete(nullptr);
}

void ClaudeBuddyScreen::onInit() {
  // Compute dialog text capacity from the actual screen dimensions
  {
    uint16_t bw  = bodyW();
    uint16_t bh  = bodyH();
    uint16_t dbx = kCharColW + 1 + kTailW;
    uint16_t dbw = (bw > dbx + 6u) ? bw - dbx : 6u;
    _cpl      = (uint8_t)((dbw - 6u) / 6u);
    _maxLines = (uint8_t)((bh > kFooterH + kGapH + kLineH)
                  ? (bh - kFooterH - kGapH) / kLineH : 1u);
    if (_cpl < 4) _cpl = 4;

    // msg: one display line.  entries: fixed kEntryLen so storage isn't
    // coupled to display width — _cpl governs wrapping only, not truncation.
    uint16_t msgB   = (uint16_t)(_cpl + 1u);
    uint16_t entryB = (uint16_t)(kEntryLen + 1u);
    uint8_t  nSlots = (uint8_t)((_maxLines + 2u) / 3u);  // ~1 entry per 3 lines
    delete[] _msg;       _msg       = new char[msgB]();
    delete[] _lines;     _lines     = new char[(uint32_t)nSlots * entryB]();
    delete[] _msgSnap;   _msgSnap   = new char[msgB]();
    delete[] _linesSnap; _linesSnap = new char[(uint32_t)nSlots * entryB]();
  }

  uint8_t mac[6] = {};
  esp_read_mac(mac, ESP_MAC_BT);
  char name[16];
  snprintf(name, sizeof(name), "Claude-%02X%02X", mac[4], mac[5]);
  buddyNusInit(name);

  memset(&_st, 0, sizeof(_st));
  _lineBufLen     = 0;
  _responseSent   = false;
  _selectedYes    = true;
  _animTick       = 0;
  _lastAnimMs     = millis();
  _lastRenderMs   = 0;
  _notifyPending  = false;

  _stMutex    = xSemaphoreCreateMutex();
  _bleRunning = true;
  xTaskCreate(_bleTaskFn, "BuddyBLE", 3072, this, 1, &_bleTask);

  if (Achievement.inc("claude_buddy_open") == 1)
    Achievement.unlock("claude_buddy_open");
}

void ClaudeBuddyScreen::onUpdate() {
  if (Uni.lcdOff) return;

  if (_connectedPending) {
    _connectedPending = false;
    if (Achievement.inc("claude_buddy_connected") == 1)
      Achievement.unlock("claude_buddy_connected");
  }

  if (_notifyPending) {
    _notifyPending = false;
    _responseSent  = false;
    _selectedYes   = true;
    if (Uni.Speaker) Uni.Speaker->playNotification();
  }

  uint32_t now = millis();

  if (now - _lastAnimMs >= kAnimIntervalMs) {
    _animTick++;
    _lastAnimMs = now;
  }

  if (Uni.Nav->wasPressed()) {
    auto dir = Uni.Nav->readDirection();

    if (dir == INavigation::DIR_BACK) {
      Screen.goBack();
      return;
    }

    char promptId[40] = {};
    xSemaphoreTake(_stMutex, portMAX_DELAY);
    memcpy(promptId, _st.promptId, sizeof(promptId));
    xSemaphoreGive(_stMutex);
    bool hasPending = promptId[0] && !_responseSent;

    if (hasPending) {
      if (dir == INavigation::DIR_UP || dir == INavigation::DIR_DOWN) {
        _selectedYes = !_selectedYes;
      } else if (dir == INavigation::DIR_PRESS) {
        char cmd[128];
        if (_selectedYes) {
          snprintf(cmd, sizeof(cmd),
            "{\"cmd\":\"permission\",\"id\":\"%s\",\"decision\":\"once\"}\n",
            promptId);
          _sendRaw(cmd);
          _responseSent = true;
          if (Achievement.inc("claude_buddy_approved") == 1)
            Achievement.unlock("claude_buddy_approved");
        } else {
          snprintf(cmd, sizeof(cmd),
            "{\"cmd\":\"permission\",\"id\":\"%s\",\"decision\":\"deny\"}\n",
            promptId);
          _sendRaw(cmd);
          _responseSent = true;
          if (Achievement.inc("claude_buddy_denied") == 1)
            Achievement.unlock("claude_buddy_denied");
        }
      }
    }
  }

  if (now - _lastRenderMs >= kRenderMs) {
    _lastRenderMs = now;
    render();
  }
}

void ClaudeBuddyScreen::onRender() {
  // Snapshot all shared state under mutex (text buffers + struct)
  _State st;
  const uint16_t entryB = (uint16_t)(kEntryLen + 1u);
  const uint8_t  nSlots = (uint8_t)((_maxLines + 2u) / 3u);
  xSemaphoreTake(_stMutex, portMAX_DELAY);
  memcpy(&st, &_st, sizeof(_State));
  memcpy(_msgSnap,   _msg,   _cpl + 1u);
  memcpy(_linesSnap, _lines, (uint32_t)nSlots * entryB);
  xSemaphoreGive(_stMutex);

  uint16_t bw = bodyW(), bh = bodyH();
  Sprite sp(&Uni.Lcd);
  sp.createSprite(bw, bh);
  sp.fillSprite(TFT_BLACK);
  sp.setTextSize(1);

  const uint16_t kDialogH = bh - kFooterH - kGapH;

  // ── Character column ──────────────────────────────────────────────────────
  const int ps    = 3;
  const int headW = 12 * ps;
  const int headH = 14 * ps;
  const int artX  = (kCharColW - headW) / 2;
  const int artY  = 6;

  int  rank = hackerGetRank(Achievement.getExp()).rank;
  bool blink;
  if (!st.connected) {
    blink = (_animTick & 3) < 2;
  } else if (_responseSent || st.waiting > 0) {
    blink = false;
  } else {
    blink = (_animTick & 7) < 1;
  }
  hackerDrawHead(sp, artX, artY, ps, blink, rank);

  uint16_t btnY      = (uint16_t)(artY + headH + 8);
  bool     hasPending = st.promptId[0] && !_responseSent;

  sp.setTextDatum(TC_DATUM);
  if (_responseSent) {
    sp.setTextColor(TFT_DARKGREY, TFT_BLACK);
    sp.drawString("sent", kCharColW / 2, btnY);
  } else if (hasPending) {
    uint16_t yesBg   = _selectedYes ? 0x07E0 : 0x2104;
    uint16_t noBg    = _selectedYes ? 0x2104 : 0xF800;
    uint16_t yesText = _selectedYes ? TFT_BLACK : TFT_WHITE;
    uint16_t noText  = _selectedYes ? TFT_WHITE : TFT_BLACK;

    sp.fillRoundRect(2, btnY,      kCharColW - 4, 14, 2, yesBg);
    sp.setTextColor(yesText, yesBg);
    sp.drawString("YES", kCharColW / 2, btnY + 3);

    sp.fillRoundRect(2, btnY + 18, kCharColW - 4, 14, 2, noBg);
    sp.setTextColor(noText, noBg);
    sp.drawString("NO", kCharColW / 2, btnY + 21);
  }
  sp.setTextDatum(TL_DATUM);

  // ── Dialog box — colors match CharacterScreen bubble ─────────────────────
  const uint16_t kBubBg = 0x0841;
  const uint16_t kCol3  = TFT_GREEN;
  const uint16_t kCol2  = 0x0460;
  const uint16_t kCol1  = 0x01C0;

  const uint16_t dbx  = kCharColW + 1u + kTailW;
  const uint16_t dbw  = bw - dbx;
  sp.fillRect(dbx + 1, 1, dbw - 2, kDialogH - 2, kBubBg);
  sp.drawRect(dbx, 0, dbw, kDialogH, kCol3);

  uint16_t dx   = dbx + 3;
  uint16_t dy   = 3;
  const uint16_t maxY = kDialogH - kLineH;

  // Word-wraps s into the dialog box, breaking at the last space within _cpl
  // chars. Falls back to a hard break when no space exists in the line.
  auto printWrapped = [&](const char* s, uint16_t color) {
    sp.setTextColor(color, kBubBg);
    int len = (int)strlen(s);
    int off = 0;
    while (off < len && dy <= maxY) {
      int avail = len - off;
      if (avail <= (int)_cpl) {
        sp.setCursor(dx, dy);
        sp.printf("%.*s", avail, s + off);
        dy += kLineH;
        break;
      }
      // find last space within _cpl chars so we break at a word boundary
      int brk = _cpl;
      for (int k = _cpl - 1; k > 0; k--) {
        if (s[off + k] == ' ') { brk = k; break; }
      }
      sp.setCursor(dx, dy);
      sp.printf("%.*s", brk, s + off);
      dy += kLineH;
      off += brk;
      while (s[off] == ' ') off++;  // skip the space we broke on
    }
  };

  if (!st.connected) {
    sp.setTextColor(kCol3, kBubBg);
    sp.setCursor(dx, dy); sp.print("Connecting..."); dy += kLineH + 3;
    sp.setTextColor(kCol2, kBubBg);
    sp.setCursor(dx, dy); sp.print("On Claude desktop:"); dy += kLineH;
    sp.setCursor(dx, dy); sp.print("Help > Troubleshoot"); dy += kLineH;
    sp.setCursor(dx, dy); sp.print("> Enable Dev Mode");   dy += kLineH + 4;
    sp.setCursor(dx, dy); sp.print("Then:");               dy += kLineH;
    sp.setCursor(dx, dy); sp.print("Developer >");         dy += kLineH;
    sp.setCursor(dx, dy); sp.print("Hardware Buddy");      dy += kLineH;
    if (dy <= maxY) { sp.setCursor(dx, dy); sp.print("> Connect"); }

  } else if (hasPending) {
    sp.setTextColor(0xFA20, kBubBg);
    sp.setCursor(dx, dy); sp.print("APPROVE?"); dy += kLineH + 3;

    int toolLen = (int)strlen(st.promptTool);
    if (toolLen <= 10 && dy + 18 <= maxY) {
      sp.setTextSize(2);
      sp.setTextColor(kCol3, kBubBg);
      sp.setCursor(dx, dy); sp.print(st.promptTool);
      sp.setTextSize(1);
      dy += 18;
    } else if (dy <= maxY) {
      sp.setTextColor(kCol3, kBubBg);
      sp.setCursor(dx, dy); sp.print(st.promptTool); dy += kLineH + 2;
    }

    printWrapped(st.promptHint, kCol2);

    uint32_t waited = st.lastUpdated ? (millis() - st.lastUpdated) / 1000 : 0;
    if (dy <= maxY) {
      sp.setTextColor(waited >= 10 ? 0xF800 : kCol1, kBubBg);
      sp.setCursor(dx, dy); sp.printf("wait %lus", (unsigned long)waited);
    }

  } else {
    for (int i = 0; i < st.nLines && dy <= maxY; i++) {
      const char* s = _linesSnap + i * entryB;
      uint16_t    c = (i == st.nLines - 1) ? kCol3
                    : (i == st.nLines - 2) ? kCol2 : kCol1;
      printWrapped(s, c);
    }
    if (_msgSnap[0] && dy <= maxY) {
      sp.setTextColor(kCol3, kBubBg);
      sp.setCursor(dx, dy); sp.print(_msgSnap);
    }
  }

  // Bubble tail — drawn last so it renders on top of dialog box border
  const int tailMy = artY + headH / 2;
  for (int i = 0; i < (int)kTailW; i++) {
    int spread = i + 1;
    int tx     = kCharColW + 1 + i;
    sp.drawFastVLine(tx, tailMy - spread, spread * 2, kBubBg);
    sp.drawPixel(tx, tailMy - spread,     kCol3);
    sp.drawPixel(tx, tailMy + spread - 1, kCol3);
  }
  // Erase dialog left border where tail meets box to create seamless opening
  sp.drawFastVLine(dbx, tailMy - (int)kTailW, kTailW * 2, kBubBg);

  // ── Footer ────────────────────────────────────────────────────────────────
  uint16_t fy = kDialogH + kGapH;
  sp.fillRect(0, fy, bw, kFooterH, 0x1082);
  uint16_t dotColor = st.connected ? TFT_GREEN : TFT_DARKGREY;
  sp.fillCircle(5, fy + kFooterH / 2, 3, dotColor);
  sp.setTextColor(TFT_DARKGREY, 0x1082);
  sp.setCursor(12, fy + 2);
  sp.printf("T:%u R:%u W:%u", st.total, st.running, st.waiting);
  sp.setTextColor(TFT_WHITE, 0x1082);
#ifdef DEVICE_HAS_KEYBOARD
  sp.setCursor(bw - 60, fy + 2);
  sp.print("BACK: quit");
#else
  sp.setCursor(bw - 72, fy + 2);
  sp.print("hold B: quit");
#endif

  sp.pushSprite(bodyX(), bodyY());
  sp.deleteSprite();
}

// ── BLE polling ───────────────────────────────────────────────────────────────

void ClaudeBuddyScreen::_pollBle() {
  while (buddyNusAvailable()) {
    int c = buddyNusRead();
    if (c < 0) break;
    if (c == '\n' || c == '\r') {
      if (_lineBufLen > 0) {
        _lineBuf[_lineBufLen] = 0;
        if (_lineBuf[0] == '{') _applyJson(_lineBuf);
        _lineBufLen = 0;
      }
    } else if (_lineBufLen < sizeof(_lineBuf) - 1) {
      _lineBuf[_lineBufLen++] = (char)c;
    }
  }
}

void ClaudeBuddyScreen::_applyJson(const char* json) {
  char cmd[20] = {};
  _jStr(json, "cmd", cmd, sizeof(cmd));

  if (cmd[0]) {
    char ack[64];
    snprintf(ack, sizeof(ack), "{\"ack\":\"%s\",\"ok\":true}\n", cmd);
    _sendRaw(ack);
    return;
  }

  // Parse everything before taking the mutex
  int v;
  uint8_t newTotal = _st.total, newRunning = _st.running, newWaiting = _st.waiting;
  if (_jInt(json, "total",   &v)) newTotal   = (uint8_t)v;
  if (_jInt(json, "running", &v)) newRunning = (uint8_t)v;
  if (_jInt(json, "waiting", &v)) newWaiting = (uint8_t)v;

  // msg: parse into local, will be truncated to _cpl when stored
  char newMsg[64] = {};
  _jStr(json, "msg", newMsg, sizeof(newMsg));

  // entries: fixed kEntryLen per slot — _cpl is for display wrapping only
  const uint16_t entryB = (uint16_t)(kEntryLen + 1u);
  const uint8_t  nSlots = (uint8_t)((_maxLines + 2u) / 3u);
  char* newLines  = new char[(uint32_t)nSlots * entryB]();
  uint8_t newNLines = 0;
  const char* ea = strstr(json, "\"entries\":[");
  if (ea) {
    const char* p = ea + 11;
    while (*p && *p != ']' && newNLines < nSlots) {
      while (*p == ' ' || *p == ',') p++;
      if (*p == '"') {
        p++;
        size_t i = 0;
        char* dst = newLines + newNLines * entryB;
        while (*p && *p != '"' && i < (size_t)kEntryLen)
          dst[i++] = *p++;
        dst[i] = 0;
        if (*p == '"') p++;
        newNLines++;
      } else break;
    }
  }

  char newPromptId[40] = {}, newPromptTool[24] = {}, newPromptHint[48] = {};
  bool hasPrompt   = strstr(json, "\"prompt\":{") != nullptr;
  bool clearPrompt = !hasPrompt && strstr(json, "\"total\"") != nullptr;
  if (hasPrompt) {
    const char* pr = strstr(json, "\"prompt\":{");
    _jStr(pr, "id",   newPromptId,   sizeof(newPromptId));
    _jStr(pr, "tool", newPromptTool, sizeof(newPromptTool));
    _jStr(pr, "hint", newPromptHint, sizeof(newPromptHint));
  }

  // Write to _st, _msg, _lines under mutex
  bool wasConnected;
  xSemaphoreTake(_stMutex, portMAX_DELAY);
  wasConnected = _st.connected;
  _st.total    = newTotal;
  _st.running  = newRunning;
  _st.waiting  = newWaiting;
  strncpy(_msg, newMsg, _cpl);
  _msg[_cpl] = 0;
  if (newNLines > 0) {
    _st.nLines = newNLines;
    memcpy(_lines, newLines, (uint32_t)newNLines * entryB);
  }
  if (hasPrompt) {
    memcpy(_st.promptId,   newPromptId,   sizeof(_st.promptId));
    memcpy(_st.promptTool, newPromptTool, sizeof(_st.promptTool));
    memcpy(_st.promptHint, newPromptHint, sizeof(_st.promptHint));
  } else if (clearPrompt) {
    _st.promptId[0] = _st.promptTool[0] = _st.promptHint[0] = 0;
  }
  _st.connected   = true;
  _st.lastUpdated = millis();
  xSemaphoreGive(_stMutex);

  delete[] newLines;

  if (hasPrompt)    _notifyPending    = true;
  if (!wasConnected) _connectedPending = true;
}

void ClaudeBuddyScreen::_sendRaw(const char* json) {
  buddyNusWrite(reinterpret_cast<const uint8_t*>(json), strlen(json));
}
