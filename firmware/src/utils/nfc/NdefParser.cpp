#include "utils/nfc/NdefParser.h"

String NdefParser::_uriPrefix(uint8_t code) {
  switch (code) {
    case 0x00: return "";
    case 0x01: return "http://www.";
    case 0x02: return "https://www.";
    case 0x03: return "http://";
    case 0x04: return "https://";
    case 0x05: return "tel:";
    case 0x06: return "mailto:";
    case 0x07: return "ftp://anonymous:anonymous@";
    case 0x08: return "ftp://ftp.";
    case 0x09: return "ftps://";
    case 0x0A: return "sftp://";
    case 0x0B: return "smb://";
    case 0x0C: return "nfs://";
    case 0x0D: return "ftp://";
    case 0x0E: return "dav://";
    case 0x0F: return "news:";
    case 0x10: return "telnet://";
    case 0x11: return "imap:";
    case 0x12: return "rtsp://";
    case 0x13: return "urn:";
    case 0x14: return "pop:";
    case 0x15: return "sip:";
    case 0x16: return "sips:";
    case 0x17: return "tftp:";
    case 0x18: return "btspp://";
    case 0x19: return "btl2cap://";
    case 0x1A: return "btgoep://";
    case 0x1B: return "tcpobex://";
    case 0x1C: return "irdaobex://";
    case 0x1D: return "file://";
    case 0x1E: return "urn:epc:id:";
    case 0x1F: return "urn:epc:tag:";
    case 0x20: return "urn:epc:pat:";
    case 0x21: return "urn:epc:raw:";
    case 0x22: return "urn:epc:";
    case 0x23: return "urn:nfc:";
    default: return "";
  }
}

String NdefParser::_bytesToString(const uint8_t* data, size_t len) {
  String s;
  s.reserve(len);
  for (size_t i = 0; i < len; ++i) s += (char)data[i];
  return s;
}

String NdefParser::_vcardValue(const String& vcard, const char* key) {
  int start = 0;
  while (start < (int)vcard.length()) {
    int end = vcard.indexOf('\n', start);
    if (end < 0) end = vcard.length();

    String line = vcard.substring(start, end);
    line.trim();

    int colon = line.indexOf(':');
    if (colon > 0) {
      String lhs = line.substring(0, colon);
      int semi = lhs.indexOf(';');
      if (semi >= 0) lhs = lhs.substring(0, semi);

      if (lhs.equalsIgnoreCase(key)) {
        String value = line.substring(colon + 1);
        value.replace("\\n", "\n");
        value.replace("\\,", ",");
        value.replace("\\;", ";");
        value.replace("\\\\", "\\");
        value.trim();
        return value;
      }
    }
    start = end + 1;
  }
  return "";
}

String NdefParser::_cleanStructured(String value) {
  while (value.indexOf(";;") >= 0) value.replace(";;", ";");
  while (value.startsWith(";")) value.remove(0, 1);
  while (value.endsWith(";")) value.remove(value.length() - 1);
  value.replace(";", ", ");
  return value;
}

bool NdefParser::extractType2Ndef(const uint8_t* dump, size_t dumpLen,
                                  const uint8_t** ndef, size_t* ndefLen) {
  if (ndef) *ndef = nullptr;
  if (ndefLen) *ndefLen = 0;
  if (!dump || dumpLen < 20 || !ndef || !ndefLen) return false;

  // NFC Forum Type 2 CC is page 3. Require NDEF magic.
  if (dump[12] != 0xE1) return false;

  size_t p = 16; // page 4
  while (p < dumpLen) {
    uint8_t type = dump[p++];

    if (type == 0x00) continue; // NULL TLV
    if (type == 0xFE) return false; // Terminator before NDEF

    if (p >= dumpLen) return false;
    size_t len = dump[p++];
    if (len == 0xFF) {
      if (p + 1 >= dumpLen) return false;
      len = ((size_t)dump[p] << 8) | dump[p + 1];
      p += 2;
    }

    if (p + len > dumpLen) return false;

    if (type == 0x03) {
      *ndef = dump + p;
      *ndefLen = len;
      return len > 0;
    }

    p += len;
  }

  return false;
}

bool NdefParser::parse(const uint8_t* ndef, size_t ndefLen, Result& out) {
  out = Result{};
  if (!ndef || ndefLen < 3) return false;

  size_t p = 0;
  const uint8_t flagsTnf = ndef[p++];
  const bool sr = (flagsTnf & 0x10) != 0;
  const bool il = (flagsTnf & 0x08) != 0;
  out.tnf = flagsTnf & 0x07;

  if (p >= ndefLen) return false;
  const uint8_t typeLen = ndef[p++];

  uint32_t payloadLen = 0;
  if (sr) {
    if (p >= ndefLen) return false;
    payloadLen = ndef[p++];
  } else {
    if (p + 3 >= ndefLen) return false;
    payloadLen = ((uint32_t)ndef[p] << 24) |
                 ((uint32_t)ndef[p + 1] << 16) |
                 ((uint32_t)ndef[p + 2] << 8) |
                 (uint32_t)ndef[p + 3];
    p += 4;
  }

  uint8_t idLen = 0;
  if (il) {
    if (p >= ndefLen) return false;
    idLen = ndef[p++];
  }

  if (p + typeLen + idLen + payloadLen > ndefLen) return false;

  const uint8_t* type = &ndef[p];
  p += typeLen;
  p += idLen;
  const uint8_t* payload = &ndef[p];

  out.type = _bytesToString(type, typeLen);
  out.payload = payload;
  out.payloadLen = payloadLen;
  out.valid = true;

  if (out.tnf == 0x01 && typeLen == 1 && type[0] == 'T' && payloadLen >= 1) {
    const uint8_t status = payload[0];
    const bool utf16 = (status & 0x80) != 0;
    const uint8_t langLen = status & 0x3F;
    if ((size_t)1 + langLen > payloadLen) {
      out.valid = false;
      return false;
    }

    out.kind = RECORD_TEXT;
    out.encoding = utf16 ? "UTF-16" : "UTF-8";
    out.language = _bytesToString(payload + 1, langLen);
    if (!utf16)
      out.text = _bytesToString(payload + 1 + langLen,
                                payloadLen - 1 - langLen);
    return true;
  }

  if (out.tnf == 0x01 && typeLen == 1 && type[0] == 'U' && payloadLen >= 1) {
    String uri = _uriPrefix(payload[0]);
    uri += _bytesToString(payload + 1, payloadLen - 1);
    out.uri = uri;

    if (uri.startsWith("mailto:")) {
      out.kind = RECORD_EMAIL;
      out.email = uri.substring(7);
    } else if (uri.startsWith("tel:")) {
      out.kind = RECORD_PHONE;
      out.phone = uri.substring(4);
    } else {
      out.kind = RECORD_URL;
    }
    return true;
  }

  if (out.tnf == 0x02 &&
      (out.type.equalsIgnoreCase("text/vcard") ||
       out.type.equalsIgnoreCase("text/x-vcard"))) {
    out.kind = RECORD_VCARD;
    String vcard = _bytesToString(payload, payloadLen);
    vcard.replace("\r\n", "\n");
    vcard.replace("\r", "\n");

    out.contact = _vcardValue(vcard, "FN");
    if (out.contact.length() == 0) out.contact = _vcardValue(vcard, "N");
    out.company = _vcardValue(vcard, "ORG");
    out.address = _vcardValue(vcard, "ADR");
    out.phone   = _vcardValue(vcard, "TEL");
    out.email   = _vcardValue(vcard, "EMAIL");
    out.website = _vcardValue(vcard, "URL");

    out.contact = _cleanStructured(out.contact);
    out.company = _cleanStructured(out.company);
    out.address = _cleanStructured(out.address);
    return true;
  }

  out.kind = RECORD_UNSUPPORTED;
  return true;
}
