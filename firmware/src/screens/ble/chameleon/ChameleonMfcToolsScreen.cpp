#include "ChameleonMfcToolsScreen.h"
#include "ChameleonMfcScreen.h"
#include "ChameleonMfcWriteScreen.h"
#include "utils/ble/ChameleonClient.h"
#include "core/ScreenManager.h"
#include "ui/actions/InputSelectAction.h"
#include "ui/actions/ShowStatusAction.h"
#include "ui/components/Header.h"
#include "ui/views/ProgressView.h"


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
  _items[0] = {"Read Tag"};
  _items[1] = {"Write to Tag"};
  _items[2] = {"Erase Tag"};
  setItems(_items);
}

void ChameleonMfcToolsScreen::_writeFromFile() {
  static constexpr uint8_t kMax = 10;
  uint8_t n = _browser.load(this, "/unigeek/nfc/dumps", BrowseFileView::Mode(BrowseFileView::Mode::FILE_ONLY, ".bin", 1024));
  if (!n) {
    render();
    ShowStatusAction::show("No Classic 1K .bin", 1500);
    render();
    return;
  }
  const uint8_t count = n < kMax ? n : kMax;
  InputSelectAction::Option opts[kMax];
  String vals[kMax];
  for (uint8_t i = 0; i < count; ++i) {
    vals[i] = String(i);
    opts[i] = {_browser.entry(i).name.c_str(), vals[i].c_str()};
  }
  const char* r = InputSelectAction::popup("Classic 1K dump", opts, count, nullptr);
  if (!r) { render(); return; }
  const uint8_t idx = (uint8_t)atoi(r);
  if (idx >= count) { render(); return; }
  String path = _browser.entry(idx).path;
  render();
  Screen.push(new ChameleonMfcWriteScreen(path));
}

void ChameleonMfcToolsScreen::_writeFromSlot() {
  auto& c = ChameleonClient::get();
  ChameleonClient::SlotTypes types[8] = {};
  if (!c.getSlotTypes(types)) {
    render();
    ShowStatusAction::show("Could not read slots", 1500);
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
    ShowStatusAction::show("Unsupported tag type", 1500);
    render();
    return;
  }

  Screen.push(new ChameleonMfcWriteScreen(slot));
}

void ChameleonMfcToolsScreen::_writeTag() {
  static const InputSelectAction::Option opts[] = {
    {"from File", "file"},
    {"from Slot", "slot"},
  };
  const char* r = InputSelectAction::popup("Write to Tag", opts, 2, nullptr);
  if (!r) { render(); return; }
  if (strcmp(r, "file") == 0) _writeFromFile();
  else _writeFromSlot();
}


void ChameleonMfcToolsScreen::_eraseTag() {
  Header header; header.render("Erase Tag");
  auto& c = ChameleonClient::get();
  uint8_t previousMode = 0; const bool restoreMode = c.getMode(&previousMode); c.setMode(1);
  auto& lcd = Uni.Lcd; const int bx=bodyX(),by=bodyY(),bw=bodyW(),bh=bodyH();
  lcd.fillRect(bx,by,bw,bh,TFT_BLACK); lcd.setTextDatum(MC_DATUM); lcd.setTextSize(1); lcd.setTextColor(TFT_YELLOW,TFT_BLACK); lcd.drawString("Place MFC1K tag...",bx+bw/2,by+bh/2);
  uint8_t uid[7]={},uidLen=0,atqa[2]={},sak=0;
  if(!c.scan14A(uid,&uidLen,atqa,&sak)||sak!=0x08){if(restoreMode)c.setMode(previousMode);render();ShowStatusAction::show("Target must be MFC1K",1500);render();return;}
  // Match PN532 Erase Tag semantics: preserve manufacturer block and every
  // sector trailer (keys/access bits), and clear data blocks only.
  uint8_t keysA[kMfc1kSectors][6]={},keysB[kMfc1kSectors][6]={}; bool foundA[kMfc1kSectors]={},foundB[kMfc1kSectors]={};
  ProgressView::init(); bool keysOk=true;
  for(uint8_t sector=0;sector<kMfc1kSectors;++sector){const uint8_t block=sector*4u;char msg[40];const uint16_t totalKeys=kMfc1kSectors*2u;const uint16_t keyAIndex=sector*2u+1u;snprintf(msg,sizeof(msg),"Checking keys (%u/%u)...",(unsigned)keyAIndex,(unsigned)totalKeys);ProgressView::progress(msg,(int)((uint32_t)(keyAIndex-1u)*100u/totalKeys));foundA[sector]=c.mf1CheckKeysOfBlock(block,0x60,&kEraseKeys[0][0],kEraseKeyCount,keysA[sector]);const uint16_t keyBIndex=keyAIndex+1u;snprintf(msg,sizeof(msg),"Checking keys (%u/%u)...",(unsigned)keyBIndex,(unsigned)totalKeys);ProgressView::progress(msg,(int)((uint32_t)(keyBIndex-1u)*100u/totalKeys));foundB[sector]=c.mf1CheckKeysOfBlock(block,0x61,&kEraseKeys[0][0],kEraseKeyCount,keysB[sector]);if(!foundA[sector]&&!foundB[sector]){keysOk=false;break;}}
  ProgressView::finish(); if(!keysOk){if(restoreMode)c.setMode(previousMode);render();ShowStatusAction::show("Erase failed: missing key",1700);render();return;}
  static constexpr uint16_t kDataBlocks=47; uint8_t zero[16]={}; uint16_t erased=0; bool ok=true; ProgressView::init();
  for(uint8_t sector=0;sector<kMfc1kSectors&&ok;++sector){const uint8_t first=sector*4u,trailer=_mfcTrailer(sector);for(uint8_t block=first;block<trailer;++block){if(block==0)continue;char msg[36];snprintf(msg,sizeof(msg),"Erasing blocks (%u/%u)...",(unsigned)(erased+1u),(unsigned)kDataBlocks);ProgressView::progress(msg,(int)((uint32_t)erased*100u/kDataBlocks));ok=_mfcWriteWithKnownKey(c,block,zero,keysA[sector],foundA[sector],keysB[sector],foundB[sector]);if(!ok)break;++erased;}}
  if(ok&&erased==kDataBlocks)ProgressView::progress("Erase complete",100);ProgressView::finish();if(restoreMode)c.setMode(previousMode);render();ShowStatusAction::show((ok&&erased==kDataBlocks)?"Tag erased":"Erase failed",1600);render();
}

void ChameleonMfcToolsScreen::onItemSelected(uint8_t index) {
  if (index == 0) Screen.push(new ChameleonMfcScreen());
  else if (index == 1) _writeTag();
  else if (index == 2) _eraseTag();
}

void ChameleonMfcToolsScreen::onBack() { Screen.goBack(); }
