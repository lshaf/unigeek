#pragma once
#include <Arduino.h>
#include <string.h>

namespace EmvReader {

static constexpr uint8_t kMaxApps = 8;

struct Application {
  uint8_t aid[16] = {};
  uint8_t aidLen = 0;
  String label;
  uint8_t priority = 0;
};

inline bool readLength(const uint8_t* data, size_t len, size_t& pos, size_t& outLen) {
  if (pos >= len) return false;
  uint8_t b = data[pos++];
  if ((b & 0x80) == 0) { outLen = b; return pos + outLen <= len; }
  uint8_t n = b & 0x7F;
  if (n == 0 || n > 2 || pos + n > len) return false;
  outLen = 0;
  while (n--) outLen = (outLen << 8) | data[pos++];
  return pos + outLen <= len;
}

inline bool readTag(const uint8_t* data, size_t len, size_t& pos, uint32_t& tag) {
  if (pos >= len) return false;
  tag = data[pos++];
  if ((tag & 0x1F) != 0x1F) return true;
  uint8_t count = 0;
  while (pos < len && count++ < 3) {
    uint8_t b = data[pos++];
    tag = (tag << 8) | b;
    if ((b & 0x80) == 0) return true;
  }
  return false;
}

inline String hex(const uint8_t* data, size_t len) {
  String s;
  for (size_t i = 0; i < len; ++i) {
    char b[3];
    snprintf(b, sizeof(b), "%02X", data[i]);
    s += b;
  }
  return s;
}

inline String ascii(const uint8_t* data, size_t len) {
  String s;
  for (size_t i = 0; i < len; ++i) {
    char c = (char)data[i];
    if (c >= 0x20 && c <= 0x7E) s += c;
  }
  s.trim();
  return s;
}

inline void parseAppTemplate(const uint8_t* data, size_t len, Application& app) {
  size_t pos = 0;
  while (pos < len) {
    uint32_t tag = 0; size_t l = 0;
    if (!readTag(data, len, pos, tag) || !readLength(data, len, pos, l)) return;
    const uint8_t* v = data + pos;
    if (tag == 0x4F && l > 0 && l <= sizeof(app.aid)) {
      memcpy(app.aid, v, l); app.aidLen = (uint8_t)l;
    } else if ((tag == 0x50 || tag == 0x9F12) && app.label.length() == 0) {
      app.label = ascii(v, l);
    } else if (tag == 0x87 && l >= 1) {
      app.priority = v[0];
    } else if (tag == 0x6F || tag == 0xA5 || tag == 0xBF0C || tag == 0x61) {
      parseAppTemplate(v, l, app);
    }
    pos += l;
  }
}

inline void scanTemplates(const uint8_t* data, size_t len, Application* apps, uint8_t& count, uint8_t maxApps) {
  size_t pos = 0;
  while (pos < len && count < maxApps) {
    uint32_t tag = 0; size_t l = 0;
    if (!readTag(data, len, pos, tag) || !readLength(data, len, pos, l)) return;
    const uint8_t* v = data + pos;
    if (tag == 0x61) {
      Application app;
      parseAppTemplate(v, l, app);
      if (app.aidLen) apps[count++] = app;
    } else if (tag == 0x6F || tag == 0xA5 || tag == 0xBF0C) {
      scanTemplates(v, l, apps, count, maxApps);
    }
    pos += l;
  }
}

inline uint8_t parsePpse(const uint8_t* response, size_t len, Application* apps, uint8_t maxApps) {
  if (!response || len < 2 || response[len - 2] != 0x90 || response[len - 1] != 0x00) return 0;
  uint8_t count = 0;
  scanTemplates(response, len - 2, apps, count, maxApps);
  return count;
}

inline size_t buildSelectPpse(uint8_t out[20]) {
  static const uint8_t name[] = {'2','P','A','Y','.','S','Y','S','.','D','D','F','0','1'};
  out[0]=0x00; out[1]=0xA4; out[2]=0x04; out[3]=0x00; out[4]=sizeof(name);
  memcpy(out+5, name, sizeof(name)); out[19]=0x00;
  return 20;
}

} // namespace EmvReader
