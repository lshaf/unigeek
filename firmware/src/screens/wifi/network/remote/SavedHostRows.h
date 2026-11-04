#pragma once

#include "ui/templates/ListScreen.h"
#include "ui/actions/ConfirmAction.h"
#include "ui/actions/InputTextAction.h"
#include "ui/actions/ShowStatusAction.h"
#include "utils/network/SavedHosts.h"

// The saved-host rows shared by the Remote Access clients' setup lists:
//   ... service fields ...
//   Save host        No | Yes
//   Name             (when saving)
//   Save password    No | Yes   (when saving, services with a password)
//   Connect / Start
//   Delete           (when the setup came from a saved host)
// The client lists its own fields, then appends tail(); a selected tail row
// goes back to select(). Passwords are not saved unless asked: the SD card
// is readable by anyone holding it.
class SavedHostRows {
public:
  enum Kind : uint8_t { NONE, SAVE, NAME, SAVE_PASSWORD, CONNECT, DELETE };

  SavedHostRows(const char* dir, bool hasPassword) : _dir(dir), _hasPassword(hasPassword) {}

  const char* dir() const { return _dir; }
  bool fromSaved() const { return _origName.length() > 0; }
  bool saving() const { return _save; }
  bool savesPassword() const { return _save && _hasPassword && _savePassword; }

  // Opened from Saved Hosts: Save host stays No (Yes overwrites it).
  void loadFrom(const SavedHost& h) {
    _origName = h.name;
    _name = h.name;
    _save = false;
    _savePassword = h.has("password");
  }

  // Appends the tail rows (from Save host to Delete) at items[n..]; returns
  // the new count. `connectLabel` is the client's own word (Connect, Start).
  uint8_t tail(ListScreen::ListItem* items, Kind* kinds, uint8_t n, uint8_t max, const char* connectLabel) {
    auto add = [&](const char* label, const char* value, Kind k) {
      if (n >= max) return;
      items[n] = {label, value};
      kinds[n] = k;
      ++n;
    };
    add("Save host", _save ? "Yes" : "No", SAVE);
    if (_save) {
      _nameLabel = _name.length() ? _name : String("-");
      add("Name", _nameLabel.c_str(), NAME);
      if (_hasPassword) add("Save password", _savePassword ? "Yes" : "No", SAVE_PASSWORD);
    }
    add(connectLabel, nullptr, CONNECT);
    if (fromSaved()) add("Delete", nullptr, DELETE);
    return n;
  }

  // Index of the Connect row in the last tail(), for the initial highlight.
  static uint8_t connectIndex(const Kind* kinds, uint8_t count) {
    for (uint8_t i = 0; i < count; ++i) if (kinds[i] == CONNECT) return i;
    return 0;
  }

  // Handles SAVE / NAME / SAVE_PASSWORD (the caller rebuilds its list).
  void select(Kind k) {
    switch (k) {
      case SAVE:
        _save = !_save;
        if (_save && _name.length() == 0) _askName();
        break;
      case NAME:          _askName(); break;
      case SAVE_PASSWORD: _savePassword = !_savePassword; break;
      default: break;
    }
  }

  // Before connecting: a host being saved needs a name. False if cancelled.
  bool prepare() {
    if (!_save || _name.length()) return true;
    _askName();
    return _name.length() > 0;
  }

  // Saves `host` (its fields filled in by the client) under the chosen name,
  // renaming the entry it was opened from. The password is dropped unless
  // asked for. No-op when Save host is No.
  void persist(SavedHost host) {
    if (!_save) return;
    if (!savesPassword()) {
      for (auto it = host.fields.begin(); it != host.fields.end();)
        it = it->first == "password" ? host.fields.erase(it) : it + 1;
    }
    host.name = _name;
    if (SavedHosts::upsert(_dir, host, _origName)) {
      _origName = _name;
      _save = false;
    } else {
      ShowStatusAction::show("Could not save host", 1500);
    }
  }

  // Confirms and deletes the saved host this setup came from. True if gone.
  bool confirmDelete() {
    if (!fromSaved()) return false;
    if (!ConfirmAction::popup("Delete host?", "This action cannot be undone")) return false;
    if (!SavedHosts::remove(_dir, _origName)) {
      ShowStatusAction::show("Could not delete host", 1500);
      return false;
    }
    _origName = "";
    return true;
  }

private:
  const char* _dir;
  bool        _hasPassword;
  bool        _save = false;
  bool        _savePassword = false;
  String      _name;
  String      _origName;
  String      _nameLabel;

  void _askName() {
    String v = InputTextAction::popup("Name", _name, InputTextAction::INPUT_TEXT);
    if (InputTextAction::wasCancelled()) return;
    v.trim();
    if (v.length()) _name = v;
  }
};
