#include "ui/actions/ConfirmAction.h"
#include "ChameleonMagicScreen.h"
#include "ChameleonMfcAdvancedScreen.h"
#include "ChameleonMfcScreen.h"
#include "ChameleonMfcToolsScreen.h"
#include "ChameleonMfcUidWriteScreen.h"
#include "ChameleonMfcWriteScreen.h"
#include "core/ScreenManager.h"
#include "ui/actions/InputSelectAction.h"
#include "ui/actions/InputTextAction.h"
#include "ui/actions/ShowStatusAction.h"
#include "ui/components/Header.h"
#include "ui/components/TagPrompt.h"
#include "ui/views/ProgressView.h"
#include "utils/IdentityFile.h"
#include "utils/ble/ChameleonClient.h"
#include "utils/nfc/HfDumpParser.h"

namespace {
static constexpr uint8_t kMfc1kSectors = 16;

static constexpr uint8_t kEraseKeys[][6] = {
  {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF},
  {0xA0,0xA1,0xA2,0xA3,0xA4,0xA5},
  {0xD3,0xF7,0xD3,0xF7,0xD3,0xF7},
  {0x00,0x00,0x00,0x00,0x00,0x00},
  {0xB0,0xB1,0xB2,0xB3,0xB4,0xB5},
  {0x4D,0x3A,0x99,0xC3,0x51,0xDD},
  {0x1A,0x98,0x2C,0x7E,0x45,0x9A},
  {0xAA,0xBB,0xCC,0xDD,0xEE,0xFF},
  {0x71,0x4C,0x5C,0x88,0x6E,0x97},
  {0x58,0x7E,0xE5,0xF9,0x35,0x0F},
  {0xA0,0x47,0x8C,0xC3,0x90,0x91},
  {0x53,0x3C,0xB6,0xC7,0x23,0xF6},
  {0x8F,0xD0,0xA4,0xF2,0x56,0xE9},
  {0x00,0x00,0x00,0x00,0x00,0x01},
  {0x11,0x22,0x33,0x44,0x55,0x66},
  {0x26,0x97,0x34,0x3B,0x00,0x00},
  {0x12,0x34,0x56,0x78,0x9A,0xBC},
  {0xBD,0x49,0x3A,0x39,0x62,0xB6},
};
static constexpr uint8_t kEraseKeyCount =
    sizeof(kEraseKeys) / sizeof(kEraseKeys[0]);

static uint8_t _mfcTrailer(uint8_t sector) {
  return sector * 4u + 3u;
}

static bool _mfcWriteWithKnownKey(
    ChameleonClient& c, uint8_t block, const uint8_t data[16],
    const uint8_t keyA[6], bool hasA,
    const uint8_t keyB[6], bool hasB) {
  if (hasA && c.mf1WriteBlock(block, 0x60, keyA, data)) return true;
  if (hasB && c.mf1WriteBlock(block, 0x61, keyB, data)) return true;
  return false;
}
}

void ChameleonMfcToolsScreen::onInit() {
  _state = MENU;
  _items[0] = {"Detect Magic"};
  _items[1] = {"Read Tag"};
  _items[2] = {"Write UID to Tag"};
  _items[3] = {"Write Dump to Tag"};
  _items[4] = {"Erase Tag"};
  _items[5] = {"Advanced"};
  setItems(_items, 6, _menuIndex);
}

void ChameleonMfcToolsScreen::_writeUid() {
  _uidSource=UID_MANUAL; _uidLen=0; _uidFile=""; _uidDumpFile=""; _rebuildUidForm(0);
}

void ChameleonMfcToolsScreen::_rebuildUidForm(uint8_t selected) {
  _state=UID_FORM; _uidCount=0;
  _uidValues[_uidCount]=_uidSource==UID_MANUAL?"Manual":_uidSource==UID_FILE?"UID File":"Dump";
  _uidItems[_uidCount]={"UID",_uidValues[_uidCount].c_str()}; ++_uidCount;
  if(_uidSource==UID_MANUAL){
    String v="-"; if(_uidLen){v="";char b[4];for(uint8_t i=0;i<_uidLen;++i){snprintf(b,sizeof(b),"%s%02X",i?":":"",_uid[i]);v+=b;}}
    _uidValues[_uidCount]=v; _uidItems[_uidCount]={"Value",_uidValues[_uidCount].c_str()}; ++_uidCount;
  } else if(_uidSource==UID_FILE){
    _uidValues[_uidCount]=_uidFile.length()?_uidFile.substring(_uidFile.lastIndexOf('/')+1):"-"; _uidItems[_uidCount]={"UID File",_uidValues[_uidCount].c_str()}; ++_uidCount;
  } else {
    _uidValues[_uidCount]=_uidDumpFile.length()?_uidDumpFile.substring(_uidDumpFile.lastIndexOf('/')+1):"-"; _uidItems[_uidCount]={"Dump File",_uidValues[_uidCount].c_str()}; ++_uidCount;
  }
  _uidItems[_uidCount++]={"Write"}; setItems(_uidItems,_uidCount,min<uint8_t>(selected,_uidCount-1));
}

void ChameleonMfcToolsScreen::_editUidManual(){
  String initial;char b[4];for(uint8_t i=0;i<_uidLen;++i){snprintf(b,sizeof(b),"%s%02X",i?":":"",_uid[i]);initial+=b;}
  String hex=InputTextAction::popup("UID (8 or 14 hex)",initial,InputTextAction::INPUT_HEX);if(InputTextAction::wasCancelled()){_rebuildUidForm(1);return;}hex.replace(" ","");hex.replace(":","");
  if(hex.length()!=8&&hex.length()!=14){_rebuildUidForm(1);ShowStatusAction::show("UID must be 4 or 7 bytes",1600);return;}_uidLen=(uint8_t)(hex.length()/2u);
  for(uint8_t i=0;i<_uidLen;++i){char x[3]={hex[i*2],hex[i*2+1],0};char*e=nullptr;unsigned long v=strtoul(x,&e,16);if(!e||*e){_uidLen=0;_rebuildUidForm(1);ShowStatusAction::show("Bad hex",1200);return;}_uid[i]=(uint8_t)v;}_rebuildUidForm(1);
}

void ChameleonMfcToolsScreen::_startUidWrite(){if(_uidLen!=4&&_uidLen!=7){ShowStatusAction::show(_uidSource==UID_MANUAL?"Enter UID":"Select source file",1600);render();return;}Screen.push(new ChameleonMfcUidWriteScreen(_uid,_uidLen));}

void ChameleonMfcToolsScreen::_writeUidFromFile() {
  _state = UID_FILE_PICKER;
  _browser.root = "/unigeek/nfc/uids";
  if (!_uidPickDir.startsWith(_browser.root)) _uidPickDir = _browser.root;
  const uint8_t n = _browser.load(this, _uidPickDir, BrowseFileView::Mode(".uid"));
  if (!n && _uidPickDir == _browser.root) {
    _rebuildUidForm(1);
    ShowStatusAction::show("No saved UIDs", 1600);
    render();
    return;
  }
  setItems(_browser.items(), n);
}

void ChameleonMfcToolsScreen::_writeUidFromDump() {
  _state = UID_DUMP_PICKER;
  _browser.root = "/unigeek/nfc/dumps";
  if (!_dumpPickDir.startsWith(_browser.root)) _dumpPickDir = _browser.root;
  const uint8_t n = _browser.load(this, _dumpPickDir, BrowseFileView::Mode(".bin", 320, 1024, 4096));
  if (!n && _dumpPickDir == _browser.root) {
    _rebuildUidForm(1);
    ShowStatusAction::show("No saved dumps", 1600);
    render();
    return;
  }
  setItems(_browser.items(), n);
}

void ChameleonMfcToolsScreen::_writeFromFile() { _openDumpFileBrowser(); }
void ChameleonMfcToolsScreen::_openDumpFileBrowser(){
  _state=DUMP_FILE_PICKER; _browser.root="/unigeek/nfc/dumps"; if(!_dumpPickDir.startsWith(_browser.root))_dumpPickDir=_browser.root;
  const uint8_t n=_browser.load(this,_dumpPickDir,BrowseFileView::Mode(".bin",1024));
  if(!n&&_dumpPickDir==_browser.root){_state=MENU;render();ShowStatusAction::show("No Classic 1K dumps",1600);render();return;}setItems(_browser.items(),n);
}

void ChameleonMfcToolsScreen::_writeFromSlot() {
  auto& c = ChameleonClient::get();
  ChameleonClient::SlotTypes types[8] = {};
  if (!c.getSlotTypes(types)) {
    render();
    ShowStatusAction::show("Could not read slots", 1600);
    render();
    return;
  }

  InputSelectAction::Option opts[8];
  String labels[8];
  String vals[8];
  for (uint8_t i = 0; i < 8; ++i) {
    labels[i] = String("Slot ") + (i + 1) + " - " +
                ChameleonClient::tagTypeName(types[i].hfType);
    vals[i] = String(i);
    opts[i] = {labels[i].c_str(), vals[i].c_str()};
  }

  const char* r = InputSelectAction::popup("Source slot", opts, 8, nullptr);
  if (!r) { render(); return; }
  const uint8_t slot = (uint8_t)atoi(r);
  if (slot >= 8) { render(); return; }

  const uint16_t hfType = types[slot].hfType;
  if (hfType == 0) {
    render();
    ShowStatusAction::show("Empty slot", 1200);
    render();
    return;
  }
  if (!(hfType == 1001)) {
    render();
    ShowStatusAction::show("Tag not supported", 1600);
    render();
    return;
  }

  Screen.push(new ChameleonMfcWriteScreen(slot));
}

void ChameleonMfcToolsScreen::_writeTag() {
  static const InputSelectAction::Option opts[] = {
    {"From File", "file"},
    {"From Slot", "slot"},
  };
  const char* r = InputSelectAction::popup("Write Dump to Tag", opts, 2, nullptr);
  if (!r) { render(); return; }
  if (strcmp(r, "file") == 0) _writeFromFile();
  else _writeFromSlot();
}

void ChameleonMfcToolsScreen::_eraseTag() {
  if (!ConfirmAction::popup("Erase tag?", "This action cannot be undone")) { render(); return; }
  Header header;
  header.render("Erase Tag");

  auto& c = ChameleonClient::get();

  uint8_t    previousMode = 0;
  const bool restoreMode  = c.getMode(&previousMode);
  c.setMode(1);

  auto&     lcd = Uni.Lcd;
  const int bx = bodyX(), by = bodyY(), bw = bodyW(), bh = bodyH();

  lcd.fillRect(bx, by, bw, bh, TFT_BLACK);
  lcd.setTextDatum(MC_DATUM);
  lcd.setTextSize(1);
  lcd.setTextColor(TFT_YELLOW, TFT_BLACK);
  TagPrompt::show("Waiting for tag...", bx, by, bw, bh, "Erase Tag");

  uint8_t uid[7] = {}, uidLen = 0, atqa[2] = {}, sak = 0;
  bool found = false;
  const uint32_t start = millis();
  while (millis() - start < 5000) {
    Uni.update();
    if (Uni.Nav->wasPressed() && Uni.Nav->readDirection() == INavigation::DIR_BACK) {
      if (restoreMode) c.setMode(previousMode);
      render();
      return;
    }
    if (c.scan14A(uid, &uidLen, atqa, &sak)) { found = true; break; }
    delay(50);
  }
  if (!found || sak != 0x08 || !c.mf1Support()) {
    if (restoreMode) c.setMode(previousMode);
    render();
    ShowStatusAction::show(found ? "Tag not supported" : "Tag not detected", found ? 1600 : 1200);
    render();
    return;
  }

  // Match PN532 Erase Tag semantics: preserve manufacturer block and every
  // sector trailer (keys/access bits), and clear data blocks only.
  uint8_t keysA[kMfc1kSectors][6] = {}, keysB[kMfc1kSectors][6] = {};
  bool    foundA[kMfc1kSectors]   = {}, foundB[kMfc1kSectors]   = {};

  ProgressView::init();
  bool keysOk = true;

  const uint16_t totalKeys = kMfc1kSectors * 2u;
  for (uint8_t sector = 0; sector < kMfc1kSectors; ++sector) {
    const uint8_t block = sector * 4u;
    char          msg[40];

    const uint16_t keyAIndex = sector * 2u + 1u;
    snprintf(msg, sizeof(msg), "Checking keys (%u/%u)...", (unsigned)keyAIndex, (unsigned)totalKeys);
    ProgressView::progress(msg, (int)((uint32_t)(keyAIndex - 1u) * 100u / totalKeys));
    foundA[sector] = c.mf1CheckKeysOfBlock(block, 0x60, &kEraseKeys[0][0], kEraseKeyCount, keysA[sector]);

    const uint16_t keyBIndex = keyAIndex + 1u;
    snprintf(msg, sizeof(msg), "Checking keys (%u/%u)...", (unsigned)keyBIndex, (unsigned)totalKeys);
    ProgressView::progress(msg, (int)((uint32_t)(keyBIndex - 1u) * 100u / totalKeys));
    foundB[sector] = c.mf1CheckKeysOfBlock(block, 0x61, &kEraseKeys[0][0], kEraseKeyCount, keysB[sector]);

    if (!foundA[sector] && !foundB[sector]) { keysOk = false; break; }
  }
  ProgressView::finish();

  if (!keysOk) {
    if (restoreMode) c.setMode(previousMode);
    render();
    ShowStatusAction::show("Key not available", 1600);
    render();
    return;
  }

  static constexpr uint16_t kDataBlocks = 47;

  uint8_t  zero[16] = {};
  uint16_t erased   = 0;
  bool     ok       = true;

  ProgressView::init();
  for (uint8_t sector = 0; sector < kMfc1kSectors && ok; ++sector) {
    const uint8_t first = sector * 4u, trailer = _mfcTrailer(sector);

    for (uint8_t block = first; block < trailer; ++block) {
      if (block == 0) continue;   // manufacturer block stays untouched

      char msg[36];
      snprintf(msg, sizeof(msg), "Erasing blocks (%u/%u)...", (unsigned)(erased + 1u), (unsigned)kDataBlocks);
      ProgressView::progress(msg, (int)((uint32_t)erased * 100u / kDataBlocks));

      ok = _mfcWriteWithKnownKey(c, block, zero, keysA[sector], foundA[sector], keysB[sector], foundB[sector]);
      if (!ok) break;
      ++erased;
    }
  }

  const bool complete = ok && erased == kDataBlocks;
  if (complete) ProgressView::progress("Erase complete", 100);
  ProgressView::finish();

  if (restoreMode) c.setMode(previousMode);
  render();
  ShowStatusAction::show(complete ? "Tag erased" : "Failed", 1600);
  render();
}

void ChameleonMfcToolsScreen::onItemSelected(uint8_t index) {
  if (_state == UID_FORM) {
    if(index==0){static const InputSelectAction::Option src[]={{"Manual","manual"},{"UID File","uid"},{"Dump","dump"}};const char*r=InputSelectAction::popup("UID",src,3,_uidSource==UID_MANUAL?"manual":_uidSource==UID_FILE?"uid":"dump");if(r){_uidSource=strcmp(r,"manual")==0?UID_MANUAL:strcmp(r,"uid")==0?UID_FILE:UID_DUMP;_uidLen=0;_uidFile="";_uidDumpFile="";_rebuildUidForm(0);}else render();}
    else if(index==1){if(_uidSource==UID_MANUAL)_editUidManual();else if(_uidSource==UID_FILE)_writeUidFromFile();else _writeUidFromDump();}
    else if(index==2)_startUidWrite(); return;
  }
  if (_state == UID_FILE_PICKER || _state == UID_DUMP_PICKER || _state == DUMP_FILE_PICKER) {
    if (index >= _browser.count()) return;
    const auto entry = _browser.entry(index);
    if (entry.isDir) {
      if (_state == UID_FILE_PICKER) { _uidPickDir = entry.path; _writeUidFromFile(); }
      else if(_state==UID_DUMP_PICKER){ _dumpPickDir = entry.path; _writeUidFromDump(); }
      else {_dumpPickDir=entry.path;_openDumpFileBrowser();}
      return;
    }
    if(_state==DUMP_FILE_PICKER){Screen.push(new ChameleonMfcWriteScreen(entry.path));return;}
    if (_state == UID_FILE_PICKER) {
      size_t n=0;uint8_t uid[7]={};if(!IdentityFile::loadNfcUid(entry.path,uid,sizeof(uid),n)||(n!=4&&n!=7)){ShowStatusAction::show("Invalid UID file",1600);return;}memcpy(_uid,uid,n);_uidLen=(uint8_t)n;_uidFile=entry.path;_rebuildUidForm(1);return;
    }
    fs::File f = Uni.Storage->open(entry.path.c_str(), "r");
    if (!f || f.size() == 0 || f.size() > 4096) {
      if (f) f.close();
      ShowStatusAction::show("Invalid dump", 1600);
      return;
    }
    const size_t len = f.size();
    uint8_t* dump = new uint8_t[len];
    if (!dump) { f.close(); ShowStatusAction::show("Out of memory", 1600); return; }
    const size_t got = f.read(dump, len); f.close();
    if (got != len) { delete[] dump; ShowStatusAction::show("Invalid dump", 1600); return; }
    const auto info = HfDumpParser::inspect(dump, len); delete[] dump;
    if (!HfDumpParser::isMifareClassic(info.type) || !info.uidValid ||
        (info.uidLen != 4 && info.uidLen != 7)) {
      ShowStatusAction::show("UID dump not supported", 1600);
      return;
    }
    memcpy(_uid,info.uid,info.uidLen);_uidLen=info.uidLen;_uidDumpFile=entry.path;_rebuildUidForm(1);
    return;
  }
  _menuIndex = index;
  if (index == 0) Screen.push(new ChameleonMagicScreen());
  else if (index == 1) Screen.push(new ChameleonMfcScreen());
  else if (index == 2) _writeUid();
  else if (index == 3) _writeTag();
  else if (index == 4) _eraseTag();
  else if (index == 5) Screen.push(new ChameleonMfcAdvancedScreen());
}

void ChameleonMfcToolsScreen::onBack() {
  if(_state==UID_FORM){onInit();return;}
  if(_state==UID_FILE_PICKER){if(_uidPickDir=="/unigeek/nfc/uids"){_rebuildUidForm(1);return;}int slash=_uidPickDir.lastIndexOf('/');_uidPickDir=slash>0?_uidPickDir.substring(0,slash):String("/unigeek/nfc/uids");_writeUidFromFile();return;}
  if(_state==UID_DUMP_PICKER){if(_dumpPickDir=="/unigeek/nfc/dumps"){_rebuildUidForm(1);return;}int slash=_dumpPickDir.lastIndexOf('/');_dumpPickDir=slash>0?_dumpPickDir.substring(0,slash):String("/unigeek/nfc/dumps");_writeUidFromDump();return;}
  if(_state==DUMP_FILE_PICKER){if(_dumpPickDir=="/unigeek/nfc/dumps"){onInit();return;}int slash=_dumpPickDir.lastIndexOf('/');_dumpPickDir=slash>0?_dumpPickDir.substring(0,slash):String("/unigeek/nfc/dumps");_openDumpFileBrowser();return;}
  if (_state != MENU) { onInit(); return; }
  Screen.goBack();
}
