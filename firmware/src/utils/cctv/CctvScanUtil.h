#pragma once

#include <WiFiClient.h>
#include <HTTPClient.h>
#include "ui/actions/ShowProgressAction.h"

class CctvScanUtil {
public:
  struct Camera {
    char ip[16];
    uint16_t port;
    char brand[20];
    char service[12];  // "HTTP" or "RTSP"
  };

  static constexpr uint8_t MAX_CAMERAS = 32;

  // Camera-specific ports to scan
  struct PortEntry { uint16_t port; const char* service; };
  static constexpr PortEntry kPorts[] = {
    // HTTP
    {80,    "HTTP"},    {81,    "HTTP"},    {8080,  "HTTP"},
    {8081,  "HTTP"},    {8082,  "HTTP"},    {8888,  "HTTP"},
    {9000,  "HTTP"},    {443,   "HTTPS"},   {8443,  "HTTPS"},
    {5000,  "HTTP"},    {7001,  "HTTP"},    {49152, "HTTP"},
    // RTSP
    {554,   "RTSP"},    {8554,  "RTSP"},    {10554, "RTSP"},
    // Proprietary
    {37777, "Dahua"},   {34567, "XMEye"},   {3702,  "ONVIF"},
    // RTMP
    {1935,  "RTMP"},
  };
  static constexpr uint8_t kPortCount = sizeof(kPorts) / sizeof(kPorts[0]);

  // Scan camera ports on a single IP. Returns number of cameras found.
  static uint8_t scanPorts(const char* ip, Camera results[], uint8_t maxResults);

  // Try to detect brand from HTTP response on ip:port
  static bool detectBrand(const char* ip, uint16_t port, char* brandOut, size_t brandLen);

  // Try RTSP OPTIONS to check if RTSP service is present
  static bool probeRtsp(const char* ip, uint16_t port, char* brandOut, size_t brandLen);

  // Default credentials list
  struct Credential { const char* username; const char* password; };
  static constexpr Credential kDefaultCreds[] = {
    {"admin",    "admin"},
    {"admin",    "12345"},
    {"admin",    "123456"},
    {"admin",    "password"},
    {"admin",    ""},
    {"root",     "root"},
    {"root",     "pass"},
    {"root",     "admin"},
    {"admin",    "hik12345"},   // Hikvision
    {"admin",    "admin123"},   // Dahua
    {"888888",   "888888"},     // Dahua DVR
    {"666666",   "666666"},     // Dahua DVR
  };
  static constexpr uint8_t kCredCount = sizeof(kDefaultCreds) / sizeof(kDefaultCreds[0]);

  // Test HTTP basic auth credential. Returns true if auth succeeds.
  static bool testCredential(const char* ip, uint16_t port,
                             const char* username, const char* password);

  // Common MJPEG stream paths to try
  static constexpr const char* kStreamPaths[] = {
    "/mjpg/video.mjpg",
    "/video/mjpg.cgi",
    "/mjpeg.cgi",
    "/cgi-bin/video.jpg",
    "/axis-cgi/mjpg/video.cgi",
    "/jpg/image.jpg",
    "/snapshot.cgi",
    "/video",
    "/stream",
    "/image.jpg",
    "/onvif/streaming",
  };
  static constexpr uint8_t kStreamPathCount = sizeof(kStreamPaths) / sizeof(kStreamPaths[0]);

  // Find a working MJPEG stream URL. Returns true if found, writes full URL to urlOut.
  static bool findStream(const char* ip, uint16_t port,
                         const char* username, const char* password,
                         char* urlOut, size_t urlLen);
};
