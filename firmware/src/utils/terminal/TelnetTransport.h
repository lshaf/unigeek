#pragma once

#include "TcpTransport.h"

// Telnet over TCP: strips the protocol's in-band commands (IAC ...) from the
// received data and answers option negotiation as a minimal client:
//   - accepts the server's ECHO and SUPPRESS-GO-AHEAD (the server echoes what
//     the user types, so the screen keeps local echo off);
//   - offers SUPPRESS-GO-AHEAD and NAWS (window size: cols x rows given at
//     construction) when asked;
//   - refuses every other option. Subnegotiation payloads are ignored.
class TelnetTransport : public TcpTransport {
public:
  TelnetTransport(const String& host, uint16_t port, uint16_t cols, uint16_t rows)
    : TcpTransport(host, port), _cols(cols), _rows(rows) {}
  ~TelnetTransport() override { end(); }

protected:
  bool   _open() override;
  size_t _readHardware(uint8_t* buf, size_t capacity) override;

private:
  enum State : uint8_t { TN_DATA, TN_IAC, TN_NEGOTIATE, TN_SB, TN_SB_IAC };

  static constexpr uint8_t IAC  = 255;
  static constexpr uint8_t DONT = 254;
  static constexpr uint8_t DO   = 253;
  static constexpr uint8_t WONT = 252;
  static constexpr uint8_t WILL = 251;
  static constexpr uint8_t SB   = 250;
  static constexpr uint8_t SE   = 240;
  static constexpr uint8_t OPT_ECHO = 1;
  static constexpr uint8_t OPT_SGA  = 3;
  static constexpr uint8_t OPT_NAWS = 31;

  uint16_t _cols;
  uint16_t _rows;
  State    _state      = TN_DATA;
  uint8_t  _command    = 0;
  bool     _sbOption   = false;

  // Returns true if c is a data byte for the terminal.
  bool _process(uint8_t c);
  void _negotiate(uint8_t command, uint8_t option);
  void _reply(uint8_t command, uint8_t option);
  void _sendNaws();
};
