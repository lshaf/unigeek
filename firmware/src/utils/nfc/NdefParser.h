#pragma once
#include <Arduino.h>
#include <stddef.h>
#include <stdint.h>

class NdefParser {
public:
  enum RecordKind {
    RECORD_UNSUPPORTED,
    RECORD_TEXT,
    RECORD_URL,
    RECORD_PHONE,
    RECORD_EMAIL,
    RECORD_VCARD,
  };

  struct Result {
    bool valid = false;
    RecordKind kind = RECORD_UNSUPPORTED;
    uint8_t tnf = 0;
    String type;

    // View into the original NDEF buffer. Valid only while that buffer remains alive.
    const uint8_t* payload = nullptr;
    size_t payloadLen = 0;

    String encoding;
    String language;
    String text;
    String uri;

    String contact;
    String company;
    String address;
    String phone;
    String email;
    String website;
  };

  // Parse the first NDEF record. Mirrors the parser used by the NFC Tools
  // preview/editor flows: Text, URI/URL, Phone, Email and vCard.
  static bool parse(const uint8_t* ndef, size_t ndefLen, Result& out);

  // Extract the NDEF Message TLV from an NFC Forum Type-2 memory image.
  // `dump` must begin at page 0; user memory starts at page 4 (offset 16).
  static bool extractType2Ndef(const uint8_t* dump, size_t dumpLen,
                               const uint8_t** ndef, size_t* ndefLen);

private:
  static String _uriPrefix(uint8_t code);
  static String _bytesToString(const uint8_t* data, size_t len);
  static String _vcardValue(const String& vcard, const char* key);
  static String _cleanStructured(String value);
};
