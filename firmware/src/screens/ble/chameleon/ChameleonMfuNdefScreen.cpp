#include "ChameleonMfuNdefScreen.h"
#include "ChameleonMfuAuthUtils.h"
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

bool type2DefaultCc(uint16_t type, uint8_t cc[4]) {
  uint8_t size = 0;
  switch (type) {
    case ChameleonClient::MFU_NTAG210:
    case ChameleonClient::MFU_ULTRALIGHT_EV1_11:
    case ChameleonClient::MFU_ULTRALIGHT:
      size = 0x06; break;
    case ChameleonClient::MFU_NTAG212:
    case ChameleonClient::MFU_ULTRALIGHT_EV1_21:
      size = 0x10; break;
    case ChameleonClient::MFU_NTAG213:
    case ChameleonClient::MFU_ULTRALIGHT_C:
      size = 0x12; break;
    case ChameleonClient::MFU_NTAG215:
      size = 0x3F; break;
    case ChameleonClient::MFU_NTAG216:
      size = 0x6F; break;
    default:
      return false;
  }
  cc[0] = 0xE1; cc[1] = 0x10; cc[2] = size; cc[3] = 0x00;
  return true;
}

bool type2CcIsValid(const uint8_t cc[4]) {
  return cc && cc[0] == 0xE1 && (cc[1] & 0xF0) == 0x10 && cc[2] != 0;
}

bool type2CcCanProgramSafely(const uint8_t current[4], const uint8_t desired[4]) {
  for (uint8_t i = 0; i < 4; ++i)
    if ((current[i] & (uint8_t)~desired[i]) != 0) return false;
  return true;
}

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
  uint8_t previousMode = 0;
  const bool restoreMode = c.getMode(&previousMode);
  c.setMode(1);

  ChameleonClient::MfuTagInfo info = {};
  renderTagPrompt("Place tag on reader...", bodyX(), bodyY(), bodyW(), bodyH());

  if (!c.mfuDetect(&info)) {
    if (restoreMode) c.setMode(previousMode);
    ShowStatusAction::show("No Type 2 tag");
    return false;
  }

  len = (size_t)info.pages * 4u;
  img = (uint8_t*)malloc(len);
  if (!img) {
    if (restoreMode) c.setMode(previousMode);
    ShowStatusAction::show("Out of memory");
    return false;
  }

  memcpy(uid, info.uid, 7);

  uint8_t pwd[4] = {}; bool usePwd = false;
  if (!ChameleonMfuAuthUtils::prepare(c, info, true, pwd, usePwd)) {
    free(img); img = nullptr; if (restoreMode) c.setMode(previousMode); return false;
  }

  uint16_t got = 0;
  ProgressView::init();

  char m[36];
  snprintf(m, sizeof(m), "Reading pages (0/%u)...", (unsigned)info.pages);
  ProgressView::progress(m, 0);

  bool ok = c.mfuReadDump(info, img, (uint16_t)len, &got, [](uint16_t d, uint16_t t) {
    char x[36];
    snprintf(x, sizeof(x), "Reading pages (%u/%u)...", (unsigned)d, (unsigned)t);
    ProgressView::progress(x, t ? (int)((uint32_t)d * 100u / t) : 0);
  }, usePwd ? pwd : nullptr);

  ProgressView::finish();
  if (restoreMode) c.setMode(previousMode);

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
      case NdefParser::RECORD_TEXT:
        add("Record", "Text");
        if (r.language.length()) add("Language", r.language);
        add("Text", r.text);
        break;
      case NdefParser::RECORD_URL:
        add("Record", "URL"); add("URL", r.uri);
        break;
      case NdefParser::RECORD_PHONE:
        add("Record", "Phone"); add("Phone", r.phone);
        break;
      case NdefParser::RECORD_EMAIL:
        add("Record", "Email"); add("Email", r.email);
        break;
      case NdefParser::RECORD_VCARD:
        add("Record", "vCard");
        if (r.contact.length()) add("Contact", r.contact);
        if (r.company.length()) add("Company", r.company);
        if (r.address.length()) add("Address", r.address);
        if (r.phone.length()) add("Phone", r.phone);
        if (r.email.length()) add("Email", r.email);
        if (r.website.length()) add("Website", r.website);
        break;
      default:
        add("Record", "Unsupported");
        break;
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
  uint8_t previousMode = 0;
  const bool restoreMode = c.getMode(&previousMode);
  c.setMode(1);

  ChameleonClient::MfuTagInfo info = {};
  renderTagPrompt("Place tag on reader...", bodyX(), bodyY(), bodyW(), bodyH());
  if (!c.mfuDetect(&info)) {
    if (restoreMode) c.setMode(previousMode);
    ShowStatusAction::show("No Type 2 tag");
    return false;
  }

  uint8_t pwd[4] = {}; bool usePwd = false;
  if (!ChameleonMfuAuthUtils::prepare(c, info, false, pwd, usePwd)) {
    if (restoreMode) c.setMode(previousMode);
    return false;
  }

  uint8_t cc[4] = {};
  const bool ccOk = usePwd ? c.mfuReadPageSession(3, cc) : c.mfuReadPage(3, cc);
  if (!ccOk || cc[0] != 0xE1 || cc[2] == 0) {
    if (restoreMode) c.setMode(previousMode);
    ShowStatusAction::show("Not NDEF formatted");
    return false;
  }

  size_t capacity = (size_t)cc[2] * 8u;
  size_t physicalCapacity = info.pages > 4 ? ((size_t)info.pages - 4u) * 4u : 0u;
  uint8_t maxCc[4] = {};
  if (type2DefaultCc(info.type, maxCc)) {
    const size_t knownCapacity = (size_t)maxCc[2] * 8u;
    if (knownCapacity < physicalCapacity) physicalCapacity = knownCapacity;
  }
  if (!physicalCapacity) {
    if (restoreMode) c.setMode(previousMode);
    ShowStatusAction::show("Invalid NDEF capacity");
    return false;
  }
  if (capacity > physicalCapacity) capacity = physicalCapacity;
  const size_t lenBytes = (nl < 0xFFu) ? 1u : 3u;
  const size_t tlvLen = 1u + lenBytes + nl + 1u; // type + length + NDEF + terminator
  const size_t paddedLen = (tlvLen + 3u) & ~((size_t)3u);
  if (paddedLen > capacity) {
    if (restoreMode) c.setMode(previousMode);
    ShowStatusAction::show("NDEF does not fit");
    return false;
  }

  uint8_t* payload = (uint8_t*)malloc(paddedLen);
  if (!payload) {
    if (restoreMode) c.setMode(previousMode);
    ShowStatusAction::show("Out of memory");
    return false;
  }
  memset(payload, 0, paddedLen);
  size_t pos = 0;
  payload[pos++] = 0x03;
  if (nl < 0xFFu) {
    payload[pos++] = (uint8_t)nl;
  } else {
    payload[pos++] = 0xFF;
    payload[pos++] = (uint8_t)((nl >> 8) & 0xFFu);
    payload[pos++] = (uint8_t)(nl & 0xFFu);
  }
  if (nl) { memcpy(payload + pos, ndef, nl); pos += nl; }
  payload[pos] = 0xFE;

  const size_t totalPages = paddedLen / 4u;
  bool ok = true;
  ProgressView::init();
  for (size_t i = 0; i < totalPages; ++i) {
    char msg[36];
    snprintf(msg, sizeof(msg), "Writing pages (%u/%u)...",
             (unsigned)(i + 1u), (unsigned)totalPages);
    ProgressView::progress(msg, (int)(i * 100u / totalPages));
    const uint8_t page = (uint8_t)(4u + i);
    ok = usePwd ? c.mfuWritePageSession(page, payload + i * 4u)
                : c.mfuWritePage(page, payload + i * 4u);
    if (!ok) break;
  }
  ProgressView::finish();
  free(payload);
  if (restoreMode) c.setMode(previousMode);

  if (strcmp(opTitle, "Erase NDEF") == 0)
    ShowStatusAction::show(ok ? "NDEF erased" : "NDEF erase failed", 1600);
  else
    ShowStatusAction::show(ok ? "NDEF written" : "NDEF write failed", 1600);
  return ok;
}

void ChameleonMfuNdefScreen::erase() {
  writeRecord(nullptr, 0, "Erase NDEF");
  goMenu();
}

void ChameleonMfuNdefScreen::format() {
  operationTitle("Format NDEF");
  _running = true;

  auto& c = ChameleonClient::get();
  uint8_t previousMode = 0;
  const bool restoreMode = c.getMode(&previousMode);
  c.setMode(1);

  ChameleonClient::MfuTagInfo info = {};
  renderTagPrompt("Place tag on reader...", bodyX(), bodyY(), bodyW(), bodyH());
  if (!c.mfuDetect(&info)) {
    if (restoreMode) c.setMode(previousMode);
    ShowStatusAction::show("No Type 2 tag");
    _running = false;
    goMenu();
    return;
  }

  uint8_t pwd[4] = {}; bool usePwd = false;
  if (!ChameleonMfuAuthUtils::prepare(c, info, false, pwd, usePwd)) {
    if (restoreMode) c.setMode(previousMode);
    _running = false;
    goMenu();
    return;
  }

  uint8_t cc[4] = {}, desired[4] = {};
  const bool ccRead = usePwd ? c.mfuReadPageSession(3, cc) : c.mfuReadPage(3, cc);
  if (!ccRead || !type2DefaultCc(info.type, desired)) {
    if (restoreMode) c.setMode(previousMode);
    ShowStatusAction::show("Format unsupported");
    _running = false;
    goMenu();
    return;
  }

  bool ok = true;
  if (!type2CcIsValid(cc)) {
    if (!type2CcCanProgramSafely(cc, desired)) {
      if (restoreMode) c.setMode(previousMode);
      ShowStatusAction::show("CC cannot be safely formatted");
      _running = false;
      goMenu();
      return;
    }
    ok = usePwd ? c.mfuWritePageSession(3, desired) : c.mfuWritePage(3, desired);
  }

  const uint8_t emptyNdef[4] = {0x03, 0x00, 0xFE, 0x00};
  if (ok) ok = usePwd ? c.mfuWritePageSession(4, emptyNdef)
                      : c.mfuWritePage(4, emptyNdef);

  if (restoreMode) c.setMode(previousMode);
  ShowStatusAction::show(ok ? "NDEF formatted" : "Format failed", 1600);
  _running = false;
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
      case 3: format();  break;
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
      if (d == INavigation::DIR_BACK) goMenu();
      else                            _view.onNav(d);
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
