#pragma once
#include <Arduino.h>

class WebScanUtil {
public:
  struct Result {
    char ip[16];
    uint16_t port;
    bool https;
    int status;
    char server[64];
    char title[80];
  };

  static constexpr uint8_t MAX_RESULTS = 32;

  static bool probe(
    const char* ip,
    uint16_t port,
    bool https,
    Result& out,
    bool patient = false
  );

private:
  static bool _readResponse(
    Client& client,
    const char* ip,
    uint16_t port,
    bool https,
    Result& out,
    bool patient
  );
};
