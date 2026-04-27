#pragma once

#include <stdint.h>
#include <stddef.h>

namespace webauthn {

// Minimal CBOR codec (RFC 8949) covering the subset CTAP2 needs:
//   major type 0  unsigned integer
//   major type 1  negative integer
//   major type 2  byte string (definite length)
//   major type 3  text string (definite length)
//   major type 4  array        (definite length)
//   major type 5  map          (definite length)
//   major type 7  simple values  (false, true, null)
//
// Indefinite-length items, tags, and floats are not supported. Both
// encoder and decoder are zero-allocation: the encoder writes into a
// caller-provided buffer; the decoder returns pointers into the input.
//
// Canonical CTAP2 encoding rules:
//   - Integer values use the shortest possible encoding (handled here).
//   - Map keys must be in canonical order (shorter encoding first, then
//     lexicographic). The caller is responsible for emitting map entries
//     in the right order — CTAP2 maps are keyed by small uints (0x01,
//     0x02, ...) so emitting them in ascending integer order is enough.

// Major type values
enum CborMajor : uint8_t {
  CBOR_UINT   = 0,
  CBOR_NEGINT = 1,
  CBOR_BYTES  = 2,
  CBOR_TEXT   = 3,
  CBOR_ARRAY  = 4,
  CBOR_MAP    = 5,
  CBOR_TAG    = 6,
  CBOR_SIMPLE = 7,
};

class CborWriter {
public:
  CborWriter(uint8_t* buf, size_t cap) : _buf(buf), _cap(cap) {}

  bool   ok()   const { return !_err; }
  size_t size() const { return _pos; }

  bool putUint  (uint64_t v);
  bool putNegInt(uint64_t magnitude);  // encodes -1 - magnitude
  bool putInt   (int64_t v);           // dispatches to putUint / putNegInt
  bool putBytes (const uint8_t* data, size_t len);
  bool putText  (const char* s, size_t len);
  bool putText  (const char* s);       // strlen
  bool putBool  (bool b);
  bool putNull  ();

  // Open a definite-length array/map. Caller must follow with `count`
  // values (or 2*count items for maps).
  bool beginArray(size_t count);
  bool beginMap  (size_t count);

private:
  bool _writeRaw(const uint8_t* p, size_t n);
  bool _writeHeader(uint8_t major, uint64_t value);

  uint8_t* _buf;
  size_t   _cap;
  size_t   _pos = 0;
  bool     _err = false;
};

class CborReader {
public:
  CborReader(const uint8_t* buf, size_t len) : _buf(buf), _len(len) {}

  bool   ok()        const { return !_err; }
  size_t pos()       const { return _pos; }
  size_t remaining() const { return _err ? 0 : _len - _pos; }

  // Peek the major type of the next item without consuming it.
  // Returns -1 if the buffer is empty or in error.
  int peekMajor() const;

  bool readUint  (uint64_t* v);
  bool readInt   (int64_t* v);
  bool readBytes (const uint8_t** data, size_t* len);
  bool readText  (const char**    str,  size_t* len);
  bool readBool  (bool* b);
  bool readNull  ();
  bool readArrayHeader(size_t* count);
  bool readMapHeader  (size_t* count);

  // Skip the next item entirely (recursing into arrays/maps).
  bool skip();

private:
  bool _readHeader(uint8_t* major, uint64_t* value);

  const uint8_t* _buf;
  size_t         _len;
  size_t         _pos = 0;
  bool           _err = false;
};

}  // namespace webauthn
