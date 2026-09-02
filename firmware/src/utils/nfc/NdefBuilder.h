#pragma once

#include <Arduino.h>
#include <stddef.h>
#include <stdint.h>

class NdefBuilder {
public:
  static bool buildText(const String& text,
                        uint8_t* out, size_t& outLen, size_t maxLen);

  static bool buildUrl(const String& url,
                       uint8_t* out, size_t& outLen, size_t maxLen);

  static bool buildPhone(const String& phone,
                         uint8_t* out, size_t& outLen, size_t maxLen);

  static bool buildEmail(const String& email,
                         uint8_t* out, size_t& outLen, size_t maxLen);

  static bool buildVcard(const String& contact,
                         const String& company,
                         const String& address,
                         const String& phone,
                         const String& email,
                         const String& website,
                         uint8_t* out, size_t& outLen, size_t maxLen);

private:
  static bool buildUri(const String& input,
                       uint8_t prefix,
                       const char* stripPrefix,
                       uint8_t* out, size_t& outLen, size_t maxLen);

  static String escapeVcard(String value);
};
