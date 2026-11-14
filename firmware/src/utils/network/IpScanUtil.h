#pragma once
#include <cstdint>
#include <cstddef>

// Host discovery on the current WiFi /24 subnet: ICMP echo sent in batches,
// a host counting as present if it answers the echo or ARP, with retries for
// silent addresses, and in the Aggressive profile mDNS queries too (see
// IpScanUtil.cpp). Uses lwIP raw / etharp APIs
// through tcpip_api_call().
class IpScanUtil {
public:
  // Fast: batched echo + ARP, two attempts, 350 ms per batch (about 22 s for
  // a /24 at worst). Aggressive: three attempts with 800 ms per batch, for
  // devices in deep power save, then mDNS queries for Apple and Cast devices
  // that may still be asleep (their sleep proxies answer for them), which
  // adds about 10 s; roughly a minute and a half for a /24 at worst.
  enum class Profile : uint8_t { Fast, Aggressive };

  struct Host {
    char ip[16];
    char hostname[50];
  };

  // Scans one target IP via ICMP ping.
  // resolveHostnames: attempt reverse DNS.
  static bool scanTarget(const char* targetIp, Host& out,
                         bool resolveHostnames = false,
                         Profile profile = Profile::Fast);

  // Resolves a host name using reverse DNS.
  static bool resolveName(const char* ip, char* out, size_t outLen);

  // Scans startOctet..endOctet on the local subnet (batched ICMP + ARP, two
  // attempts; results in address order).
  // resolveHostnames: attempt reverse DNS.
  // progressCb: optional, called with 0–100 during scan.
  // Returns number of live hosts found, written to out[].
  static uint8_t scan(uint8_t startOctet, uint8_t endOctet,
                      Host* out, uint8_t maxHosts,
                      bool resolveHostnames = false,
                      void (*progressCb)(uint8_t) = nullptr,
                      bool (*cancelCb)() = nullptr,
                      Profile profile = Profile::Fast);
};
