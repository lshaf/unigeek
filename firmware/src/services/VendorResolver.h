#pragma once
#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>

// One worker, one pending request and one completed result; UI polls by MAC.
class VendorResolver {
public:
  struct Result { bool found = false; uint8_t status = 1; char name[64] = {}; };
  static VendorResolver& instance();
  bool request(const uint8_t mac[6]);
  bool poll(const uint8_t mac[6], Result& out);
private:
  struct Message { uint8_t mac[6]; Result result; };
  static void worker(void* arg);
  QueueHandle_t _requests = nullptr;
  QueueHandle_t _results = nullptr;
  static constexpr size_t CACHE_SIZE = 64;
  struct CacheEntry {
    uint8_t mac[6] = {};
    Result result;
    uint32_t lastUsed = 0;
    bool valid = false;
  };
  CacheEntry _cache[CACHE_SIZE] = {};
  SemaphoreHandle_t _mutex = nullptr;
  uint32_t _clock = 0;
  uint8_t _activeMac[6] = {};
  bool _active = false;
  bool _started = false;
  bool findCached(const uint8_t mac[6], Result& out); // caller holds mutex
  void remember(const uint8_t mac[6], const Result& result); // caller holds mutex
};
