#pragma once
#include <stdint.h>
#include <stddef.h>

// Nordic UART Service peripheral for Claude Desktop Hardware Buddy protocol.
// Service  6e400001-b5a3-f393-e0a9-e50e24dcca9e
// RX char  6e400002  (desktop writes → device receives)
// TX char  6e400003  (device notifies → desktop reads)
// Wire format: UTF-8 JSON, one object per \n-terminated line.

void   buddyNusInit(const char* name);
void   buddyNusDeinit();
bool   buddyNusConnected();
size_t buddyNusAvailable();
int    buddyNusRead();
void   buddyNusWrite(const uint8_t* data, size_t len);
