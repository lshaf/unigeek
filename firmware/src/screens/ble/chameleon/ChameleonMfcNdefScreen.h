#pragma once

#include "ui/templates/ListScreen.h"
#include "ui/views/BrowseFileView.h"
#include "ui/views/ScrollListView.h"

class ChameleonMfcNdefScreen : public ListScreen {
public:
  const char* title() override;
  bool inhibitPowerOff() override { return _running; }

  void onInit() override;
  void onUpdate() override;
  void onRender() override;
  void onItemSelected(uint8_t index) override;
  void onBack() override;

private:
  enum State {
    STATE_MENU,
    STATE_WRITE_MENU,
    STATE_RESULT,
    STATE_FILE_SELECT,
  };

  static constexpr size_t MAX_NDEF_BYTES = 254;
  static constexpr const char* NDEF_DIR = "/unigeek/nfc/ndefs";

  State _state = STATE_MENU;
  bool _running = false;
  bool _writePreview = false;
  bool _writePreviewFromFile = false;

  uint8_t _uid[7] = {};
  uint8_t _uidLen = 0;
  uint8_t _sak = 0;
  uint8_t _atqa[2] = {};
  uint8_t _sectors = 16;

  uint8_t _ndef[MAX_NDEF_BYTES] = {};
  size_t _ndefLen = 0;
  size_t _capacity = 0;
  bool _hasNdef = false;

  ListItem _menuItems[4] = {
    {"Read NDEF"},
    {"Write NDEF"},
    {"Erase NDEF"},
    {"Format NDEF"},
  };
  ListItem _writeItems[6] = {
    {"Text"}, {"URL"}, {"Phone"}, {"Email"}, {"vCard"}, {"Load from File"},
  };

  ScrollListView _scrollView;
  static constexpr uint8_t MAX_ROWS = 48;
  ScrollListView::Row _rows[MAX_ROWS];
  String _labels[MAX_ROWS];
  String _values[MAX_ROWS];
  uint8_t _rowCount = 0;

  BrowseFileView _browser;
  String _pickDir;

  void _goMenu();
  void _goWriteMenu();
  void _loadFilePicker();
  void _selectFile(uint8_t index);

  bool _scanClassic();
  uint8_t _trailerBlock(uint8_t sector) const;
  uint16_t _firstBlock(uint8_t sector) const;
  bool _readNdefSectors(uint8_t* sectors, size_t maxSectors, size_t& count);
  bool _readNdefArea(const uint8_t* sectors, size_t sectorCount,
                     uint8_t*& area, size_t& areaLen);
  bool _writeNdefRecord(const uint8_t* ndef, size_t ndefLen);
  bool _formatClassic1kNdef();

  void _doRead();
  void _doErase();
  void _showResult(const uint8_t* ndef, size_t ndefLen);
  void _showActions();
  void _saveCurrent();

  void _writeText();
  void _writeUrl();
  void _writePhone();
  void _writeEmail();
  void _writeVcard();
  void _showWritePreview(const uint8_t* ndef, size_t ndefLen, bool fromFile);

  void _resetRows();
  void _addRow(const String& label, const String& value);
  void _addWrapped(const String& label, const String& value);
  String _uidString() const;
};
