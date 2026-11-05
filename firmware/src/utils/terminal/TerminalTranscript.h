#pragma once

#include <Arduino.h>

// Text model behind a terminal screen: turns received bytes into a scrollable
// transcript of lines.
//
// Text mode applies a minimal ANSI/VT100 interpretation for line-oriented
// sessions (shared by the UART, USB Serial, Telnet and SSH terminals). It does
// not emulate a screen: colors/styles and vertical/full-screen operations are
// ignored, while horizontal editing (CR, BS, CUF/CUB/CHA, EL) is applied to the
// current line. Hex mode shows every byte as two hex digits, BYTES_PER_HEX_LINE
// per line.
//
// Completed lines (received, or added with pushLine()) are also passed to an
// optional line sink, used for logging.
class TerminalTranscript {
public:
  using LineSink = void (*)(void* context, const String& line);

  static constexpr int MAX_PARTIAL_LEN      = 160;
  static constexpr int MAX_TRANSCRIPT_CHARS = 4096;
  static constexpr int BYTES_PER_HEX_LINE   = 8;

  void setLineSink(LineSink sink, void* context) { _sink = sink; _sinkContext = context; }

  void clear();
  void setHexMode(bool hex);
  bool hexMode() const { return _hex; }

  // Feed received bytes.
  void feed(const uint8_t* data, size_t len);

  // Append a complete line (local echo, status messages). Commits any
  // partially received line first so the order on screen is preserved.
  void pushLine(const String& line);

  // Commit the partial line, if any (e.g. when the connection closes).
  void flush();

  // Transcript plus the line still being received, ready for TextScrollView.
  String text() const;
  bool empty() const { return _transcript.length() == 0 && _partial.length() == 0; }

private:
  enum AnsiState : uint8_t { ANSI_DATA, ANSI_ESC, ANSI_CSI, ANSI_OSC, ANSI_OSC_ESC };

  String    _transcript;
  String    _partial;
  int       _cursor     = 0;
  String    _ansiParams;
  AnsiState _ansiState  = ANSI_DATA;
  bool      _hex        = false;
  int       _hexCount   = 0;

  LineSink _sink        = nullptr;
  void*    _sinkContext = nullptr;

  void _feedText(uint8_t c);
  void _feedHex(uint8_t c);
  void _putChar(char c);
  int  _ansiParam(int index, int defaultValue) const;
  void _handleCsi(uint8_t finalByte);
  void _commitPartial();
  void _append(const String& line);
  void _trim();
};
