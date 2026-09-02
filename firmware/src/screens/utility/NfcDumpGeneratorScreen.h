#pragma once

#include "ui/templates/ListScreen.h"
#include "ui/views/BrowseFileView.h"
#include "ui/views/ScrollListView.h"

class NfcDumpGeneratorScreen : public ListScreen
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
    STATE_TAG_TYPE,
    STATE_NDEF_CONTENT,
    STATE_NDEF_TYPE,
    STATE_NDEF_FILE_SELECT,
    STATE_NDEF_PREVIEW,
  };

  enum TagType {
    TAG_MIFARE_CLASSIC_1K,
    TAG_MIFARE_CLASSIC_4K,
    TAG_NTAG210,
    TAG_NTAG212,
    TAG_NTAG213,
    TAG_NTAG215,
    TAG_NTAG216,
  };

  State _state = STATE_TAG_TYPE;
  TagType _tagType = TAG_NTAG215;

  static constexpr const char* _nfcPath   = "/unigeek/nfc";
  static constexpr const char* _ndefPath  = "/unigeek/nfc/ndefs";
  static constexpr const char* _dumpPath  = "/unigeek/nfc/dumps";
  static constexpr size_t MAX_INPUT_NDEF  = 491;

  ListItem _tagItems[7] = {
    {"MIFARE Classic 1K"},
    {"MIFARE Classic 4K"},
    {"NTAG210"},
    {"NTAG212"},
    {"NTAG213"},
    {"NTAG215"},
    {"NTAG216"},
  };

  ListItem _contentItems[3] = {
    {"New Record"},
    {"Load NDEF Record from File"},
    {"Empty"},
  };

  ListItem _ndefTypeItems[5] = {
    {"Text"},
    {"URL"},
    {"Phone"},
    {"Email"},
    {"vCard"},
  };

  BrowseFileView _browser;
  String _ndefPickDir;

  // NDEF file preview: same ScrollListView/parser layout used by PN532 Read NDEF.
  ScrollListView _scrollView;
  static constexpr size_t MAX_ROWS = 64;
  ScrollListView::Row _rows[MAX_ROWS];
  String _rowLabels[MAX_ROWS];
  String _rowValues[MAX_ROWS];
  uint16_t _rowCount = 0;
  uint8_t _previewNdef[MAX_INPUT_NDEF] = {};
  size_t _previewNdefLen = 0;
  String _previewSuggestedName;
  bool _previewFromFile = false;

  void _goTagType();
  void _goNdefContent();
  void _goNdefType();
  void _openNdefFiles();
  void _selectNdefFile(uint8_t index);
  void _showNdefPreview(const uint8_t* ndef, size_t ndefLen, const String& suggestedName);
  void _resetRows();
  void _pushRow(const String& label, const String& value);
  void _pushWrappedRow(const String& label, const String& value);
  String _hexBlock(const uint8_t* data, uint8_t len) const;

  bool _saveDump(const uint8_t* ndef, size_t ndefLen,
                const String& suggestedName);
  bool _saveMifareClassic1K(const uint8_t* ndef, size_t ndefLen,
                            const String& suggestedName);
  bool _saveMifareClassic4K(const uint8_t* ndef, size_t ndefLen,
                            const String& suggestedName);
  bool _saveNtag21x(const uint8_t* ndef, size_t ndefLen,
                    const String& suggestedName);
  void _generateMifareUid(uint8_t uid[4]);
  void _generateUid(uint8_t uid[7]);
};
