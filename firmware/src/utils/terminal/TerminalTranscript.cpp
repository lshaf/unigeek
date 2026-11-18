#include "TerminalTranscript.h"

void TerminalTranscript::clear() {
  _transcript = "";
  _partial    = "";
  _cursor     = 0;
  _ansiParams = "";
  _ansiState  = ANSI_DATA;
  _hexCount   = 0;
}

void TerminalTranscript::setHexMode(bool hex) {
  if (_hex == hex) return;
  if (_partial.length() > 0) _commitPartial();
  _hex       = hex;
  _cursor    = 0;
  _hexCount  = 0;
  _ansiState = ANSI_DATA;
}

void TerminalTranscript::feed(const uint8_t* data, size_t len) {
  if (!data) return;
  for (size_t i = 0; i < len; i++) {
    if (_hex) _feedHex(data[i]);
    else      _feedText(data[i]);
  }
}

void TerminalTranscript::pushLine(const String& line) {
  if (_partial.length() > 0) _commitPartial();
  _append(line);
}

void TerminalTranscript::flush() {
  if (_partial.length() > 0) _commitPartial();
}

String TerminalTranscript::text() const {
  if (_partial.length() == 0) return _transcript;
  return _transcript + _partial;
}

void TerminalTranscript::_feedHex(uint8_t c) {
  static const char HEX_DIGITS[] = "0123456789ABCDEF";
  if (_hexCount > 0) _partial += ' ';
  _partial += HEX_DIGITS[c >> 4];
  _partial += HEX_DIGITS[c & 0x0F];
  if (++_hexCount >= BYTES_PER_HEX_LINE) _commitPartial();
}

void TerminalTranscript::_feedText(uint8_t c) {
  switch (_ansiState) {
    case ANSI_ESC:
      _ansiParams = "";
      if (c == '[')      _ansiState = ANSI_CSI;
      else if (c == ']') _ansiState = ANSI_OSC;
      else               _ansiState = ANSI_DATA;
      return;

    case ANSI_CSI:
      if (c >= 0x40 && c <= 0x7e) {
        _handleCsi(c);
        _ansiState  = ANSI_DATA;
        _ansiParams = "";
      } else if (_ansiParams.length() < 24 &&
                 ((c >= '0' && c <= '9') || c == ';' || c == '?' || c == '>')) {
        _ansiParams += (char)c;
      }
      return;

    case ANSI_OSC:
      // OSC ends on BEL or ST (ESC \). Its content is metadata (often a window
      // title), not transcript text.
      if (c == 0x07)      _ansiState = ANSI_DATA;
      else if (c == 0x1b) _ansiState = ANSI_OSC_ESC;
      return;

    case ANSI_OSC_ESC:
      _ansiState = (c == '\\') ? ANSI_DATA : ANSI_OSC;
      return;

    case ANSI_DATA:
    default:
      if (c == 0x1b) {
        _ansiState = ANSI_ESC;
        return;
      }
      break;
  }

  // CR returns to column zero. If followed by LF, LF commits the resulting
  // line; a bare CR lets the following bytes overwrite that line.
  if (c == '\r') {
    _cursor = 0;
    return;
  }

  if (c == '\n') {
    _commitPartial();
    return;
  }

  // Backspace moves left; the usual "BS SP BS" erase sequence therefore works
  // with overwrite semantics.
  if (c == '\b' || c == 0x7f) {
    if (_cursor > 0) _cursor--;
    return;
  }

  if (c == '\t') {
    int spaces = 8 - (_cursor % 8);  // tab stops every 8 columns
    while (spaces-- > 0) _putChar(' ');
  } else if (c >= 0x20 && c <= 0x7e) {
    _putChar((char)c);
  }
  // Other control/binary bytes are ignored in text mode (use hex mode).

  if ((int)_partial.length() >= MAX_PARTIAL_LEN) _commitPartial();
}

void TerminalTranscript::_putChar(char c) {
  if (_cursor < 0) _cursor = 0;
  while ((int)_partial.length() < _cursor) _partial += ' ';

  if (_cursor < (int)_partial.length()) _partial.setCharAt(_cursor, c);
  else                                  _partial += c;
  _cursor++;
}

int TerminalTranscript::_ansiParam(int index, int defaultValue) const {
  int  current   = 0;
  int  value     = 0;
  bool haveDigit = false;

  for (int i = 0; i <= (int)_ansiParams.length(); i++) {
    char c = (i < (int)_ansiParams.length()) ? _ansiParams[i] : ';';
    if (c >= '0' && c <= '9') {
      value = value * 10 + (c - '0');
      haveDigit = true;
    } else if (c == ';') {
      if (current == index) return haveDigit ? value : defaultValue;
      current++;
      value = 0;
      haveDigit = false;
    }
  }
  return defaultValue;
}

void TerminalTranscript::_handleCsi(uint8_t finalByte) {
  int n = _ansiParam(0, 1);
  if (n < 1) n = 1;

  switch (finalByte) {
    case 'C':  // CUF: cursor forward
      _cursor = min((int)_partial.length(), _cursor + n);
      break;

    case 'D':  // CUB: cursor backward
      _cursor = max(0, _cursor - n);
      break;

    case 'G':  // CHA: horizontal absolute, 1-based
      _cursor = max(0, n - 1);
      if (_cursor > MAX_PARTIAL_LEN) _cursor = MAX_PARTIAL_LEN;
      break;

    case 'K': {  // EL: erase in line
      int mode = _ansiParam(0, 0);
      if (mode == 2) {
        _partial = "";
        _cursor  = 0;
      } else if (mode == 1) {
        int upto = min(_cursor + 1, (int)_partial.length());
        for (int i = 0; i < upto; i++) _partial.setCharAt(i, ' ');
      } else if (_cursor < (int)_partial.length()) {
        _partial.remove(_cursor);
      }
      break;
    }

    // SGR ('m') and screen/cursor operations (J/A/B/H/f, ...) are ignored: the
    // output is a scrollable transcript, not a VT100 screen.
    default:
      break;
  }
}

void TerminalTranscript::_commitPartial() {
  const String line = _partial;
  _partial  = "";
  _cursor   = 0;
  _hexCount = 0;
  _append(line);
}

void TerminalTranscript::_append(const String& line) {
  _transcript += line;
  _transcript += '\n';
  _trim();
  if (_sink) _sink(_sinkContext, line);
}

void TerminalTranscript::_trim() {
  if ((int)_transcript.length() <= MAX_TRANSCRIPT_CHARS) return;

  int excess = (int)_transcript.length() - MAX_TRANSCRIPT_CHARS;
  int cut = _transcript.indexOf('\n', excess);
  if (cut >= 0) _transcript.remove(0, cut + 1);
  else          _transcript.remove(0, excess);
}
