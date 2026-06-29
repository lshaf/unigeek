#pragma once

#include <Arduino.h>
#include <WiFi.h>

// ── Responder ─────────────────────────────────────────────────────────────────
// LLMNR (UDP 5355) + NBT-NS (UDP 137) name poisoning: answers name lookups with
// the device's IP so victims connect to us. A fake SMB server (TCP 445) then runs
// the NTLM challenge/response and captures NTLMv2 hashes (hashcat -m 5600) to SD.
//
// Ported from bruce-firmware/src/modules/wifi/responder.cpp
// (originally 7h30th3r0n3 / Evil-M5Project). Driven cooperatively via poll().
class ResponderUtil {
public:
  using LogFn = void (*)(void* ctx, const char* msg, uint16_t color);
  using HitFn = void (*)(void* ctx);   // fired when a hash is captured

  // nbName/nbDomain/dnsDomain are the identity we advertise (e.g. "UNIGEEK").
  bool begin(const char* nbName, const char* nbDomain, const char* dnsDomain,
             LogFn log, HitFn hit, void* ctx);
  void poll();
  void stop();

  uint32_t hashCount() const;
  String   lastUser() const;
  String   lastDomain() const;
  String   lastClient() const;
  String   lastQuery() const;       // last poisoned name
  String   lastProtocol() const;    // "LLMNR" / "NBNS"

  ~ResponderUtil() { stop(); }
};
