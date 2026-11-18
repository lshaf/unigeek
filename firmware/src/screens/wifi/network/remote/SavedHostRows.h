#pragma once

#include "ui/templates/ListScreen.h"
#include "ui/actions/InputTextAction.h"
#include "ui/actions/ShowStatusAction.h"
#include "utils/network/SavedHosts.h"

// The saved-host rows shared by the Remote Access clients' setup lists:
//   ... service fields ...
//   Save Host        No | Yes   ("Save Changes" when editing a saved host)
//   Name             "-" until set (when saving)
//   Save Password    No | Yes   (when saving, with password authentication)
//   Connect / Start
// The client lists its own fields, then appends tail(); a selected tail row
// goes back to select(). Nothing is asked for on its own: Name is filled in
// by selecting its row, and Connect without one shows "Name not set".
// Passwords are not saved unless asked: the SD card is readable by anyone
// holding it.
class SavedHostRows {
public:
  enum Kind : uint8_t { NONE, SAVE, NAME, SAVE_PASSWORD, CONNECT };

  SavedHostRows(const char* dir, bool hasPassword) : _dir(dir), _hasPassword(hasPassword) {}

  const char* dir() const { return _dir; }
  bool fromSaved() const { return _origName.length() > 0; }
  bool saving() const { return _save; }
  bool savesPassword() const { return _save && _passwordRow() && _savePassword; }

  // Whether the client currently authenticates with a password (SSH / SFTP:
  // Auth Method = Password). Save Password shows only then. Call before tail().
  void setPasswordApplies(bool applies) { _passwordApplies = applies; }

  // How many tail rows come before Connect (Save Host, and Name / Save
  // Password while saving): lets a client find its Connect row.
  uint8_t rowsBeforeConnect() const { return 1 + (_save ? 1 + (_passwordRow() ? 1 : 0) : 0); }

  // Opened from Saved Hosts: the row reads "Save Changes" and stays No (Yes
  // overwrites the entry); connecting without saving is the common case.
  void loadFrom(const SavedHost& h) {
    _origName = h.name;
    _name = h.name;
    _save = false;
    _savePassword = h.has("password");
  }

  // Appends the tail rows (from Save Host to Delete) at items[n..]; returns
  // the new count. `connectLabel` is the client's own word (Connect, Start).
  uint8_t tail(ListScreen::ListItem* items, Kind* kinds, uint8_t n, uint8_t max, const char* connectLabel) {
    auto add = [&](const char* label, const char* value, Kind k) {
      if (n >= max) return;
      items[n] = {label, value};
      kinds[n] = k;
      ++n;
    };
    add(fromSaved() ? "Save Changes" : "Save Host", _save ? "Yes" : "No", SAVE);
    if (_save) {
      _nameLabel = _name.length() ? _name : String("-");
      add("Name", _nameLabel.c_str(), NAME);
      if (_passwordRow()) add("Save Password", _savePassword ? "Yes" : "No", SAVE_PASSWORD);
    }
    add(connectLabel, nullptr, CONNECT);
    return n;
  }

  // Handles SAVE / NAME / SAVE_PASSWORD (the caller rebuilds its list).
  void select(Kind k) {
    switch (k) {
      case SAVE:          _save = !_save; break;
      case NAME:          _askName(); break;
      case SAVE_PASSWORD: _savePassword = !_savePassword; break;
      default: break;
    }
  }

  // Before connecting: a host being saved needs a name ("Name not set").
  bool prepare() {
    if (!_save || _name.length()) return true;
    ShowStatusAction::show("Name not set", 1200);
    return false;
  }

  // Saves `host` (its fields filled in by the client) under the chosen name,
  // renaming the entry it was opened from. The password is dropped unless
  // asked for. No-op when Save Host is No.
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

private:
  const char* _dir;
  bool        _hasPassword;
  bool        _passwordApplies = true;
  bool        _save = false;
  bool        _savePassword = false;
  String      _name;
  String      _origName;
  String      _nameLabel;

  bool _passwordRow() const { return _hasPassword && _passwordApplies; }

  void _askName() {
    String v = InputTextAction::popup("Name", _name, InputTextAction::INPUT_TEXT);
    if (InputTextAction::wasCancelled()) return;
    v.trim();
    if (v.length()) _name = v;
  }
};
