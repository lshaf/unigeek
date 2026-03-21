#pragma once

#include <WiFiClient.h>

// MJPEG stream reader — connects to HTTP MJPEG URL, parses multipart frames,
// and delivers raw JPEG buffers via callback for TJpgDec decoding.
class CctvStreamUtil {
public:
  // Callback receives JPEG data and its length. Return false to stop streaming.
  using FrameCallback = bool (*)(const uint8_t* jpegBuf, size_t jpegLen, void* userData);

  bool begin(const char* url, const char* username = nullptr, const char* password = nullptr);
  void stop();
  bool isConnected() const { return _client.connected(); }

  // Call each frame. Reads one JPEG frame and calls the callback.
  // Returns false if stream ended or error.
  bool readFrame(FrameCallback cb, void* userData);

private:
  WiFiClient _client;
  String     _boundary;
  bool       _headersParsed = false;

  static constexpr size_t MAX_FRAME = 40 * 1024;  // 40KB max JPEG frame
  uint8_t*  _frameBuf = nullptr;

  bool _parseHttpHeaders();
  bool _readUntilBoundary();
  bool _readFrameData(size_t& outLen);
  int  _readLine(char* buf, size_t maxLen, uint32_t timeoutMs = 2000);
};
