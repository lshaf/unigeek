#include "NdefBuilder.h"

#include <cstring>

bool NdefBuilder::buildUri(const String& input,
                           uint8_t prefix,
                           const char* stripPrefix,
                           uint8_t* out, size_t& outLen, size_t maxLen) {
  if (!out) return false;

  String body = input;
  if (stripPrefix && body.startsWith(stripPrefix)) {
    body = body.substring(strlen(stripPrefix));
  }

  const size_t payloadLen = 1 + body.length();
  const size_t ndefLen = 4 + payloadLen;
  if (payloadLen > 255 || ndefLen > maxLen) return false;

  out[0] = 0xD1;                  // MB | ME | SR | TNF=Well Known
  out[1] = 0x01;                  // Type length
  out[2] = (uint8_t)payloadLen;
  out[3] = 'U';                   // URI record
  out[4] = prefix;
  memcpy(&out[5], body.c_str(), body.length());
  outLen = ndefLen;
  return true;
}

bool NdefBuilder::buildText(const String& text,
                            uint8_t* out, size_t& outLen, size_t maxLen) {
  if (!out) return false;

  // Short-record NDEF Text: D1 01 <payloadLen> 54 02 'e' 'n' <text>
  const size_t payloadLen = 3 + text.length();
  const size_t ndefLen = 4 + payloadLen;
  if (payloadLen > 255 || ndefLen > maxLen) return false;

  out[0] = 0xD1;
  out[1] = 0x01;
  out[2] = (uint8_t)payloadLen;
  out[3] = 'T';
  out[4] = 0x02;                  // UTF-8, language code length 2
  out[5] = 'e';
  out[6] = 'n';
  memcpy(&out[7], text.c_str(), text.length());
  outLen = ndefLen;
  return true;
}

bool NdefBuilder::buildUrl(const String& url,
                           uint8_t* out, size_t& outLen, size_t maxLen) {
  uint8_t prefix = 0x00;
  const char* strip = nullptr;

  if      (url.startsWith("https://www.")) { prefix = 0x02; strip = "https://www."; }
  else if (url.startsWith("http://www."))  { prefix = 0x01; strip = "http://www."; }
  else if (url.startsWith("https://"))      { prefix = 0x04; strip = "https://"; }
  else if (url.startsWith("http://"))       { prefix = 0x03; strip = "http://"; }

  return buildUri(url, prefix, strip, out, outLen, maxLen);
}

bool NdefBuilder::buildPhone(const String& phone,
                             uint8_t* out, size_t& outLen, size_t maxLen) {
  return buildUri(phone, 0x05, "tel:", out, outLen, maxLen);
}

bool NdefBuilder::buildEmail(const String& email,
                             uint8_t* out, size_t& outLen, size_t maxLen) {
  return buildUri(email, 0x06, "mailto:", out, outLen, maxLen);
}

String NdefBuilder::escapeVcard(String value) {
  value.replace("\\", "\\\\");
  value.replace("\r", "");
  value.replace("\n", "\\n");
  value.replace(";", "\\;");
  value.replace(",", "\\,");
  return value;
}

bool NdefBuilder::buildVcard(const String& contact,
                             const String& company,
                             const String& address,
                             const String& phone,
                             const String& email,
                             const String& website,
                             uint8_t* out, size_t& outLen, size_t maxLen) {
  if (!out) return false;

  String card = "BEGIN:VCARD\r\nVERSION:3.0\r\n";
  card += "FN:" + escapeVcard(contact) + "\r\n";
  if (company.length()) card += "ORG:" + escapeVcard(company) + "\r\n";
  if (address.length()) card += "ADR:;;" + escapeVcard(address) + ";;;;\r\n";
  if (phone.length()) card += "TEL:" + escapeVcard(phone) + "\r\n";
  if (email.length()) card += "EMAIL:" + escapeVcard(email) + "\r\n";
  if (website.length() && website != "https://") {
    card += "URL:" + escapeVcard(website) + "\r\n";
  }
  card += "END:VCARD\r\n";

  static constexpr const char* MIME = "text/vcard";
  const size_t typeLen = strlen(MIME);
  const size_t payloadLen = card.length();
  const size_t ndefLen = 3 + typeLen + payloadLen;
  if (payloadLen > 255 || ndefLen > maxLen) return false;

  out[0] = 0xD2;                  // MB | ME | SR | TNF=MIME Media
  out[1] = (uint8_t)typeLen;
  out[2] = (uint8_t)payloadLen;
  memcpy(&out[3], MIME, typeLen);
  memcpy(&out[3 + typeLen], card.c_str(), payloadLen);
  outLen = ndefLen;
  return true;
}
