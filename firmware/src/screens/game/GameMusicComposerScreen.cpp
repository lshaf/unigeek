#include "GameMusicComposerScreen.h"

#include <math.h>

#include "core/Device.h"
#include "core/INavigation.h"
#include "core/ScreenManager.h"
#include "core/ConfigManager.h"
#include "ui/actions/InputSelectAction.h"
#include "ui/actions/InputNumberAction.h"
#include "ui/actions/InputTextAction.h"
#include "ui/actions/ShowStatusAction.h"

namespace {
constexpr const char* kMusicDir = "/unigeek/music";

constexpr const char* kNoteNames[12] = {
  "C","C#","D","D#","E","F","F#","G","G#","A","A#","B"
};

// ── Built-in demo songs ───────────────────────────────────────────────────────
// Traditional public-domain melodies, RTTTL format.
struct BuiltinSong { const char* name; const char* rtttl; };
constexpr BuiltinSong kBuiltins[] = {
  {"Twinkle",      "Twinkle:d=4,o=5,b=120:c,c,g,g,a,a,2g,f,f,e,e,d,d,2c,g,g,f,f,e,e,2d,g,g,f,f,e,e,2d"},
  {"OdeToJoy",     "OdeToJoy:d=4,o=5,b=140:e,e,f,g,g,f,e,d,c,c,d,e,e.,8d,2d,e,e,f,g,g,f,e,d,c,c,d,e,d.,8c,2c"},
  {"FrereJacques", "Frere:d=4,o=5,b=120:c,d,e,c,c,d,e,c,e,f,2g,e,f,2g,8g,8a,8g,8f,e,c,8g,8a,8g,8f,e,c,c,2g4,2c,c,2g4,2c"},
  {"JingleBells",  "JingleBells:d=8,o=5,b=180:e,e,4e,e,e,4e,e,g,c,d,2e,f,f,f,f,f,e,e,e,e,d,d,e,2d,2g"},
  {"MaryLamb",     "MaryLamb:d=4,o=5,b=140:e,d,c,d,e,e,2e,d,d,2d,e,g,2g,e,d,c,d,e,e,e,e,d,d,e,d,2c"},
  {"AlsAct",       "AlsAct:d=4,o=5,b=120:8a,8b,c6,8b,a,2p,8a,8b,c6,8b,a,2p,a,c6,8e6,8d6,2c6"},
  {"Beep",         "Beep:d=8,o=5,b=160:c,e,g,c6,p,c6,g,e,c"},
};
constexpr uint8_t kBuiltinCount = sizeof(kBuiltins) / sizeof(kBuiltins[0]);

uint8_t noteCharToOffset(char c) {
  switch (c) {
    case 'c': return 0;
    case 'd': return 2;
    case 'e': return 4;
    case 'f': return 5;
    case 'g': return 7;
    case 'a': return 9;
    case 'b': return 11;
    default:  return 255;
  }
}

bool isValidLen(uint8_t len) {
  return len == 1 || len == 2 || len == 4 || len == 8 || len == 16 || len == 32;
}

uint8_t nextLen(uint8_t len) {
  switch (len) {
    case 1:  return 2;
    case 2:  return 4;
    case 4:  return 8;
    case 8:  return 16;
    case 16: return 32;
    default: return 1;
  }
}
} // namespace

// ── Lifecycle ────────────────────────────────────────────────────────────────

const char* GameMusicComposerScreen::title() {
  switch (_state) {
    case STATE_FILES:   return "Open Song";
    case STATE_BUILTIN: return "Built-in";
    case STATE_EDIT:    return "Composer";
    case STATE_PLAY:    return "Playing";
    default:            return "Music Composer";
  }
}

void GameMusicComposerScreen::onInit() {
  if (Uni.Storage && Uni.Storage->isAvailable()) {
    Uni.Storage->makeDir(kMusicDir);
  }
  _enterMenu();
}

void GameMusicComposerScreen::onUpdate() {
  if (_state == STATE_EDIT) {
    if (!Uni.Nav->wasPressed()) return;
    auto dir = Uni.Nav->readDirection();

    if (dir == INavigation::DIR_BACK) { _enterMenu(); return; }

    // UP/DOWN always shifts pitch on the current step (the high-frequency
    // edit). On 4-way boards LEFT/RIGHT moves the cursor; on 3-button stick
    // boards cursor movement lives in the action menu under "Move step…" —
    // composing is overwhelmingly pitch edits, so the common case is one
    // click per semitone instead of opening a menu every time.
    if (dir == INavigation::DIR_UP)   { _shiftSemitone(+1); render(); return; }
    if (dir == INavigation::DIR_DOWN) { _shiftSemitone(-1); render(); return; }
    if (Uni.Nav->is4Way()) {
      if (dir == INavigation::DIR_LEFT) {
        if (_stepCount > 0) _cursor = (_cursor == 0) ? (_stepCount - 1) : (_cursor - 1);
        _dirty = true; render(); return;
      }
      if (dir == INavigation::DIR_RIGHT) {
        if (_stepCount > 0) _cursor = (_cursor + 1) % _stepCount;
        _dirty = true; render(); return;
      }
    }
    if (dir == INavigation::DIR_PRESS) { _openActionMenu(); render(); return; }
    return;
  }

  if (_state == STATE_PLAY) {
    // Non-blocking stop on any button.
    if (Uni.Nav->wasPressed()) {
      Uni.Nav->readDirection();
      if (Uni.Speaker) Uni.Speaker->noTone();
      _enterEdit();
      return;
    }
    if (millis() < _playNextMs) return;

    if (_playIdx >= _stepCount) {
      if (Uni.Speaker) Uni.Speaker->noTone();
      ShowStatusAction::show("Done", 700);
      _enterEdit();
      return;
    }

    const Step& s = _steps[_playIdx];
    uint32_t durMs = _stepDurationMs(s.len);
    // Tiny gap so consecutive same-pitch notes are heard as distinct.
    uint32_t playMs = (durMs > 30) ? (durMs - 25) : durMs;
    if (Uni.Speaker) {
      if (s.midi == 0) Uni.Speaker->noTone();
      else             Uni.Speaker->tone(_midiToFreq(s.midi), playMs);
    }
    _playNextMs = millis() + durMs;
    _playIdx++;
    _renderPlayback();
    return;
  }

  // Menu / Files / Builtin → default ListScreen behaviour.
  ListScreen::onUpdate();
}

void GameMusicComposerScreen::onRender() {
  if (_state == STATE_EDIT) { _renderEditor(); return; }
  if (_state == STATE_PLAY) { _renderPlayback(); return; }
  ListScreen::onRender();
}

void GameMusicComposerScreen::onBack() {
  if (_state == STATE_FILES || _state == STATE_BUILTIN) { _enterMenu(); return; }
  if (_state == STATE_EDIT || _state == STATE_PLAY)     { _enterMenu(); return; }
  Screen.goBack();
}

void GameMusicComposerScreen::onItemSelected(uint8_t index) {
  if (_state == STATE_MENU) {
    switch (index) {
      case 0: _newSong();     _enterEdit();    break;
      case 1: _enterFiles();                   break;
      case 2: _enterBuiltin();                 break;
    }
    return;
  }
  if (_state == STATE_FILES) {
    const auto& e = _browser.entry(index);
    if (e.isDir) return;  // music dir is flat — ignore
    if (_loadFile(e.path)) {
      _filename = e.path;
      _enterEdit();
    } else {
      ShowStatusAction::show("Load failed");
      render();  // ShowStatusAction left a black hole in the file list
    }
    return;
  }
  if (_state == STATE_BUILTIN) {
    if (index >= kBuiltinCount) return;
    if (_parseRtttl(kBuiltins[index].rtttl)) {
      _filename = "";  // built-ins start unsaved
      _cursor   = 0;
      _enterEdit();
    } else {
      ShowStatusAction::show("Parse failed");
      render();
    }
    return;
  }
}

// ── State transitions ────────────────────────────────────────────────────────

void GameMusicComposerScreen::_enterMenu() {
  _state = STATE_MENU;
  _menuItems[0] = { "New Song" };
  _menuItems[1] = { "Open Saved..." };
  _menuItems[2] = { "Built-in Demos" };
  setItems(_menuItems, 3);
  render();
}

void GameMusicComposerScreen::_enterFiles() {
  _state = STATE_FILES;
  uint8_t n = _browser.load(this, kMusicDir, BrowseFileView::Mode(".rtttl"), "song");
  if (n == 0) {
    ShowStatusAction::show("No saved songs");
    _enterMenu();
    return;
  }
  setItems(_browser.items(), n);
  render();
}

void GameMusicComposerScreen::_enterBuiltin() {
  _state = STATE_BUILTIN;
  uint8_t n = kBuiltinCount;
  if (n > sizeof(_builtinItems) / sizeof(_builtinItems[0]))
    n = sizeof(_builtinItems) / sizeof(_builtinItems[0]);
  for (uint8_t i = 0; i < n; i++) _builtinItems[i] = { kBuiltins[i].name };
  setItems(_builtinItems, n);
  render();
}

void GameMusicComposerScreen::_enterEdit() {
  _state  = STATE_EDIT;
  _ensureFirstStep();
  if (_cursor >= _stepCount) _cursor = _stepCount ? _stepCount - 1 : 0;
  _dirty = true;
  render();
}

void GameMusicComposerScreen::_enterPlay() {
  if (_stepCount == 0) { ShowStatusAction::show("Empty song"); return; }
  _state       = STATE_PLAY;
  _playIdx     = 0;
  _playNextMs  = millis();
  _playFirst   = true;
  render();
}

// ── New / edit helpers ───────────────────────────────────────────────────────

void GameMusicComposerScreen::_newSong() {
  _stepCount   = 0;
  _cursor      = 0;
  _bpm         = 120;
  _songName    = "Untitled";
  _filename    = "";
  _ensureFirstStep();
}

void GameMusicComposerScreen::_ensureFirstStep() {
  if (_stepCount == 0) {
    _steps[0] = { 60, 4 };  // middle C, quarter note
    _stepCount = 1;
  }
}

void GameMusicComposerScreen::_shiftSemitone(int delta) {
  if (_stepCount == 0) return;
  Step& s = _steps[_cursor];
  // Treat rest as "below MIDI_MIN" so UP from rest gives the lowest pitch.
  int v = (s.midi == 0) ? (MIDI_MIN - 1) : s.midi;
  v += delta;
  if (v < (int)(MIDI_MIN - 1)) v = MIDI_MAX;
  else if (v < (int)MIDI_MIN)  s.midi = 0;     // landed on the rest slot
  else if (v > (int)MIDI_MAX)  v = MIDI_MIN - 1;
  if (v >= (int)MIDI_MIN && v <= (int)MIDI_MAX) s.midi = (uint8_t)v;
  else if (v == (int)(MIDI_MIN - 1))            s.midi = 0;

  // Audition the current note so the user hears the pitch they just picked.
  if (Uni.Speaker && s.midi != 0) {
    Uni.Speaker->tone(_midiToFreq(s.midi), 120);
  }
  _dirty = true;
}

void GameMusicComposerScreen::_cycleLength() {
  if (_stepCount == 0) return;
  _steps[_cursor].len = nextLen(_steps[_cursor].len);
  _dirty = true;
}

void GameMusicComposerScreen::_insertStepAtCursor() {
  if (_stepCount >= MAX_STEPS) { ShowStatusAction::show("Step limit"); return; }
  uint8_t at = _cursor + 1;
  for (int i = _stepCount; i > at; i--) _steps[i] = _steps[i - 1];
  Step prev = _steps[_cursor];
  _steps[at]  = { prev.midi, prev.len };
  _stepCount++;
  _cursor = at;
  _dirty = true;
}

void GameMusicComposerScreen::_deleteStepAtCursor() {
  if (_stepCount <= 1) { ShowStatusAction::show("Need >= 1 step"); return; }
  for (int i = _cursor; i < _stepCount - 1; i++) _steps[i] = _steps[i + 1];
  _stepCount--;
  if (_cursor >= _stepCount) _cursor = _stepCount - 1;
  _dirty = true;
}

void GameMusicComposerScreen::_clearAll() {
  _stepCount = 0;
  _cursor    = 0;
  _ensureFirstStep();
  _dirty = true;
}

// ── Action menu (PRESS in editor) ────────────────────────────────────────────

void GameMusicComposerScreen::_openActionMenu() {
  // Any popup below paints over the body (InputSelect wipes its overlay rect,
  // InputNumber/InputText in scroll mode wipe the whole screen). Force a full
  // editor repaint on return so the outer render() doesn't no-op via _dirty.
  _dirty = true;

  // Pitch +/- entries are essential on 2-axis boards where UP/DOWN drives
  // the cursor; on 4-way boards UP/DOWN already shifts pitch, but keeping
  // these entries also lets a touch board change pitch without 4-way.
  static constexpr InputSelectAction::Option opts[] = {
    {"Play song",        "play"},
    {"Move step...",     "go"},
    {"Prev step",        "prev"},
    {"Next step",        "next"},
    {"Octave +1",        "p+12"},
    {"Octave -1",        "p-12"},
    {"Toggle rest",      "rest"},
    {"Note length",      "len"},
    {"Insert step",      "ins"},
    {"Delete step",      "del"},
    {"Tempo (BPM)",      "bpm"},
    {"Rename",           "ren"},
    {"Save",             "save"},
    {"Clear all",        "clear"},
  };
  const char* pick = InputSelectAction::popup("Composer", opts, sizeof(opts) / sizeof(opts[0]));
  if (!pick) return;
  if (!strcmp(pick, "play"))  { _enterPlay(); return; }
  if (!strcmp(pick, "go"))    {
    if (_stepCount == 0) return;
    int v = InputNumberAction::popup("Step", 1, _stepCount, _cursor + 1);
    if (!InputNumberAction::wasCancelled() && v >= 1 && v <= (int)_stepCount) {
      _cursor = (uint8_t)(v - 1);
    }
    return;
  }
  if (!strcmp(pick, "prev"))  {
    if (_stepCount > 0) _cursor = (_cursor == 0) ? (_stepCount - 1) : (_cursor - 1);
    return;
  }
  if (!strcmp(pick, "next"))  {
    if (_stepCount > 0) _cursor = (_cursor + 1) % _stepCount;
    return;
  }
  if (!strcmp(pick, "p+12"))  { _shiftSemitone(+12);  return; }
  if (!strcmp(pick, "p-12"))  { _shiftSemitone(-12);  return; }
  if (!strcmp(pick, "rest"))  {
    if (_stepCount > 0) {
      Step& s = _steps[_cursor];
      s.midi = (s.midi == 0) ? 60 : 0;  // toggle between rest and middle C
      _dirty = true;
    }
    return;
  }
  if (!strcmp(pick, "len"))   { _cycleLength(); return; }
  if (!strcmp(pick, "ins"))   { _insertStepAtCursor(); return; }
  if (!strcmp(pick, "del"))   { _deleteStepAtCursor(); return; }
  if (!strcmp(pick, "bpm"))   {
    int v = InputNumberAction::popup("BPM", 40, 300, _bpm);
    if (!InputNumberAction::wasCancelled() && v >= 40 && v <= 300) _bpm = (uint16_t)v;
    return;
  }
  if (!strcmp(pick, "ren"))   { _rename(); return; }
  if (!strcmp(pick, "save"))  { _save(); return; }
  if (!strcmp(pick, "clear")) { _clearAll(); return; }
}

void GameMusicComposerScreen::_rename() {
  String name = InputTextAction::popup("Song name", _songName);
  if (name.length() == 0) return;
  // RTTTL-safe: no commas or colons.
  name.replace(",", " ");
  name.replace(":", " ");
  _songName = name;
}

void GameMusicComposerScreen::_save() {
  if (!Uni.Storage || !Uni.Storage->isAvailable()) {
    ShowStatusAction::show("No storage"); return;
  }
  Uni.Storage->makeDir(kMusicDir);

  String fname = _filename;
  if (fname.length() == 0) {
    String base = _songName.length() ? _songName : String("song");
    // Sanitize basename.
    String safe;
    for (size_t i = 0; i < base.length(); i++) {
      char c = base[i];
      if (isalnum((unsigned char)c)) safe += c;
      else if (c == ' ' || c == '-' || c == '_') safe += '_';
    }
    if (safe.length() == 0) safe = "song";
    fname = String(kMusicDir) + "/" + safe + ".rtttl";
  }

  String body = _serializeRtttl();
  if (Uni.Storage->writeFile(fname.c_str(), body.c_str())) {
    _filename = fname;
    ShowStatusAction::show("Saved", 700);
  } else {
    ShowStatusAction::show("Save failed");
  }
}

// ── Timing & pitch ───────────────────────────────────────────────────────────

uint32_t GameMusicComposerScreen::_stepDurationMs(uint8_t lenCode) const {
  if (!isValidLen(lenCode)) lenCode = 4;
  // A quarter note (len=4) = one beat. ms_per_beat = 60000 / bpm.
  // duration = ms_per_beat * (4 / len).
  return (uint32_t)(60000UL * 4UL / (uint32_t)_bpm / (uint32_t)lenCode);
}

uint16_t GameMusicComposerScreen::_midiToFreq(uint8_t midi) const {
  if (midi == 0) return 0;
  double f = 440.0 * pow(2.0, ((double)midi - 69.0) / 12.0);
  if (f < 1.0)     return 1;
  if (f > 20000.0) return 20000;
  return (uint16_t)(f + 0.5);
}

// ── Editor rendering ─────────────────────────────────────────────────────────

void GameMusicComposerScreen::_renderEditor() {
  if (!_dirty) return;
  _dirty = false;

  auto& lcd = Uni.Lcd;
  int x = bodyX(), y = bodyY(), w = bodyW(), h = bodyH();
  uint16_t theme = Config.getThemeColor();

  Sprite sp(&lcd);
  sp.createSprite(w, h);
  sp.fillSprite(TFT_BLACK);
  sp.setTextSize(1);

  // ── Header line: name + BPM + position ─────────────────────────────────────
  sp.setTextDatum(TL_DATUM);
  sp.setTextColor(TFT_YELLOW);
  String hdr = _songName;
  if (hdr.length() > 14) hdr = hdr.substring(0, 13) + "…";
  sp.drawString(hdr.c_str(), 0, 0);

  char meta[32];
  snprintf(meta, sizeof(meta), "%u BPM  %u/%u",
           (unsigned)_bpm, (unsigned)(_cursor + 1), (unsigned)_stepCount);
  sp.setTextColor(TFT_DARKGREY);
  sp.setTextDatum(TR_DATUM);
  sp.drawString(meta, w - 1, 0);

  // ── Big current-note display ───────────────────────────────────────────────
  const Step& cur = _steps[_cursor];
  char bigName[8];
  if (cur.midi == 0) {
    snprintf(bigName, sizeof(bigName), "REST");
  } else {
    uint8_t off = cur.midi % 12;
    int     oct = (int)(cur.midi / 12) - 1;
    snprintf(bigName, sizeof(bigName), "%s%d", kNoteNames[off], oct);
  }
  sp.setTextDatum(MC_DATUM);
  sp.setTextColor(theme);
  sp.setTextSize(3);
  int midY = 14 + (h - 14 - 36) / 2;
  sp.drawString(bigName, w / 2, midY - 8);

  // Length sub-line ("1/4 note")
  sp.setTextSize(1);
  char lenStr[16];
  snprintf(lenStr, sizeof(lenStr), "1/%u note", (unsigned)cur.len);
  sp.setTextColor(TFT_LIGHTGREY);
  sp.drawString(lenStr, w / 2, midY + 18);

  // ── Mini-strip: 9 steps centered on cursor ────────────────────────────────
  static constexpr int STRIP_H = 14;
  int stripY = h - STRIP_H - 10;
  int slots  = 9;
  int slotW  = w / slots;
  for (int i = 0; i < slots; i++) {
    int idx = (int)_cursor + i - slots / 2;
    int sx  = i * slotW;
    bool inRange = (idx >= 0 && idx < (int)_stepCount);
    bool sel     = (i == slots / 2) && inRange;
    if (sel) sp.fillRoundRect(sx + 1, stripY, slotW - 2, STRIP_H, 2, theme);
    else     sp.drawRoundRect(sx + 1, stripY, slotW - 2, STRIP_H, 2, 0x2104);
    if (!inRange) continue;
    const Step& st = _steps[idx];
    char tag[4];
    if (st.midi == 0) tag[0] = '-', tag[1] = '\0';
    else {
      uint8_t off = st.midi % 12;
      // single-char name (drop sharp suffix) so it fits the cell
      tag[0] = kNoteNames[off][0];
      tag[1] = '\0';
    }
    sp.setTextColor(sel ? TFT_WHITE : TFT_LIGHTGREY);
    sp.setTextDatum(MC_DATUM);
    sp.drawString(tag, sx + slotW / 2, stripY + STRIP_H / 2);
  }

  // ── Bottom hint ────────────────────────────────────────────────────────────
  sp.setTextColor(TFT_DARKGREY);
  sp.setTextDatum(BC_DATUM);
  if (Uni.Nav->is4Way()) sp.drawString("LR step  UPDN pitch  PRESS menu", w / 2, h - 1);
  else                   sp.drawString("UPDN pitch  PRESS menu",           w / 2, h - 1);

  sp.pushSprite(x, y);
  sp.deleteSprite();
}

// ── Playback rendering ───────────────────────────────────────────────────────

void GameMusicComposerScreen::_renderPlayback() {
  auto& lcd = Uni.Lcd;
  int x = bodyX(), y = bodyY(), w = bodyW(), h = bodyH();
  uint16_t theme = Config.getThemeColor();

  Sprite sp(&lcd);
  sp.createSprite(w, h);
  sp.fillSprite(TFT_BLACK);
  sp.setTextSize(1);

  sp.setTextDatum(TL_DATUM);
  sp.setTextColor(TFT_YELLOW);
  String hdr = _songName;
  if (hdr.length() > 18) hdr = hdr.substring(0, 17) + "…";
  sp.drawString(hdr.c_str(), 0, 0);

  // Big "now playing" note
  uint8_t playing = (_playIdx > 0 && _playIdx <= _stepCount)
                  ? _steps[_playIdx - 1].midi : 0;
  char bigName[8];
  if (playing == 0) snprintf(bigName, sizeof(bigName), "—");
  else {
    uint8_t off = playing % 12;
    int     oct = (int)(playing / 12) - 1;
    snprintf(bigName, sizeof(bigName), "%s%d", kNoteNames[off], oct);
  }
  sp.setTextDatum(MC_DATUM);
  sp.setTextColor(theme);
  sp.setTextSize(3);
  sp.drawString(bigName, w / 2, h / 2 - 6);

  // Progress
  sp.setTextSize(1);
  char prog[16];
  snprintf(prog, sizeof(prog), "%u / %u", (unsigned)_playIdx, (unsigned)_stepCount);
  sp.setTextColor(TFT_LIGHTGREY);
  sp.drawString(prog, w / 2, h / 2 + 22);

  int barY = h - 20;
  int barW = w - 8;
  int fill = (_stepCount > 0) ? (barW * (int)_playIdx / (int)_stepCount) : 0;
  sp.fillRect(4, barY, barW, 4, 0x2104);
  sp.fillRect(4, barY, fill, 4, theme);

  sp.setTextColor(TFT_DARKGREY);
  sp.setTextDatum(BC_DATUM);
  sp.drawString("Any button to stop", w / 2, h - 1);

  sp.pushSprite(x, y);
  sp.deleteSprite();
}

// ── RTTTL parse / serialize ──────────────────────────────────────────────────
//
// Format: <name>:<defaults>:<notes>
//   defaults:  d=N,o=N,b=N
//   note:      [duration][note-letter][#][octave][.]   (lowercase, comma-separated)
//   note letter 'p' = pause/rest.
//
// We deliberately do NOT preserve dotted durations — collapse them onto the
// next-shorter base length so every step still maps to one of {1,2,4,8,16,32}.
// Truncates to MAX_STEPS.

bool GameMusicComposerScreen::_parseRtttl(const String& text) {
  int c1 = text.indexOf(':');
  if (c1 < 0) return false;
  int c2 = text.indexOf(':', c1 + 1);
  if (c2 < 0) return false;

  _songName = text.substring(0, c1);
  _songName.trim();
  if (_songName.length() == 0) _songName = "Untitled";

  String defaults = text.substring(c1 + 1, c2);
  String notes    = text.substring(c2 + 1);

  uint8_t  defDur  = 4;
  uint8_t  defOct  = 5;
  uint16_t defBpm  = 120;

  // Parse "d=N,o=N,b=N" tolerantly.
  defaults.toLowerCase();
  int p = 0;
  while (p < (int)defaults.length()) {
    int eq = defaults.indexOf('=', p);
    if (eq < 0) break;
    char key = defaults[p];
    int co  = defaults.indexOf(',', eq + 1);
    String val = (co < 0) ? defaults.substring(eq + 1) : defaults.substring(eq + 1, co);
    val.trim();
    int n = val.toInt();
    if      (key == 'd' && n > 0) defDur = (uint8_t)n;
    else if (key == 'o' && n > 0) defOct = (uint8_t)n;
    else if (key == 'b' && n > 0) defBpm = (uint16_t)n;
    p = (co < 0) ? defaults.length() : (co + 1);
  }

  if (defBpm < 40) defBpm = 40;
  if (defBpm > 300) defBpm = 300;
  _bpm = defBpm;

  _stepCount = 0;
  _cursor    = 0;
  notes.toLowerCase();

  int i = 0, n = notes.length();
  while (i < n && _stepCount < MAX_STEPS) {
    while (i < n && (notes[i] == ' ' || notes[i] == ',')) i++;
    if (i >= n) break;

    // Duration (optional digits).
    int dur = 0;
    while (i < n && isdigit((unsigned char)notes[i])) { dur = dur * 10 + (notes[i] - '0'); i++; }
    if (dur == 0) dur = defDur;

    if (i >= n) break;
    char letter = notes[i++];
    uint8_t midi = 0;
    bool isRest = false;

    if (letter == 'p') {
      isRest = true;
    } else {
      uint8_t off = noteCharToOffset(letter);
      if (off == 255) {
        // Skip unknown token until next comma.
        while (i < n && notes[i] != ',') i++;
        continue;
      }
      bool sharp = (i < n && notes[i] == '#');
      if (sharp) { i++; off++; }
      int oct = defOct;
      if (i < n && isdigit((unsigned char)notes[i])) {
        oct = notes[i] - '0'; i++;
        // Two-digit octave unlikely but tolerate.
        if (i < n && isdigit((unsigned char)notes[i])) { oct = oct * 10 + (notes[i] - '0'); i++; }
      }
      int m = 12 * (oct + 1) + off;
      if (m < MIDI_MIN) m = MIDI_MIN;
      if (m > MIDI_MAX) m = MIDI_MAX;
      midi = (uint8_t)m;
    }

    bool dotted = false;
    if (i < n && notes[i] == '.') { dotted = true; i++; }

    // Snap duration: dotted N → N/2 (next shorter base length, so 1.5x feel
    // is lost; acceptable given fixed-step model). Otherwise clamp to nearest.
    uint8_t lenCode = (uint8_t)dur;
    if (dotted) lenCode = nextLen(lenCode);
    if (!isValidLen(lenCode)) {
      // Clamp to closest valid length.
      uint8_t best = 4, bestErr = 255;
      static const uint8_t cands[] = {1,2,4,8,16,32};
      for (uint8_t c : cands) {
        uint8_t err = (lenCode > c) ? (lenCode - c) : (c - lenCode);
        if (err < bestErr) { bestErr = err; best = c; }
      }
      lenCode = best;
    }

    _steps[_stepCount++] = { (uint8_t)(isRest ? 0 : midi), lenCode };
  }

  return _stepCount > 0;
}

String GameMusicComposerScreen::_serializeRtttl() const {
  String out;
  out.reserve(64 + _stepCount * 6);

  // Strip the colons / commas from the name once more, just in case.
  String name = _songName;
  name.replace(",", " ");
  name.replace(":", " ");
  if (name.length() == 0) name = "song";

  out += name;
  out += ":d=4,o=5,b=";
  out += String((uint32_t)_bpm);
  out += ':';

  for (uint8_t i = 0; i < _stepCount; i++) {
    if (i > 0) out += ',';
    const Step& s = _steps[i];
    if (s.len != 4) out += String((uint32_t)s.len);
    if (s.midi == 0) {
      out += 'p';
    } else {
      uint8_t off = s.midi % 12;
      int     oct = (int)(s.midi / 12) - 1;
      const char* nm = kNoteNames[off];
      out += (char)tolower(nm[0]);
      if (nm[1] == '#') out += '#';
      out += String(oct);
    }
  }
  return out;
}

bool GameMusicComposerScreen::_loadFile(const String& path) {
  if (!Uni.Storage || !Uni.Storage->isAvailable()) return false;
  String body = Uni.Storage->readFile(path.c_str());
  if (body.length() == 0) return false;
  if (!_parseRtttl(body)) return false;
  return true;
}

bool GameMusicComposerScreen::_saveFile(const String& path) {
  if (!Uni.Storage || !Uni.Storage->isAvailable()) return false;
  return Uni.Storage->writeFile(path.c_str(), _serializeRtttl().c_str());
}