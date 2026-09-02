#pragma once

#include "ui/templates/ListScreen.h"
#include "ui/views/BrowseFileView.h"
#include "ui/views/ScrollListView.h"

class NdefEditorScreen : public ListScreen
{
public:
  const char* title() override;

  void onInit() override;
  void onUpdate() override;
  void onRender() override;
  void onBack() override;
  void onItemSelected(uint8_t index) override;

private:
  enum State {
    STATE_FILE_SELECT,
    STATE_PREVIEW_INITIAL,
    STATE_PREVIEW_FINAL,
    STATE_VCARD_FORM,
  };

  enum RecordType {
    REC_UNSUPPORTED,
    REC_TEXT,
    REC_URL,
    REC_PHONE,
    REC_EMAIL,
    REC_VCARD,
  };

  static constexpr const char* _nfcPath  = "/unigeek/nfc";
  static constexpr const char* _ndefPath = "/unigeek/nfc/ndefs";
  static constexpr size_t MAX_NDEF_BYTES = 491;
  static constexpr size_t MAX_ROWS = 64;

  State _state = STATE_FILE_SELECT;
  RecordType _recordType = REC_UNSUPPORTED;

  BrowseFileView _browser;
  String _pickDir;

  ScrollListView _scrollView;
  ScrollListView::Row _rows[MAX_ROWS];
  String _rowLabels[MAX_ROWS];
  String _rowValues[MAX_ROWS];
  uint16_t _rowCount = 0;

  uint8_t _ndef[MAX_NDEF_BYTES] = {};
  size_t _ndefLen = 0;
  String _filePath;
  String _baseName;

  // Parsed editable values.
  String _text;
  String _url;
  String _phone;
  String _email;
  String _contact;
  String _company;
  String _address;
  String _vcardPhone;
  String _vcardEmail;
  String _website;

  String _vcardDisplay[6];
  ListItem _vcardItems[7];

  void _openVcardForm();
  void _refreshVcardForm(uint8_t selected = 0);
  void _editVcardField(uint8_t index);
  void _finishVcardEdit();
  bool _confirmVcardSave();

  void _openFiles();
  void _selectFile(uint8_t index);
  bool _parseNdef(const uint8_t* ndef, size_t len);
  void _showPreview();
  bool _editRecord();
  bool _rebuildRecord(uint8_t* out, size_t& outLen);
  bool _saveEdited(const uint8_t* ndef, size_t len);

  void _resetRows();
  void _pushRow(const String& label, const String& value);
  void _pushWrappedRow(const String& label, const String& value);
};
