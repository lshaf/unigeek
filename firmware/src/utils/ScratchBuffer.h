#pragma once

#include <stdlib.h>
#include <stddef.h>

// Lazily allocated scratch buffers.
//
// Several radio/transfer paths need multi-kilobyte scratch arrays. Declaring
// them as function-local `static` arrays is convenient, but the linker then
// reserves every one of them in `dram0_0_seg` for the whole life of the
// firmware — even on a board that never opens the feature. On the classic
// ESP32 (320 KB DRAM, and a much smaller static budget) that reservation is
// the binding constraint: the project's static data alone reached ~125 KB and
// the CYD/StickC boards stopped linking with a ~41 KB overflow.
//
// scratchAlloc() moves the cost to the heap and takes it on first use, so a
// board only pays for the features it actually runs. The pointer is kept after
// the first allocation (these are hot paths — SubGHz RX runs per frame), so
// this trades a permanent static reservation for an on-demand one, not for
// repeated malloc/free churn.
//
// Returns nullptr when the allocation fails; every call site must handle that
// and bail out of the operation rather than proceeding with a null buffer.
template <typename T>
inline T* scratchAlloc(T*& slot, size_t count) {
  if (!slot) slot = (T*)malloc(count * sizeof(T));
  return slot;
}
