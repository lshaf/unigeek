#pragma once
#include <Arduino.h>
#include <Client.h>

class FileServiceScanUtil {
public:
  enum Service {
    SERVICE_FTP,
    SERVICE_SFTP_CANDIDATE,
    SERVICE_SMB,
    SERVICE_WEBDAV_HTTP,
    SERVICE_WEBDAV_HTTPS,
  };

  struct Result {
    char ip[16];
    uint16_t port;
    Service service;
    int status;
    char info[96];
  };

  static constexpr uint8_t MAX_RESULTS = 40;

  static bool probeFtp(const char* ip, Result& out, bool patient = false);
  static bool probeSftpCandidate(
    const char* ip,
    Result& out,
    bool patient = false
  );
  static bool probeSmb(
    const char* ip,
    uint16_t port,
    Result& out,
    bool patient = false
  );
  static bool probeWebDav(
    const char* ip,
    uint16_t port,
    bool https,
    Result& out,
    bool patient = false
  );

private:
  static bool _readLine(
    Client& client,
    char* out,
    size_t outLen,
    uint32_t timeoutMs
  );

  static bool _probeSmbDirect(
    Client& client,
    const char* ip,
    uint16_t port,
    Result& out,
    bool patient
  );

  static bool _startNetbiosSession(Client& client, bool patient);

  static bool _probeWebDavClient(
    Client& client,
    const char* ip,
    uint16_t port,
    bool https,
    Result& out,
    bool patient
  );
};
