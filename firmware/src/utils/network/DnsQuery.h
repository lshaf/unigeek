#pragma once
#include <Arduino.h>
#include <IPAddress.h>
#include <WiFiUdp.h>

// Minimal DNS client for the Toolbox's DNS Lookup: one query of a chosen type
// to a chosen server over UDP, parsed into printable records. Non-blocking:
// start() sends, poll() is called until it stops returning Pending (one
// resend after 2 s, gives up after 4 s).
class DnsQuery {
public:
  enum Type : uint16_t { A = 1, NS = 2, CNAME = 5, PTR = 12, MX = 15, TXT = 16, AAAA = 28 };
  enum class Status : uint8_t { Pending, Done, Timeout, Error };

  struct Record {
    uint16_t type;
    uint32_t ttl;
    char     text[96];
  };
  static constexpr uint8_t MAX_RECORDS = 12;

  // For PTR, a dotted IPv4 name is turned into its in-addr.arpa form.
  bool start(const IPAddress& server, const String& name, Type type);
  Status poll();
  void stop();

  uint8_t       count() const { return _count; }
  const Record& record(uint8_t i) const { return _records[i]; }
  uint8_t       rcode() const { return _rcode; }   // 0 = ok, 3 = no such name
  uint32_t      elapsedMs() const { return _doneAt - _sentAt; }

  static const char* typeName(uint16_t type);
  static const char* rcodeName(uint8_t rcode);

private:
  WiFiUDP       _udp;
  IPAddress     _server;
  uint8_t       _packet[300];
  size_t        _packetLen = 0;
  uint16_t      _id = 0;
  bool          _resent = false;
  bool          _active = false;
  unsigned long _sentAt = 0;
  unsigned long _doneAt = 0;
  Record        _records[MAX_RECORDS];
  uint8_t       _count = 0;
  uint8_t       _rcode = 0;

  bool _parse(const uint8_t* msg, size_t len);
};
