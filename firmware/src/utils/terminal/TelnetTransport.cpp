#include "TelnetTransport.h"

bool TelnetTransport::_open() {
  _state    = TN_DATA;
  _command  = 0;
  _sbOption = false;
  return TcpTransport::_open();
}

size_t TelnetTransport::_readHardware(uint8_t* buf, size_t capacity) {
  uint8_t raw[128];
  const size_t n = TcpTransport::_readHardware(raw, min(capacity, sizeof(raw)));

  size_t out = 0;
  for (size_t i = 0; i < n; i++) {
    if (_process(raw[i])) buf[out++] = raw[i];
  }
  return out;
}

bool TelnetTransport::_process(uint8_t c) {
  switch (_state) {
    case TN_DATA:
      if (c == IAC) {
        _state = TN_IAC;
        return false;
      }
      return true;

    case TN_IAC:
      if (c == WILL || c == WONT || c == DO || c == DONT) {
        _command = c;
        _state   = TN_NEGOTIATE;
      } else if (c == SB) {
        _sbOption = false;
        _state    = TN_SB;
      } else {
        // IAC IAC is an escaped 0xFF data byte, which text mode can't show;
        // other one-byte commands (NOP, GA, ...) are ignored.
        _state = TN_DATA;
      }
      return false;

    case TN_NEGOTIATE:
      _negotiate(_command, c);
      _state = TN_DATA;
      return false;

    case TN_SB:
      if (!_sbOption)   _sbOption = true;  // option byte
      else if (c == IAC) _state = TN_SB_IAC;
      return false;                         // payload ignored

    case TN_SB_IAC:
      if (c == SE) {
        _state    = TN_DATA;
        _sbOption = false;
      } else {
        _state = TN_SB;
      }
      return false;
  }
  return false;
}

void TelnetTransport::_negotiate(uint8_t command, uint8_t option) {
  if (command == WILL) {
    if (option == OPT_ECHO || option == OPT_SGA) _reply(DO, option);
    else                                         _reply(DONT, option);
  } else if (command == DO) {
    if (option == OPT_SGA) {
      _reply(WILL, option);
    } else if (option == OPT_NAWS) {
      _reply(WILL, option);
      _sendNaws();
    } else {
      _reply(WONT, option);
    }
  }
  // DONT / WONT need no answer from this minimal client.
}

void TelnetTransport::_reply(uint8_t command, uint8_t option) {
  const uint8_t reply[3] = {IAC, command, option};
  _writeHardware(reply, sizeof(reply));
}

void TelnetTransport::_sendNaws() {
  // RFC 1073: IAC SB NAWS <width16> <height16> IAC SE, with 0xFF bytes doubled.
  uint8_t packet[3 + 8 + 2];
  size_t  len = 0;

  packet[len++] = IAC;
  packet[len++] = SB;
  packet[len++] = OPT_NAWS;

  auto append = [&](uint8_t b) {
    packet[len++] = b;
    if (b == IAC) packet[len++] = b;
  };
  append((uint8_t)(_cols >> 8));
  append((uint8_t)(_cols & 0xff));
  append((uint8_t)(_rows >> 8));
  append((uint8_t)(_rows & 0xff));

  packet[len++] = IAC;
  packet[len++] = SE;

  _writeHardware(packet, len);
}
