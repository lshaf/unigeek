#pragma once
#include <Arduino.h>
#include <Client.h>

class RemoteShellScanUtil {
public:
  enum Protocol {
    PROTO_SSH,
    PROTO_TELNET,
    PROTO_WINRM_HTTP,
    PROTO_WINRM_HTTPS,
  };

  struct Result {
    char ip[16];
    uint16_t port;
    Protocol protocol;
    char banner[160];
    int status;
  };

  static constexpr uint8_t MAX_RESULTS = 32;

  static bool probeSsh(const char* ip, Result& out, bool patient = false);
  static bool probeTelnet(const char* ip, Result& out, bool patient = false);
  static bool probeWinRm(
    const char* ip,
    bool https,
    Result& out,
    bool patient = false
  );

private:
  static bool _readBanner(Client& client, char* out, size_t outLen, uint32_t timeoutMs);
  static bool _probeWinRmClient(
    Client& client,
    const char* ip,
    uint16_t port,
    bool https,
    Result& out,
    bool patient
  );
};
