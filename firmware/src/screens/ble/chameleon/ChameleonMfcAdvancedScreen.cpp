#include "ui/actions/ConfirmAction.h"
#include <new>
#include "ChameleonMfcAdvancedScreen.h"
#include "screens/utility/NfcMemoryViewScreen.h"
#include "core/ScreenManager.h"
#include "ui/actions/InputNumberAction.h"
#include "ui/actions/InputSelectAction.h"
#include "ui/actions/InputTextAction.h"
#include "ui/actions/ShowStatusAction.h"
#include "ui/components/Header.h"
#include "ui/components/StatusBar.h"
#include "ui/components/TagPrompt.h"
#include "ui/views/ProgressView.h"
#include "utils/ble/ChameleonClient.h"

namespace {
static void renderOperationChrome(const char* title) {
  Header header;
  header.render(title);
  StatusBar::refresh();
}

static void renderTagPrompt(const char* message, int bx, int by, int bw, int bh) {
  TagPrompt::show(message, bx, by, bw, bh);
}
static bool scanClassicOrShow(ChameleonClient& c, uint8_t uid[7], uint8_t& uidLen,
                              uint8_t atqa[2], uint8_t& sak,
                              int bx, int by, int bw, int bh,
                              uint32_t timeoutMs = 5000) {
  renderTagPrompt("Waiting for tag...", bx, by, bw, bh);
  const uint32_t start = millis();
  while (millis() - start < timeoutMs) {
    Uni.update();
    if (Uni.Nav->wasPressed() && Uni.Nav->readDirection() == INavigation::DIR_BACK)
      return false;
    if (c.scan14A(uid, &uidLen, atqa, &sak)) {
      if (sak == 0x08 || sak == 0x18) return true;
      ShowStatusAction::show("Tag not supported", 1200);
      return false;
    }
    delay(50);
  }
  ShowStatusAction::show("Tag not detected", 1200);
  return false;
}
static const uint8_t kKeys[][6] = {
  {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF},{0xA0,0xA1,0xA2,0xA3,0xA4,0xA5},
  {0xD3,0xF7,0xD3,0xF7,0xD3,0xF7},{0,0,0,0,0,0},{0xB0,0xB1,0xB2,0xB3,0xB4,0xB5},
  {0x4D,0x3A,0x99,0xC3,0x51,0xDD},{0x1A,0x98,0x2C,0x7E,0x45,0x9A},{0xAA,0xBB,0xCC,0xDD,0xEE,0xFF}
};
static uint8_t sectorForBlock(uint16_t b){ return b<128 ? b/4 : 32+(b-128)/16; }
static bool findKey(ChameleonClient& c,uint8_t block,uint8_t out[6],uint8_t& type){
  const uint8_t count=(uint8_t)(sizeof(kKeys)/sizeof(kKeys[0]));
  if(c.mf1CheckKeysOfBlock(block,0x60,&kKeys[0][0],count,out)){type=0x60;return true;}
  if(c.mf1CheckKeysOfBlock(block,0x61,&kKeys[0][0],count,out)){type=0x61;return true;}
  return false;
}
}

void ChameleonMfcAdvancedScreen::onInit(){
  _items[0] = {"Read Memory"};
  _items[1] = {"Edit Memory"};
  _items[2] = {"Lock UID (Gen3)"};
  setItems(_items, 3, _selMenu);
}
void ChameleonMfcAdvancedScreen::onItemSelected(uint8_t i){
  _selMenu = i;
  if (i == 0) _readMemory();
  else if (i == 1) _editMemory();
  else if (i == 2) _lockUidGen3();
}

void ChameleonMfcAdvancedScreen::onBack(){ Screen.goBack(); }

void ChameleonMfcAdvancedScreen::_readMemory(){
  renderOperationChrome("Read Memory");
  auto& c=ChameleonClient::get();
  uint8_t previousMode=0; const bool restoreMode=c.getMode(&previousMode); c.setMode(1);
  auto restore=[&](){ if(restoreMode) c.setMode(previousMode); };
  uint8_t uid[7]={},ul=0,atqa[2]={},sak=0;
  if(!scanClassicOrShow(c,uid,ul,atqa,sak,bodyX(),bodyY(),bodyW(),bodyH())){
    restore(); render(); return;
  }
  const uint16_t blocks=sak==0x18?256:64; uint8_t keys[40][6]={},types[40]={}; bool have[40]={};
  uint8_t* dump = new(std::nothrow) uint8_t[(size_t)blocks * 16u]();
  uint8_t* valid = new(std::nothrow) uint8_t[blocks]();
  if (!dump || !valid) {
    delete[] dump; delete[] valid; restore(); render();
    ShowStatusAction::show("Out of memory", 1200); return;
  }
  ProgressView::init();
  for(uint16_t b=0;b<blocks;++b){
    uint8_t sec=sectorForBlock(b); if(!have[sec])have[sec]=findKey(c,(uint8_t)b,keys[sec],types[sec]);
    uint8_t* data=dump+(size_t)b*16u; bool ok=have[sec]&&c.mf1ReadBlock((uint8_t)b,types[sec],keys[sec],data);
    valid[b]=ok?1:0;
    char msg[36];snprintf(msg,sizeof(msg),"Reading blocks (%u/%u)...",(unsigned)(b+1),(unsigned)blocks);ProgressView::progress(msg,(int)((uint32_t)(b+1u)*100u/blocks));
  }
  ProgressView::finish(); restore(); render();
  Screen.push(new NfcMemoryViewScreen(NfcMemoryViewScreen::MIFARE_CLASSIC,
                                      sak==0x18?"MF Classic 4K":"MF Classic 1K",
                                      uid, ul, dump, (size_t)blocks*16u, valid));
  delete[] dump; delete[] valid;
}
void ChameleonMfcAdvancedScreen::_editMemory(){
  renderOperationChrome("Edit Memory");
  auto& c=ChameleonClient::get();
  uint8_t previousMode=0; const bool restoreMode=c.getMode(&previousMode); c.setMode(1);
  auto restore=[&](){ if(restoreMode) c.setMode(previousMode); };
  uint8_t uid[7]={},ul=0,atqa[2]={},sak=0;
  if(!scanClassicOrShow(c,uid,ul,atqa,sak,bodyX(),bodyY(),bodyW(),bodyH())){
    restore(); render(); return;
  }
  const int max=sak==0x18?255:63; int block=InputNumberAction::popup((String("Block (1..")+String(max)+")").c_str(),1,max,1); if(InputNumberAction::wasCancelled()){restore();render();return;}
  String h=InputTextAction::popup("Block data (32 hex)","",InputTextAction::INPUT_HEX); if(InputTextAction::wasCancelled()){restore();render();return;} h.replace(" ","");h.replace(":","");
  if(h.length()!=32){restore();render();ShowStatusAction::show("Need 32 hex chars", 1600);return;} uint8_t d[16]={}; for(int i=0;i<16;++i){char x[3]={h[i*2],h[i*2+1],0};char*e=nullptr;unsigned long v=strtoul(x,&e,16);if(!e||*e){restore();render();ShowStatusAction::show("Bad hex", 1200);return;}d[i]=(uint8_t)v;}
  uint8_t key[6]={},type=0; bool ok=findKey(c,(uint8_t)block,key,type)&&c.mf1WriteBlock((uint8_t)block,type,key,d); restore(); render();ShowStatusAction::show(ok?"Block written":"Key not available",1600);
}

void ChameleonMfcAdvancedScreen::_lockUidGen3(){
  renderOperationChrome("Lock UID");
  auto& c = ChameleonClient::get();
  uint8_t previousMode=0; const bool restoreMode=c.getMode(&previousMode); c.setMode(1);
  auto restore=[&](){ if(restoreMode) c.setMode(previousMode); };

  uint8_t uid[7]={},uidLen=0,atqa[2]={},sak=0;
  if(!scanClassicOrShow(c,uid,uidLen,atqa,sak,bodyX(),bodyY(),bodyW(),bodyH())){
    restore(); render(); return;
  }
  if (c.detectMagicType() != MagicCardType::GEN3) {
    restore(); render(); ShowStatusAction::show("Tag is not Gen3", 1600); return;
  }

  const bool confirmed = ConfirmAction::popup("Lock UID permanently?", "This action cannot be undone");
  render();
  if (!confirmed) { restore(); return; }

  const uint8_t cmd[] = {0x90, 0xFD, 0x11, 0x11, 0x00};
  uint8_t resp[16] = {};
  uint16_t respLen = 0;
  uint16_t st = 0;
  const bool ok = c.hf14ARaw(128 | 64 | 32 | 16 | 8, 500,
                              sizeof(cmd) * 8u, cmd, sizeof(cmd),
                              resp, &respLen, sizeof(resp), &st) &&
                  (st == 0 || st == 0x68);
  restore(); ShowStatusAction::show(ok ? "Gen3 UID locked" : "Failed", 1600);
}
