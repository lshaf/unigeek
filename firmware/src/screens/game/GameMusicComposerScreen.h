#pragma once

#include <Arduino.h>
#include "ui/templates/ListScreen.h"
#include "ui/views/BrowseFileView.h"

class GameMusicComposerScreen : public ListScreen
{
public:
  const char* title() override;
  bool        inhibitPowerSave() override { return _state == STATE_PLAY || _state == STATE_EDIT; }

  void onInit() override;
  void onUpdate() override;
  void onRender() override;
  void onBack() override;
  void onItemSelected(uint8_t index) override;

private:
  static constexpr uint8_t  MAX_STEPS    = 64;
  static constexpr uint8_t  MIDI_MIN     = 36;  // C2
  static constexpr uint8_t  MIDI_MAX     = 96;  // C7

  // ── Note model ───────────────────────────────────────────────────────────────
  // midi = 0 → rest, else 36..96. len ∈ {1,2,4,8,16,32} (note denominator).
  struct Step { uint8_t midi; uint8_t len; };

  // ── State machine ────────────────────────────────────────────────────────────
  enum State : uint8_t {
    STATE_MENU,      // top-level list
    STATE_FILES,     // pick a saved .rtttl
    STATE_BUILTIN,   // pick a built-in demo
    STATE_EDIT,      // composer
    STATE_PLAY,      // non-blocking playback
  };

  State    _state = STATE_MENU;

  Step     _steps[MAX_STEPS];
  uint8_t  _stepCount = 0;
  uint8_t  _cursor    = 0;
  uint16_t _bpm       = 120;
  String   _songName;
  String   _filename;        // "" until first save
  bool     _dirty     = true;  // editor needs repaint

  // ── File listing ─────────────────────────────────────────────────────────────
  BrowseFileView _browser;

  // ── Menu items (one buffer reused per state) ─────────────────────────────────
  ListItem  _menuItems[3];
  ListItem  _builtinItems[8];

  // ── Playback ─────────────────────────────────────────────────────────────────
  uint8_t   _playIdx     = 0;
  uint32_t  _playNextMs  = 0;
  bool      _playFirst   = true;

  // ── Helpers ──────────────────────────────────────────────────────────────────
  void _enterMenu();
  void _enterFiles();
  void _enterBuiltin();
  void _enterEdit();
  void _enterPlay();

  void _newSong();
  void _ensureFirstStep();
  void _shiftSemitone(int delta);
  void _cycleLength();
  void _insertStepAtCursor();
  void _deleteStepAtCursor();
  void _clearAll();
  void _openActionMenu();
  void _save();
  void _rename();

  uint32_t _stepDurationMs(uint8_t lenCode) const;
  uint16_t _midiToFreq(uint8_t midi) const;

  void _renderEditor();
  void _renderPlayback();

  // RTTTL
  bool   _parseRtttl(const String& text);
  String _serializeRtttl() const;
  bool   _loadFile(const String& path);
  bool   _saveFile(const String& path);
};