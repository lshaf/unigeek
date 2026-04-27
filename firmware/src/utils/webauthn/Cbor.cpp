#include "Cbor.h"

#include <string.h>

namespace webauthn {

// ── Writer ────────────────────────────────────────────────────────────────

bool CborWriter::_writeRaw(const uint8_t* p, size_t n)
{
  if (_err) return false;
  if (_pos + n > _cap) { _err = true; return false; }
  memcpy(_buf + _pos, p, n);
  _pos += n;
  return true;
}

bool CborWriter::_writeHeader(uint8_t major, uint64_t value)
{
  uint8_t hdr = (uint8_t)(major << 5);
  if (value <= 23) {
    uint8_t b = hdr | (uint8_t)value;
    return _writeRaw(&b, 1);
  }
  if (value <= 0xFF) {
    uint8_t b[2] = { (uint8_t)(hdr | 24), (uint8_t)value };
    return _writeRaw(b, 2);
  }
  if (value <= 0xFFFF) {
    uint8_t b[3] = {
      (uint8_t)(hdr | 25),
      (uint8_t)((value >> 8) & 0xFF),
      (uint8_t)(value & 0xFF),
    };
    return _writeRaw(b, 3);
  }
  if (value <= 0xFFFFFFFFu) {
    uint8_t b[5] = {
      (uint8_t)(hdr | 26),
      (uint8_t)((value >> 24) & 0xFF),
      (uint8_t)((value >> 16) & 0xFF),
      (uint8_t)((value >>  8) & 0xFF),
      (uint8_t)( value        & 0xFF),
    };
    return _writeRaw(b, 5);
  }
  uint8_t b[9] = {
    (uint8_t)(hdr | 27),
    (uint8_t)((value >> 56) & 0xFF), (uint8_t)((value >> 48) & 0xFF),
    (uint8_t)((value >> 40) & 0xFF), (uint8_t)((value >> 32) & 0xFF),
    (uint8_t)((value >> 24) & 0xFF), (uint8_t)((value >> 16) & 0xFF),
    (uint8_t)((value >>  8) & 0xFF), (uint8_t)( value        & 0xFF),
  };
  return _writeRaw(b, 9);
}

bool CborWriter::putUint(uint64_t v)            { return _writeHeader(CBOR_UINT,   v); }
bool CborWriter::putNegInt(uint64_t magnitude)  { return _writeHeader(CBOR_NEGINT, magnitude); }

bool CborWriter::putInt(int64_t v)
{
  if (v >= 0) return putUint((uint64_t)v);
  // -1 - magnitude, so magnitude = -1 - v = -(v+1)
  return putNegInt((uint64_t)(-(v + 1)));
}

bool CborWriter::putBytes(const uint8_t* data, size_t len)
{
  if (!_writeHeader(CBOR_BYTES, len)) return false;
  return _writeRaw(data, len);
}

bool CborWriter::putText(const char* s, size_t len)
{
  if (!_writeHeader(CBOR_TEXT, len)) return false;
  return _writeRaw((const uint8_t*)s, len);
}

bool CborWriter::putText(const char* s)
{
  return putText(s, strlen(s));
}

bool CborWriter::putBool(bool b)
{
  uint8_t hdr = (uint8_t)((CBOR_SIMPLE << 5) | (b ? 21 : 20));
  return _writeRaw(&hdr, 1);
}

bool CborWriter::putNull()
{
  uint8_t hdr = (uint8_t)((CBOR_SIMPLE << 5) | 22);
  return _writeRaw(&hdr, 1);
}

bool CborWriter::beginArray(size_t count) { return _writeHeader(CBOR_ARRAY, count); }
bool CborWriter::beginMap  (size_t count) { return _writeHeader(CBOR_MAP,   count); }

// ── Reader ────────────────────────────────────────────────────────────────

int CborReader::peekMajor() const
{
  if (_err || _pos >= _len) return -1;
  return (int)(_buf[_pos] >> 5);
}

bool CborReader::_readHeader(uint8_t* major, uint64_t* value)
{
  if (_err)         return false;
  if (_pos >= _len) { _err = true; return false; }

  uint8_t b = _buf[_pos++];
  *major = b >> 5;
  uint8_t info = b & 0x1F;

  if (info <= 23) { *value = info; return true; }
  size_t need = 0;
  switch (info) {
    case 24: need = 1; break;
    case 25: need = 2; break;
    case 26: need = 4; break;
    case 27: need = 8; break;
    default: _err = true; return false;
  }
  if (_pos + need > _len) { _err = true; return false; }
  uint64_t v = 0;
  for (size_t i = 0; i < need; i++) v = (v << 8) | _buf[_pos + i];
  _pos += need;
  *value = v;
  return true;
}

bool CborReader::readUint(uint64_t* v)
{
  uint8_t  m;
  uint64_t x;
  if (!_readHeader(&m, &x)) return false;
  if (m != CBOR_UINT)       { _err = true; return false; }
  *v = x;
  return true;
}

bool CborReader::readInt(int64_t* v)
{
  uint8_t  m;
  uint64_t x;
  if (!_readHeader(&m, &x)) return false;
  if (m == CBOR_UINT) {
    if (x > 0x7FFFFFFFFFFFFFFFULL) { _err = true; return false; }
    *v = (int64_t)x;
    return true;
  }
  if (m == CBOR_NEGINT) {
    if (x > 0x7FFFFFFFFFFFFFFFULL) { _err = true; return false; }
    *v = -1 - (int64_t)x;
    return true;
  }
  _err = true;
  return false;
}

bool CborReader::readBytes(const uint8_t** data, size_t* len)
{
  uint8_t  m;
  uint64_t x;
  if (!_readHeader(&m, &x)) return false;
  if (m != CBOR_BYTES)      { _err = true; return false; }
  if (_pos + x > _len)      { _err = true; return false; }
  *data = _buf + _pos;
  *len  = (size_t)x;
  _pos += (size_t)x;
  return true;
}

bool CborReader::readText(const char** str, size_t* len)
{
  uint8_t  m;
  uint64_t x;
  if (!_readHeader(&m, &x)) return false;
  if (m != CBOR_TEXT)       { _err = true; return false; }
  if (_pos + x > _len)      { _err = true; return false; }
  *str = (const char*)(_buf + _pos);
  *len = (size_t)x;
  _pos += (size_t)x;
  return true;
}

bool CborReader::readBool(bool* b)
{
  uint8_t  m;
  uint64_t x;
  if (!_readHeader(&m, &x)) return false;
  if (m != CBOR_SIMPLE)     { _err = true; return false; }
  if (x == 20)      { *b = false; return true; }
  if (x == 21)      { *b = true;  return true; }
  _err = true;
  return false;
}

bool CborReader::readNull()
{
  uint8_t  m;
  uint64_t x;
  if (!_readHeader(&m, &x)) return false;
  if (m == CBOR_SIMPLE && x == 22) return true;
  _err = true;
  return false;
}

bool CborReader::readArrayHeader(size_t* count)
{
  uint8_t  m;
  uint64_t x;
  if (!_readHeader(&m, &x)) return false;
  if (m != CBOR_ARRAY)      { _err = true; return false; }
  *count = (size_t)x;
  return true;
}

bool CborReader::readMapHeader(size_t* count)
{
  uint8_t  m;
  uint64_t x;
  if (!_readHeader(&m, &x)) return false;
  if (m != CBOR_MAP)        { _err = true; return false; }
  *count = (size_t)x;
  return true;
}

bool CborReader::skip()
{
  if (_err)         return false;
  if (_pos >= _len) { _err = true; return false; }

  uint8_t  m;
  uint64_t x;
  if (!_readHeader(&m, &x)) return false;

  switch (m) {
    case CBOR_UINT:
    case CBOR_NEGINT:
      return true;
    case CBOR_BYTES:
    case CBOR_TEXT:
      if (_pos + x > _len) { _err = true; return false; }
      _pos += (size_t)x;
      return true;
    case CBOR_ARRAY:
      for (size_t i = 0; i < x; i++) if (!skip()) return false;
      return true;
    case CBOR_MAP:
      for (size_t i = 0; i < x * 2; i++) if (!skip()) return false;
      return true;
    case CBOR_TAG:
      return skip();   // skip tagged value too
    case CBOR_SIMPLE:
      return true;     // simple values have no further payload
    default:
      _err = true;
      return false;
  }
}

}  // namespace webauthn
