#pragma once

#include <IPAddress.h>
#include "ui/templates/ListScreen.h"
#include "ui/views/TextScrollView.h"

// Base of the Network Toolbox tools (Ping, Traceroute, DNS Lookup,
// Wake-on-LAN): a setup list ending in Start, then an output view that grows
// line by line, like a terminal.
//
// Output: [Press] Stop while running, [Press] Restart once finished or
// stopped (same setup), [Back] returns to the setup (no hint), arrows
// scroll. The two-line footer matches the other live screens: the hint
// sits on the lower line.
class ToolScreen : public ListScreen {
public:
  ~ToolScreen() override = default;

  void onInit() override;
  void onUpdate() override;
  void onRender() override;
  void onItemSelected(uint8_t index) override;
  void onBack() override;

protected:
  static constexpr uint8_t MAX_ROWS = 8;

  // Setup rows before Start: add(label, value, id) for each.
  virtual void _buildRows() = 0;
  // Edits the row with this id (popups); the setup is redrawn afterwards.
  virtual void _editRow(uint8_t id) = 0;
  // Validates the setup and starts; false stays on the setup (after showing
  // why). May _print() the first lines.
  virtual bool _begin() = 0;
  // Runs a little more; false once finished. Called while running.
  virtual bool _step() = 0;
  // Releases resources after finishing or stopping.
  virtual void _end() {}

  void _addRow(const char* label, const String& value, uint8_t id);
  void _print(const String& line);

  // Host entry as in SSH: prefilled with the network prefix; a name or an IP
  // (keyboard boards type freely; others get the IP keyboard and its ABC page).
  static String _editHost(const char* title, const String& current);
  // Resolves an IP or a host name; false (with a status shown) if it can't.
  bool _resolve(const String& host, IPAddress& out);

private:
  enum State { STATE_SETUP, STATE_RUNNING, STATE_DONE };
  static constexpr uint8_t ID_START = 0xFF;
  static constexpr int     kFooterH = 27;
  static constexpr size_t  kMaxLog  = 6000;

  State          _state = STATE_SETUP;
  ListItem       _items[MAX_ROWS + 1];
  String         _values[MAX_ROWS];
  uint8_t        _ids[MAX_ROWS + 1] = {};
  uint8_t        _count = 0;
  uint8_t        _selected = 0;
  TextScrollView _view;
  String         _log;
  bool           _chromeDrawn = false;

  void _showSetup();
  void _start();
  void _finish(const char* note);
  void _renderOutput();
  void _renderFooter();
};
