#pragma once
#include <Arduino.h>
#include <IPAddress.h>

// Single ICMP echo probes with a chosen TTL, for Ping and Traceroute.
// Non-blocking: send() fires one echo, poll() reports its outcome once known,
// so the UI can keep reading the keys (Stop) while waiting. One probe at a
// time; replies are matched by id and sequence number, including the copy of
// the echo that routers return inside "time exceeded" / "unreachable".
// lwIP raw calls run in the tcpip thread through tcpip_api_call().
class IcmpProbe {
public:
  enum class Result : uint8_t {
    Pending,       // still waiting
    Reply,         // echo reply from the target
    TimeExceeded,  // TTL ran out at a router (Traceroute hop)
    Unreachable,   // destination / network unreachable
    Timeout,
  };

  bool open();
  void close();

  // Sends one echo (payloadSize bytes of data) with the given TTL.
  bool send(const IPAddress& target, uint8_t ttl, uint16_t seq, uint16_t payloadSize,
            uint32_t timeoutMs);

  // Outcome of the last send(); `from` is the replying address and `rttMs`
  // the round trip, once the result is no longer Pending.
  Result poll(IPAddress& from, uint32_t& rttMs);
};
