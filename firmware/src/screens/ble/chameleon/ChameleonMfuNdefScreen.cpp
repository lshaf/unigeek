#include "ChameleonMfuNdefScreen.h"
#include "utils/ble/ChameleonClient.h"
#include "utils/nfc/NdefBuilder.h"
#include "utils/nfc/NdefParser.h"
#include "utils/nfc/NfcDumpBuilder.h"
#include "core/Device.h"
#include "core/ScreenManager.h"
#include "ui/actions/InputTextAction.h"
#include "ui/actions/ShowStatusAction.h"
#include "ui/components/Header.h"
#include "ui/views/ProgressView.h"

static void renderTagPrompt(const char* message, int bx, int by, int bw, int bh) {
  auto& lcd = Uni.Lcd;
  lcd.fillRect(bx, by, bw, bh, TFT_BLACK);
  lcd.setTextDatum(MC_DATUM);
  lcd.setTextSize(1);
  lcd.setTextColor(TFT_YELLOW, TFT_BLACK);
  lcd.drawString(message, bx + bw / 2, by + bh / 2);
}

namespace {

void operationTitle(const char* t) {
  Header h;
  h.render(t);
}

// Write-progress callback handed to ChameleonClient::mfuWriteNtag215User().
void prog(uint16_t done, uint16_t total) {
  char m[36];
  snprintf(m, sizeof(m), "Writing pages (%u/%u)...", (unsigned)done, (unsigned)total);
  ProgressView::progress(m, total ? (int)((uint32_t)done * 100u / total) : 0);
}

constexpr const char* kNdefDir = "/unigeek/nfc/ndefs";

}  // namespace

const char* ChameleonMfuNdefScreen::title() {
  if (_state == WRITE_MENU)  return "Write NDEF";
  if (_state == RESULT)      return "NDEF Details";
  if (_state == FILE_SELECT) return "NDEF Files";
  return "NDEF Operations";
}

void ChameleonMfuNdefScreen::onInit() {
  goMenu();
}

// ── states ─────────────────────────────────────────────

void ChameleonMfuNdefScreen::goMenu() {
  _state = MENU;
  setItems(_menu);
  render();
}

void ChameleonMfuNdefScreen::goWrite() {
  _state = WRITE_MENU;
  setItems(_write);
  render();
}

void ChameleonMfuNdefScreen::add(const String& label, const String& value) {
  if (_n >= kMaxRows) return;
  _l[_n]    = label;
  _v[_n]    = value;
  _rows[_n] = {_l[_n].c_str(), _v[_n].c_str()};
  ++_n;
}

// ── tag I/O ────────────────────────────────────────────

// Reads the whole Type 2 image into a heap buffer. On success the caller owns
// `img` and must free() it; every failure path frees it here.
bool ChameleonMfuNdefScreen::readImage(uint8_t*& img, size_t& len, uint8_t uid[7]) {
  auto& c = ChameleonClient::get();
  c.setMode(1);

  ChameleonClient::MfuTagInfo info = {};
  renderTagPrompt("Place tag on reader...", bodyX(), bodyY(), bodyW(), bodyH());

  if (!c.mfuDetect(&info)) {
    c.setMode(0);
    ShowStatusAction::show("No Type 2 tag");
    return false;
  }

  len = (size_t)info.pages * 4u;
  img = (uint8_t*)malloc(len);
  if (!img) {
    c.setMode(0);
    ShowStatusAction::show("Out of memory");
    return false;
  }

  memcpy(uid, info.uid, 7);

  uint16_t got = 0;
  ProgressView::init();

  char m[36];
  snprintf(m, sizeof(m), "Reading pages (0/%u)...", (unsigned)info.pages);
  ProgressView::progress(m, 0);

  bool ok = c.mfuReadDump(info, img, (uint16_t)len, &got, [](uint16_t d, uint16_t t) {
    char x[36];
    snprintf(x, sizeof(x), "Reading pages (%u/%u)...", (unsigned)d, (unsigned)t);
    ProgressView::progress(x, t ? (int)((uint32_t)d * 100u / t) : 0);
  });

  ProgressView::finish();
  c.setMode(0);

  if (!ok) {
    free(img);
    img = nullptr;
    ShowStatusAction::show("Read failed");
    return false;
  }

  len = got;
  return true;
}

void ChameleonMfuNdefScreen::show(const uint8_t* ndef, size_t len, const uint8_t uid[7]) {
  _state = RESULT;
  _n     = 0;

  String u;
  for (int i = 0; i < 7; i++) {
    char b[4];
    snprintf(b, sizeof(b), "%02X%s", uid[i], i < 6 ? ":" : "");
    u += b;
  }
  add("UID", u);

  NdefParser::Result r;
  if (!ndef || !len || !NdefParser::parse(ndef, len, r)) {
    add("NDEF", "Not found");
  } else {
    switch (r.kind) {
      case NdefParser::RECORD_TEXT:  add("NDEF", "Text");  add("Text", r.text);       break;
      case NdefParser::RECORD_URL:   add("NDEF", "URI");   add("URI", r.uri);         break;
      case NdefParser::RECORD_PHONE: add("NDEF", "Phone"); add("Phone", r.phone);     break;
      case NdefParser::RECORD_EMAIL: add("NDEF", "Email"); add("Mail", r.email);      break;
      case NdefParser::RECORD_VCARD: add("NDEF", "vCard"); add("Contact", r.contact); break;
      default:                       add("NDEF", "Unsupported");                      break;
    }
  }

  _view.resetScroll();
  _view.setRows(_rows, _n);
  render();
}

void ChameleonMfuNdefScreen::read() {
  operationTitle("Read NDEF");
  _running = true;

  uint8_t* img    = nullptr;
  uint8_t  uid[7] = {};
  size_t   len    = 0;

  if (readImage(img, len, uid)) {
    const uint8_t* n  = nullptr;
    size_t         nl = 0;
    if (NdefParser::extractType2Ndef(img, len, &n, &nl)) show(n, nl, uid);
    else                                                 show(nullptr, 0, uid);
    free(img);
  }

  _running = false;
}

bool ChameleonMfuNdefScreen::writeRecord(const uint8_t* ndef, size_t nl, const char* opTitle) {
  operationTitle(opTitle);

  auto& c = ChameleonClient::get();
  c.setMode(1);

  ChameleonClient::MfuTagInfo info = {};
  renderTagPrompt("Place tag on reader...", bodyX(), bodyY(), bodyW(), bodyH());

  if (!c.mfuDetect(&info) || info.type != ChameleonClient::MFU_NTAG215) {
    c.setMode(0);
    ShowStatusAction::show("Write supports NTAG215");
    return false;
  }

  uint8_t img[NfcDumpBuilder::NTAG215_SIZE];
  size_t  len = 0;
  if (!NfcDumpBuilder::buildNtag215(info.uid, ndef, nl, img, len, sizeof(img))) {
    c.setMode(0);
    ShowStatusAction::show("NDEF does not fit");
    return false;
  }

  ProgressView::init();
  bool ok = c.mfuWriteNtag215User(img, (uint16_t)len, prog, &info);
  ProgressView::finish();

  c.setMode(0);
  ShowStatusAction::show(ok ? "NDEF written" : "NDEF write failed");
  return ok;
}

bool ChameleonMfuNdefScreen::format(const char* opTitle) {
  return writeRecord(nullptr, 0, opTitle);
}

void ChameleonMfuNdefScreen::erase() {
  bool ok = format("Erase NDEF");
  if (ok) ShowStatusAction::show("NDEF erased");
  goMenu();
}

void ChameleonMfuNdefScreen::writeBuilt(uint8_t kind) {
  const char* prompt = kind == 0 ? "Text"
                     : kind == 1 ? "URL"
                     : kind == 2 ? "Phone"
                                 : "Email";

  String a = InputTextAction::popup(prompt, "");
  if (InputTextAction::wasCancelled() || !a.length()) {
    goWrite();
    return;
  }

  uint8_t n[500];
  size_t  l  = 0;
  bool    ok = kind == 0 ? NdefBuilder::buildText(a, n, l, sizeof(n))
             : kind == 1 ? NdefBuilder::buildUrl(a, n, l, sizeof(n))
             : kind == 2 ? NdefBuilder::buildPhone(a, n, l, sizeof(n))
                         : NdefBuilder::buildEmail(a, n, l, sizeof(n));

  if (ok) writeRecord(n, l, "Write NDEF");
  else    ShowStatusAction::show("Cannot build NDEF");

  goWrite();
}

// ── .ndef files ────────────────────────────────────────

void ChameleonMfuNdefScreen::files() {
  _state = FILE_SELECT;
  if (!_pickDir.length()) _pickDir = kNdefDir;

  _browser.root = kNdefDir;
  uint8_t n = _browser.load(this, _pickDir, ".ndef");
  setItems(_browser.items(), n);
  render();

  if (!n) ShowStatusAction::show("No .ndef files");
}

void ChameleonMfuNdefScreen::fileSelected(uint8_t index) {
  if (index >= _browser.count()) return;

  auto& e = _browser.entry(index);
  if (e.isDir) {
    _pickDir = e.path;
    files();
    return;
  }

  String raw = Uni.Storage->readFile(e.path.c_str());
  if (!raw.length()) {
    ShowStatusAction::show("Empty file");
    return;
  }

  writeRecord((const uint8_t*)raw.c_str(), raw.length(), "Write NDEF");
  files();
}

// ── screen hooks ───────────────────────────────────────

void ChameleonMfuNdefScreen::onItemSelected(uint8_t index) {
  if (_state == MENU) {
    switch (index) {
      case 0: read();    break;
      case 1: goWrite(); break;
      case 2: erase();   break;
      case 3: format("Format NDEF"); goMenu(); break;
    }
    return;
  }

  if (_state == WRITE_MENU) {
    if (index < 4) {
      writeBuilt(index);
      return;
    }

    if (index == 4) {
      // vCard needs three fields; cancelling any one aborts the whole record.
      String name = InputTextAction::popup("Name", "");
      if (InputTextAction::wasCancelled()) { goWrite(); return; }

      String phone = InputTextAction::popup("Phone", "");
      if (InputTextAction::wasCancelled()) { goWrite(); return; }

      String email = InputTextAction::popup("Email", "");
      if (InputTextAction::wasCancelled()) { goWrite(); return; }

      uint8_t n[500];
      size_t  l = 0;
      if (NdefBuilder::buildVcard(name, "", "", phone, email, "", n, l, sizeof(n)))
        writeRecord(n, l, "Write NDEF");
      else
        ShowStatusAction::show("Cannot build vCard");

      goWrite();
      return;
    }

    files();
    return;
  }

  if (_state == FILE_SELECT) fileSelected(index);
}

void ChameleonMfuNdefScreen::onUpdate() {
  if (_state == RESULT) {
    if (Uni.Nav->wasPressed()) {
      auto d = Uni.Nav->readDirection();
      if (d == INavigation::DIR_BACK)       goMenu();
      else if (d == INavigation::DIR_PRESS) read();
      else                                  _view.onNav(d);
    }
    return;
  }
  ListScreen::onUpdate();
}

void ChameleonMfuNdefScreen::onRender() {
  if (_state == RESULT) {
    _view.render(bodyX(), bodyY(), bodyW(), bodyH());
    return;
  }
  ListScreen::onRender();
}

void ChameleonMfuNdefScreen::onBack() {
  if (_state == WRITE_MENU || _state == RESULT) {
    goMenu();
    return;
  }

  if (_state == FILE_SELECT) {
    _pickDir = "";
    goWrite();
    return;
  }

  Screen.goBack();
}
