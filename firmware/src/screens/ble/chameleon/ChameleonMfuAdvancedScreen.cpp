#include "ChameleonMfuAdvancedScreen.h"
#include "ChameleonMfuPagesScreen.h"
#include "ChameleonMfuAuthUtils.h"
#include "utils/ble/ChameleonClient.h"
#include "core/ScreenManager.h"
#include "ui/actions/InputNumberAction.h"
#include "ui/actions/InputTextAction.h"
#include "ui/actions/InputSelectAction.h"
#include "ui/actions/ShowStatusAction.h"
#include "ui/components/Header.h"

namespace {
const char* _mfuSensitivePageLabel(uint16_t type, uint16_t page) {
  const uint16_t dynamicLock = ChameleonMfuAuthUtils::dynamicLockPage(type);
  const uint16_t config0 = ChameleonMfuAuthUtils::config0(type);
  if (type == ChameleonClient::MFU_ULTRALIGHT_C) {
    if (page >= 40 && page <= 43) return "Security/config page";
    if (page >= 44 && page <= 47) return "3DES key page";
    return nullptr;
  }
  if (page == dynamicLock) return "Dynamic lock page";
  if (page == config0 || page == config0 + 1) return "Configuration page";
  if (page == config0 + 2) return "Password page";
  if (page == config0 + 3) return "PACK page";
  return nullptr;
}

bool _confirmSensitiveWrite(uint16_t type, uint16_t page) {
  const char* label = _mfuSensitivePageLabel(type, page);
  if (!label) return true;
  static const InputSelectAction::Option opts[] = {{"Write anyway", "write"}};
  String title = String("Warning: ") + label;
  const char* choice = InputSelectAction::popup(title.c_str(), opts, 1, nullptr);
  return choice && strcmp(choice, "write") == 0;
}

bool _readHex4(const char* title, uint8_t out[4]) {
  String hex = InputTextAction::popup(title, "", InputTextAction::INPUT_HEX);
  if (InputTextAction::wasCancelled()) return false;
  hex.replace(" ", ""); hex.replace(":", "");
  if (hex.length() != 8) { ShowStatusAction::show("Need 8 hex chars"); return false; }
  for (uint8_t i = 0; i < 4; ++i) {
    char b[3] = {hex[i * 2], hex[i * 2 + 1], 0};
    char* e = nullptr; unsigned long v = strtoul(b, &e, 16);
    if (!e || *e) { ShowStatusAction::show("Bad hex"); return false; }
    out[i] = (uint8_t)v;
  }
  return true;
}

bool _detect(ChameleonClient& c, ChameleonClient::MfuTagInfo& info) {
  auto& lcd = Uni.Lcd;
  const int bx = 0, by = 26, bw = lcd.width(), bh = lcd.height() - 26;
  lcd.fillRect(bx, by, bw, bh, TFT_BLACK);
  lcd.setTextDatum(MC_DATUM); lcd.setTextColor(TFT_YELLOW, TFT_BLACK);
  lcd.drawString("Place tag on reader...", bx + bw / 2, by + bh / 2);
  return c.mfuDetect(&info);
}

bool _buildLockMasks(uint16_t type, uint16_t first, uint16_t last,
                     uint8_t staticMask[2], uint8_t dynamicMask[3]) {
  staticMask[0] = staticMask[1] = 0;
  dynamicMask[0] = dynamicMask[1] = dynamicMask[2] = 0;
  if (first > last || first < 4) return false;

  for (uint16_t p = first; p <= last && p <= 15; ++p) {
    if (p <= 7) staticMask[0] |= (uint8_t)(1u << p);
    else staticMask[1] |= (uint8_t)(1u << (p - 8));
  }
  if (last <= 15) return true;

  auto setGroup = [&](uint16_t start, uint16_t end, uint8_t byte, uint8_t bit) {
    if (last >= start && first <= end) dynamicMask[byte] |= (uint8_t)(1u << bit);
  };

  switch (type) {
    case ChameleonClient::MFU_NTAG212:
    case ChameleonClient::MFU_ULTRALIGHT_EV1_21:
      for (uint8_t i = 0; i < 10; ++i) setGroup(16 + i * 2, 17 + i * 2, i / 8, i % 8);
      return true;
    case ChameleonClient::MFU_NTAG213:
      for (uint8_t i = 0; i < 12; ++i) setGroup(16 + i * 2, 17 + i * 2, i / 8, i % 8);
      return true;
    case ChameleonClient::MFU_NTAG215:
      for (uint8_t i = 0; i < 7; ++i) setGroup(16 + i * 16, 31 + i * 16, 0, i);
      setGroup(128, 129, 0, 7);
      return true;
    case ChameleonClient::MFU_NTAG216:
      for (uint8_t i = 0; i < 8; ++i) setGroup(16 + i * 16, 31 + i * 16, 0, i);
      for (uint8_t i = 0; i < 5; ++i) setGroup(144 + i * 16, 159 + i * 16, 1, i);
      setGroup(224, 225, 1, 5);
      return true;
    case ChameleonClient::MFU_NTAG210:
    case ChameleonClient::MFU_ULTRALIGHT_EV1_11:
    case ChameleonClient::MFU_ULTRALIGHT:
      return last <= 15;
    default:
      return false;
  }
}
}

void ChameleonMfuAdvancedScreen::onInit() {
  _items[0] = {"Read Pages"};
  _items[1] = {"Write Page"};
  _items[2] = {"Lock Pages"};
  _items[3] = {"Set Password"};
  _items[4] = {"Configure Protection"};
  _items[5] = {"Disable Protection"};
  setItems(_items);
}

void ChameleonMfuAdvancedScreen::_writePage() {
  Header header; header.render("Write Page");
  auto& c = ChameleonClient::get(); uint8_t previousMode = 0;
  const bool restoreMode = c.getMode(&previousMode); c.setMode(1);
  ChameleonClient::MfuTagInfo info = {};
  if (!_detect(c, info) || info.pages <= 4) { if (restoreMode) c.setMode(previousMode); render(); ShowStatusAction::show("Unsupported / no tag"); render(); return; }
  const int page = InputNumberAction::popup((String("Page (4..") + String(info.pages - 1) + ")").c_str(), 4, info.pages - 1, 4);
  if (InputNumberAction::wasCancelled() || !_confirmSensitiveWrite(info.type, (uint16_t)page)) { if (restoreMode) c.setMode(previousMode); render(); return; }
  uint8_t data[4] = {}; if (!_readHex4("Page data (8 hex)", data)) { if (restoreMode) c.setMode(previousMode); render(); return; }
  uint8_t pwd[4] = {}; bool usePwd = false;
  if (!ChameleonMfuAuthUtils::ensureForRange(c, info, page, page, false, pwd, usePwd)) { if (restoreMode) c.setMode(previousMode); render(); return; }
  const bool ok = usePwd ? c.mfuWritePageSession((uint8_t)page, data)
                         : c.mfuWritePage((uint8_t)page, data);
  if (restoreMode) c.setMode(previousMode); render(); ShowStatusAction::show(ok ? "Page written" : "Write failed"); render();
}

void ChameleonMfuAdvancedScreen::_lockPages() {
  Header header; header.render("Lock Pages");
  auto& c = ChameleonClient::get(); uint8_t previousMode = 0;
  const bool restoreMode = c.getMode(&previousMode); c.setMode(1);
  ChameleonClient::MfuTagInfo info = {};
  if (!_detect(c, info) || info.pages <= 4 || info.type == ChameleonClient::MFU_ULTRALIGHT_C) {
    if (restoreMode) c.setMode(previousMode); render(); ShowStatusAction::show("Unsupported tag type"); render(); return;
  }
  uint16_t lastUser = ChameleonMfuAuthUtils::dynamicLockPage(info.type);
  if (lastUser != 0xFFFF) --lastUser; else lastUser = 15;
  const int first = InputNumberAction::popup((String("First page (4..") + String(lastUser) + ")").c_str(), 4, lastUser, 4);
  if (InputNumberAction::wasCancelled()) { if (restoreMode) c.setMode(previousMode); render(); return; }
  const int last = InputNumberAction::popup((String("Last page (") + String(first) + ".." + String(lastUser) + ")").c_str(), first, lastUser, first);
  if (InputNumberAction::wasCancelled()) { if (restoreMode) c.setMode(previousMode); render(); return; }
  static const InputSelectAction::Option opts[] = {{"Lock permanently", "lock"}};
  if (!InputSelectAction::popup("Permanent page-group lock", opts, 1, nullptr)) { if (restoreMode) c.setMode(previousMode); render(); return; }

  uint8_t sm[2], dm[3];
  if (!_buildLockMasks(info.type, first, last, sm, dm)) { if (restoreMode) c.setMode(previousMode); render(); ShowStatusAction::show("Unsupported lock range"); render(); return; }
  uint8_t pwd[4] = {}; bool usePwd = false;
  const uint16_t dyn = ChameleonMfuAuthUtils::dynamicLockPage(info.type);
  const uint16_t authPage = dyn != 0xFFFF && (dm[0] || dm[1] || dm[2]) ? dyn : 2;
  if (!ChameleonMfuAuthUtils::ensureForRange(c, info, authPage, authPage, false, pwd, usePwd)) { if (restoreMode) c.setMode(previousMode); render(); return; }
  bool ok = true;
  if (sm[0] || sm[1]) {
    uint8_t p2[4] = {0, 0, sm[0], sm[1]};
    ok = usePwd ? c.mfuWritePageSession(2, p2) : c.mfuWritePage(2, p2);
  }
  if (ok && dyn != 0xFFFF && (dm[0] || dm[1] || dm[2])) {
    uint8_t cur[4] = {};
    const bool readOk = usePwd ? c.mfuReadPageSession((uint8_t)dyn, cur)
                               : c.mfuReadPage((uint8_t)dyn, cur);
    if (!readOk) ok = false;
    else {
      cur[0] |= dm[0]; cur[1] |= dm[1]; cur[2] |= dm[2];
      ok = usePwd ? c.mfuWritePageSession((uint8_t)dyn, cur)
                  : c.mfuWritePage((uint8_t)dyn, cur);
    }
  }
  if (restoreMode) c.setMode(previousMode); render(); ShowStatusAction::show(ok ? "Pages locked" : "Lock failed"); render();
}

void ChameleonMfuAdvancedScreen::_setPassword() {
  Header header; header.render("Set Password"); auto& c = ChameleonClient::get(); uint8_t previousMode=0;
  const bool restoreMode=c.getMode(&previousMode); c.setMode(1); ChameleonClient::MfuTagInfo info={};
  if (!_detect(c, info) || !ChameleonMfuAuthUtils::supportsPwd(info.type)) { if (restoreMode) c.setMode(previousMode); render(); ShowStatusAction::show("Password not supported"); render(); return; }
  const uint16_t cfg=ChameleonMfuAuthUtils::config0(info.type); uint8_t current[4]={}; bool usePwd=false;
  if (!ChameleonMfuAuthUtils::ensureForRange(c, info, cfg+2, cfg+2, false, current, usePwd)) { if (restoreMode)c.setMode(previousMode); render(); return; }
  uint8_t newPwd[4]={};
  if (!ChameleonMfuAuthUtils::promptPassword(newPwd, "New Password")) { if (restoreMode)c.setMode(previousMode); render(); return; }
  // Keep PACK unchanged. Text passwords use the NFC Tools-compatible
  // MD5-derived 4-byte PWD; PACK remains an implementation detail.
  bool ok = usePwd ? c.mfuWritePageSession((uint8_t)(cfg+2), newPwd)
                   : c.mfuWritePage((uint8_t)(cfg+2), newPwd);
  if (restoreMode)c.setMode(previousMode); render(); ShowStatusAction::show(ok?"Password updated":"Password write failed"); render();
}

void ChameleonMfuAdvancedScreen::_configureProtection() {
  Header header; header.render("Configure Protection"); auto& c=ChameleonClient::get(); uint8_t previousMode=0;
  const bool restoreMode=c.getMode(&previousMode); c.setMode(1); ChameleonClient::MfuTagInfo info={};
  if (!_detect(c, info) || !ChameleonMfuAuthUtils::supportsPwd(info.type)) { if (restoreMode)c.setMode(previousMode); render(); ShowStatusAction::show("Protection not supported"); render(); return; }
  const uint16_t cfg=ChameleonMfuAuthUtils::config0(info.type); uint8_t pwd[4]={}; bool usePwd=false;
  if (!ChameleonMfuAuthUtils::ensureForRange(c, info, cfg, cfg+1, false, pwd, usePwd)) { if (restoreMode)c.setMode(previousMode); render(); return; }
  uint8_t c0[4]={}, c1[4]={};
  const bool r0 = usePwd ? c.mfuReadPageSession((uint8_t)cfg, c0) : c.mfuReadPage((uint8_t)cfg, c0);
  const bool r1 = usePwd ? c.mfuReadPageSession((uint8_t)(cfg+1), c1) : c.mfuReadPage((uint8_t)(cfg+1), c1);
  if (!r0 || !r1) { if(restoreMode)c.setMode(previousMode); render(); ShowStatusAction::show("Read config failed"); render(); return; }
  if (c1[0]&0x40) { if(restoreMode)c.setMode(previousMode); render(); ShowStatusAction::show("Configuration locked"); render(); return; }
  const int first=InputNumberAction::popup((String("Protect from (4..")+String(info.pages-1)+")").c_str(),4,info.pages-1,4); if(InputNumberAction::wasCancelled()){if(restoreMode)c.setMode(previousMode);render();return;}
  static const InputSelectAction::Option modes[]={{"Write only","w"},{"Read + Write","rw"}}; const char* mode=InputSelectAction::popup("Protection",modes,2,nullptr); if(!mode){if(restoreMode)c.setMode(previousMode);render();return;}
  const int lim=InputNumberAction::popup("Auth limit (0..7)",0,7,0); if(InputNumberAction::wasCancelled()){if(restoreMode)c.setMode(previousMode);render();return;}
  if(lim>0){static const InputSelectAction::Option warn[]={{"Use auth limit","yes"}}; if(!InputSelectAction::popup("Warning: may lock access",warn,1,nullptr)){if(restoreMode)c.setMode(previousMode);render();return;}}
  c1[0]=(uint8_t)((c1[0]&~0x87u)|(strcmp(mode,"rw")==0?0x80:0)|(lim&0x07)); c0[3]=(uint8_t)first;
  bool ok = usePwd ? c.mfuWritePageSession((uint8_t)(cfg+1), c1)
                   : c.mfuWritePage((uint8_t)(cfg+1), c1);
  if(ok) ok = usePwd ? c.mfuWritePageSession((uint8_t)cfg, c0)
                     : c.mfuWritePage((uint8_t)cfg, c0);
  if(restoreMode)c.setMode(previousMode); render(); ShowStatusAction::show(ok?"Protection configured":"Protection failed"); render();
}

void ChameleonMfuAdvancedScreen::_disableProtection() {
  Header header; header.render("Disable Protection"); auto& c=ChameleonClient::get(); uint8_t previousMode=0;
  const bool restoreMode=c.getMode(&previousMode); c.setMode(1); ChameleonClient::MfuTagInfo info={};
  if(!_detect(c,info)||!ChameleonMfuAuthUtils::supportsPwd(info.type)){if(restoreMode)c.setMode(previousMode);render();ShowStatusAction::show("Protection not supported");render();return;}
  const uint16_t cfg=ChameleonMfuAuthUtils::config0(info.type); uint8_t pwd[4]={}; bool usePwd=false;
  if(!ChameleonMfuAuthUtils::ensureForRange(c,info,cfg,cfg,false,pwd,usePwd)){if(restoreMode)c.setMode(previousMode);render();return;}
  uint8_t c0[4]={},c1[4]={};
  const bool r0 = usePwd ? c.mfuReadPageSession((uint8_t)cfg, c0) : c.mfuReadPage((uint8_t)cfg, c0);
  const bool r1 = usePwd ? c.mfuReadPageSession((uint8_t)(cfg+1), c1) : c.mfuReadPage((uint8_t)(cfg+1), c1);
  if(!r0||!r1){if(restoreMode)c.setMode(previousMode);render();ShowStatusAction::show("Read config failed");render();return;}
  if(c1[0]&0x40){if(restoreMode)c.setMode(previousMode);render();ShowStatusAction::show("Configuration locked");render();return;}
  c0[3]=0xFF;
  const bool ok = usePwd ? c.mfuWritePageSession((uint8_t)cfg, c0)
                         : c.mfuWritePage((uint8_t)cfg, c0);
  if(restoreMode)c.setMode(previousMode); render(); ShowStatusAction::show(ok?"Protection disabled":"Disable failed"); render();
}

void ChameleonMfuAdvancedScreen::onItemSelected(uint8_t index) {
  if(index==0) Screen.push(new ChameleonMfuPagesScreen());
  else if(index==1) _writePage();
  else if(index==2) _lockPages();
  else if(index==3) _setPassword();
  else if(index==4) _configureProtection();
  else if(index==5) _disableProtection();
}
void ChameleonMfuAdvancedScreen::onBack(){Screen.goBack();}
