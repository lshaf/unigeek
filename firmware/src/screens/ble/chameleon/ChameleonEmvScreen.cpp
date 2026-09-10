#include "ChameleonEmvScreen.h"
#include "utils/ble/ChameleonClient.h"
#include "utils/nfc/EmvReader.h"
#include "core/Device.h"
#include "core/ScreenManager.h"
#include "ui/actions/ShowStatusAction.h"

namespace {
static constexpr uint8_t kIsoDepActivateOptions = 0xFC; // activate/wait/CRC/auto-select/keep/check CRC
static constexpr uint8_t kIsoDepSessionOptions  = 0xEC; // same, but keep current ISO-DEP session

static bool raw(ChameleonClient& c, uint8_t options, const uint8_t* tx, uint16_t txLen,
                uint8_t* rx, uint16_t& rxLen, uint16_t rxCap, uint16_t timeout = 800) {
  uint16_t st = 0;
  rxLen = 0;
  if (!c.hf14ARaw(options, timeout, txLen * 8, tx, txLen, rx, &rxLen, rxCap, &st)) return false;
  return st == 0 || st == 0x68;
}

static bool activateIsoDep(ChameleonClient& c) {
  // RATS: FSDI=8 (256-byte reader frame), CID=0.
  const uint8_t rats[2] = {0xE0, 0x80};
  uint8_t ats[64] = {}; uint16_t len = 0;
  return raw(c, kIsoDepActivateOptions, rats, sizeof(rats), ats, len, sizeof(ats)) && len >= 1;
}

static bool exchangeApdu(ChameleonClient& c, const uint8_t* apdu, uint16_t apduLen,
                         uint8_t* out, uint16_t& outLen, uint16_t outCap, uint8_t& blockNo) {
  if (apduLen + 1 > 260) return false;
  uint8_t frame[260] = {};
  frame[0] = (uint8_t)(0x02 | (blockNo & 1)); // I-block, no CID/NAD, no chaining
  memcpy(frame + 1, apdu, apduLen);

  uint8_t rsp[280] = {}; uint16_t rspLen = 0;
  if (!raw(c, kIsoDepSessionOptions, frame, apduLen + 1, rsp, rspLen, sizeof(rsp), 1200)) return false;

  outLen = 0;
  for (uint8_t guard = 0; guard < 8; ++guard) {
    if (rspLen < 1) return false;
    uint8_t pcb = rsp[0];

    // S(WTX): echo it and continue waiting for the application response.
    if ((pcb & 0xF7) == 0xF2) {
      if (!raw(c, kIsoDepSessionOptions, rsp, rspLen, rsp, rspLen, sizeof(rsp), 2500)) return false;
      continue;
    }

    if ((pcb & 0xC0) != 0x00 || (pcb & 0x02) == 0) return false; // require I-block
    uint16_t off = 1;
    if (pcb & 0x08) { if (rspLen <= off) return false; ++off; } // CID
    if (pcb & 0x04) { if (rspLen <= off) return false; ++off; } // NAD
    if (rspLen < off || outLen + (rspLen - off) > outCap) return false;
    memcpy(out + outLen, rsp + off, rspLen - off);
    outLen += rspLen - off;

    if ((pcb & 0x10) == 0) { blockNo ^= 1; return true; }

    // Card response is chained: acknowledge the received block number.
    const uint8_t rack = (uint8_t)(0xA2 | (pcb & 1));
    if (!raw(c, kIsoDepSessionOptions, &rack, 1, rsp, rspLen, sizeof(rsp), 1200)) return false;
  }
  return false;
}
}

void ChameleonEmvScreen::_push(const String& label, const String& value) {
  if (_rowCount >= kMaxRows) return;
  _labels[_rowCount] = label; _values[_rowCount] = value;
  _rows[_rowCount] = {_labels[_rowCount].c_str(), _values[_rowCount]};
  ++_rowCount;
}

void ChameleonEmvScreen::_pushWrapped(const String& label, const String& value) {
  if (value.length() == 0) {
    _push(label, "");
    return;
  }

  // Match the PN532 EMV details layout: preserve room for the label on the
  // first row and use continuation rows for long AIDs/application labels.
  int totalChars = (bodyW() - 10) / 6;
  if (totalChars < 12) totalChars = 12;
  int firstChars = totalChars - (int)label.length() - 2;
  if (firstChars < 8) firstChars = 8;

  int pos = 0;
  bool first = true;
  while (pos < (int)value.length() && _rowCount < kMaxRows) {
    while (pos < (int)value.length() &&
           (value[pos] == ' ' || value[pos] == '\n' || value[pos] == '\r' || value[pos] == '\t')) ++pos;
    if (pos >= (int)value.length()) break;

    const int maxChars = first ? firstChars : totalChars;
    int end = pos + maxChars;
    if (end > (int)value.length()) end = value.length();

    const int newline = value.indexOf('\n', pos);
    if (newline >= pos && newline < end) {
      end = newline;
    } else if (end < (int)value.length()) {
      int split = -1;
      for (int i = end; i > pos; --i) {
        if (value[i - 1] == ' ' || value[i - 1] == '\t') { split = i - 1; break; }
      }
      if (split > pos) end = split;
    }
    if (end <= pos) end = min(pos + maxChars, (int)value.length());

    String part = value.substring(pos, end);
    part.trim();
    _push(first ? label : "", part);
    pos = end;
    first = false;
  }
}

void ChameleonEmvScreen::_read() {
  _reading = true; _hasResult = false; _rowCount = 0;
  auto& lcd = Uni.Lcd;
  lcd.fillRect(bodyX(), bodyY(), bodyW(), bodyH(), TFT_BLACK);
  lcd.setTextDatum(MC_DATUM); lcd.setTextSize(1); lcd.setTextColor(TFT_YELLOW, TFT_BLACK);
  lcd.drawString("Place card on reader...", bodyX()+bodyW()/2, bodyY()+bodyH()/2);

  auto& c = ChameleonClient::get();
  uint8_t oldMode = 0; const bool restoreMode = c.getMode(&oldMode); c.setMode(1);
  uint8_t uid[7] = {}, uidLen = 0, atqa[2] = {}, sak = 0;
  bool ok = c.scan14A(uid, &uidLen, atqa, &sak);
  if (!ok || (sak & 0x20) == 0) {
    if (restoreMode) c.setMode(oldMode);
    _reading = false;
    ShowStatusAction::show(ok ? "Tag not supported" : "No tag detected");
    Screen.goBack(); return;
  }
  uint8_t apdu[20] = {}; uint8_t response[256] = {}; uint16_t responseLen = 0; uint8_t blockNo = 0;
  const uint16_t apduLen = EmvReader::buildSelectPpse(apdu);

  // Current CU firmware's HF14A_SCAN already sends RATS for ISO14443-4A and
  // leaves the card activated. Use that session first. For older firmware,
  // fall back to an explicit RATS + retry.
  ok = exchangeApdu(c, apdu, apduLen, response, responseLen, sizeof(response), blockNo);
  if (!ok && activateIsoDep(c)) {
    blockNo = 0; responseLen = 0;
    ok = exchangeApdu(c, apdu, apduLen, response, responseLen, sizeof(response), blockNo);
  }
  if (restoreMode) c.setMode(oldMode);
  _reading = false;
  if (!ok) { ShowStatusAction::show("EMV read failed"); Screen.goBack(); return; }

  EmvReader::Application apps[EmvReader::kMaxApps];
  const uint8_t count = EmvReader::parsePpse(response, responseLen, apps, EmvReader::kMaxApps);
  if (!count) { ShowStatusAction::show("EMV not found"); Screen.goBack(); return; }

  _push("UID", EmvReader::hex(uid, uidLen));
  _push("Applications", String(count));
  for (uint8_t i = 0; i < count; ++i) {
    String n = String(i + 1);
    _pushWrapped(String("AID ") + n, EmvReader::hex(apps[i].aid, apps[i].aidLen));
    if (apps[i].label.length()) _pushWrapped(String("Label ") + n, apps[i].label);
    if (apps[i].priority) _push(String("Priority ") + n, String(apps[i].priority & 0x0F));
  }
  _scrollView.setRows(_rows, _rowCount);
  _hasResult = true; render();
}

void ChameleonEmvScreen::onInit() { _read(); }
void ChameleonEmvScreen::onUpdate() {
  if (_reading || !Uni.Nav->wasPressed()) return;
  auto d = Uni.Nav->readDirection();
  if (d == INavigation::DIR_BACK) Screen.goBack();
  else if (_hasResult) _scrollView.onNav(d);
}
void ChameleonEmvScreen::onRender() {
  if (_hasResult) _scrollView.render(bodyX(), bodyY(), bodyW(), bodyH());
}
