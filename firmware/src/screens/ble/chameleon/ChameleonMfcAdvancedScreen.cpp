#include "ChameleonMfcAdvancedScreen.h"
#include "utils/ble/ChameleonClient.h"
#include "core/ScreenManager.h"
#include "ui/actions/InputNumberAction.h"
#include "ui/actions/InputSelectAction.h"
#include "ui/actions/InputTextAction.h"
#include "ui/actions/ShowStatusAction.h"
#include "ui/components/Header.h"
#include "ui/views/ProgressView.h"

namespace {
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
static String hex16(const uint8_t* d){ String s; char b[4]; for(int i=0;i<16;++i){snprintf(b,sizeof(b),"%s%02X",i?" ":"",d[i]);s+=b;} return s; }
}

void ChameleonMfcAdvancedScreen::onInit(){
  _items[0] = {"Read Memory"};
  _items[1] = {"Edit Memory"};
  _items[2] = {"Edit UID (Gen3)"};
  _items[3] = {"Lock UID (Gen3)"};
  setItems(_items);
}
void ChameleonMfcAdvancedScreen::_addRow(const String& l,const String& v){ if(_rowCount>=260)return; _labels[_rowCount]=l;_values[_rowCount]=v;_rows[_rowCount]={_labels[_rowCount].c_str(),_values[_rowCount].c_str()};++_rowCount; }
void ChameleonMfcAdvancedScreen::onItemSelected(uint8_t i){
  if (i == 0) _readMemory();
  else if (i == 1) _editMemory();
  else if (i == 2) _editUidGen3();
  else if (i == 3) _lockUidGen3();
}
void ChameleonMfcAdvancedScreen::onBack(){ if(_showingMemory){_showingMemory=false;setItems(_items);render();} else Screen.goBack(); }
void ChameleonMfcAdvancedScreen::onUpdate(){ if(!_showingMemory){ListScreen::onUpdate();return;} if(Uni.Nav->wasPressed()){auto d=Uni.Nav->readDirection();if(d==INavigation::DIR_BACK)onBack();else _view.onNav(d);} }
void ChameleonMfcAdvancedScreen::onRender(){ if(_showingMemory){_view.render(bodyX(),bodyY(),bodyW(),bodyH());return;} ListScreen::onRender(); }

void ChameleonMfcAdvancedScreen::_readMemory(){
  Header header; header.render("Read Memory");
  auto& c=ChameleonClient::get();
  uint8_t previousMode=0; const bool restoreMode=c.getMode(&previousMode); c.setMode(1);
  auto restore=[&](){ if(restoreMode) c.setMode(previousMode); };
  uint8_t uid[7]={},ul=0,atqa[2]={},sak=0;
  if(!c.scan14A(uid,&ul,atqa,&sak)||(sak!=0x08&&sak!=0x18)){restore();render();ShowStatusAction::show("Tag not MIFARE Classic");render();return;}
  const uint16_t blocks=sak==0x18?256:64; uint8_t keys[40][6]={},types[40]={}; bool have[40]={};
  _rowCount=0; _addRow("Type",sak==0x18?"MF Classic 4K":"MF Classic 1K");
  String uidText; for(uint8_t i=0;i<ul;++i){char b[4];snprintf(b,sizeof(b),"%s%02X",i?":":"",uid[i]);uidText+=b;} _addRow("UID",uidText);
  _addRow("Blocks", String(blocks));
  ProgressView::init();
  for(uint16_t b=0;b<blocks;++b){
    uint8_t sec=sectorForBlock(b); if(!have[sec])have[sec]=findKey(c,(uint8_t)b,keys[sec],types[sec]);
    uint8_t data[16]={}; bool ok=have[sec]&&c.mf1ReadBlock((uint8_t)b,types[sec],keys[sec],data);
    char msg[36];snprintf(msg,sizeof(msg),"Reading blocks (%u/%u)...",(unsigned)(b+1),(unsigned)blocks);ProgressView::progress(msg,(int)((uint32_t)b*100u/blocks));
    _addRow("B"+String(b),ok?hex16(data):String("Unreadable (key)"));
  }
  ProgressView::finish(); restore(); _view.setRows(_rows,_rowCount); _showingMemory=true; render();
}

void ChameleonMfcAdvancedScreen::_editMemory(){
  Header header; header.render("Edit Memory");
  auto& c=ChameleonClient::get();
  uint8_t previousMode=0; const bool restoreMode=c.getMode(&previousMode); c.setMode(1);
  auto restore=[&](){ if(restoreMode) c.setMode(previousMode); };
  uint8_t uid[7]={},ul=0,atqa[2]={},sak=0;
  if(!c.scan14A(uid,&ul,atqa,&sak)||(sak!=0x08&&sak!=0x18)){restore();render();ShowStatusAction::show("Tag not MIFARE Classic");render();return;}
  const int max=sak==0x18?255:63; int block=InputNumberAction::popup((String("Block (1..")+String(max)+")").c_str(),1,max,1); if(InputNumberAction::wasCancelled()){restore();render();return;}
  String h=InputTextAction::popup("Block data (32 hex)","",InputTextAction::INPUT_HEX); if(InputTextAction::wasCancelled()){restore();render();return;} h.replace(" ","");h.replace(":","");
  if(h.length()!=32){restore();render();ShowStatusAction::show("Need 32 hex chars");render();return;} uint8_t d[16]={}; for(int i=0;i<16;++i){char x[3]={h[i*2],h[i*2+1],0};char*e=nullptr;unsigned long v=strtoul(x,&e,16);if(!e||*e){restore();render();ShowStatusAction::show("Bad hex");render();return;}d[i]=(uint8_t)v;}
  uint8_t key[6]={},type=0; bool ok=findKey(c,(uint8_t)block,key,type)&&c.mf1WriteBlock((uint8_t)block,type,key,d); restore(); render();ShowStatusAction::show(ok?"Block written":"Write failed: missing key",1600);render();
}


void ChameleonMfcAdvancedScreen::_editUidGen3(){
  Header header; header.render("Edit UID");
  auto& c = ChameleonClient::get();
  uint8_t previousMode=0; const bool restoreMode=c.getMode(&previousMode); c.setMode(1);
  auto restore=[&](){ if(restoreMode) c.setMode(previousMode); };

  if (c.detectMagicType() != MagicCardType::GEN3) {
    restore(); render(); ShowStatusAction::show("Tag is not Gen3", 1500); render(); return;
  }

  String hex = InputTextAction::popup("New UID (8 or 14 hex)", "", InputTextAction::INPUT_HEX);
  if (InputTextAction::wasCancelled()) { restore(); render(); return; }
  hex.replace(" ", ""); hex.replace(":", "");
  if (hex.length() != 8 && hex.length() != 14) {
    restore(); render(); ShowStatusAction::show("UID must be 4 or 7 bytes", 1500); render(); return;
  }

  uint8_t uid[7] = {};
  const uint8_t uidLen = (uint8_t)(hex.length() / 2);
  for (uint8_t i = 0; i < uidLen; ++i) {
    char b[3] = {hex[i * 2], hex[i * 2 + 1], 0};
    char* end = nullptr;
    const unsigned long v = strtoul(b, &end, 16);
    if (!end || *end) {
      restore(); render(); ShowStatusAction::show("Bad hex", 1200); render(); return;
    }
    uid[i] = (uint8_t)v;
  }

  uint8_t block0[16] = {};
  const bool ok = c.writeMagicUid(MagicCardType::GEN3, uid, uidLen, block0);
  restore(); render(); ShowStatusAction::show(ok ? "Gen3 UID edited" : "Edit UID failed", 1600); render();
}

void ChameleonMfcAdvancedScreen::_lockUidGen3(){
  Header header; header.render("Lock UID");
  auto& c = ChameleonClient::get();
  uint8_t previousMode=0; const bool restoreMode=c.getMode(&previousMode); c.setMode(1);
  auto restore=[&](){ if(restoreMode) c.setMode(previousMode); };

  if (c.detectMagicType() != MagicCardType::GEN3) {
    restore(); render(); ShowStatusAction::show("Tag is not Gen3", 1500); render(); return;
  }

  static const InputSelectAction::Option opts[] = {{"Lock UID permanently", "lock"}};
  const char* choice = InputSelectAction::popup("Permanent UID lock", opts, 1, nullptr);
  render();
  if (!choice || strcmp(choice, "lock") != 0) { restore(); return; }

  const uint8_t cmd[] = {0x90, 0xFD, 0x11, 0x11, 0x00};
  uint8_t resp[16] = {};
  uint16_t respLen = 0;
  uint16_t st = 0;
  const bool ok = c.hf14ARaw(128 | 64 | 32 | 16 | 8, 500,
                              sizeof(cmd) * 8u, cmd, sizeof(cmd),
                              resp, &respLen, sizeof(resp), &st) &&
                  (st == 0 || st == 0x68);
  restore(); ShowStatusAction::show(ok ? "Gen3 UID locked" : "Lock failed", 1600); render();
}
