#include "services/VendorResolver.h"
#include "services/VendorDatabase.h"
#include "core/Device.h"
#include <cstring>

VendorResolver& VendorResolver::instance() { static VendorResolver resolver; return resolver; }

bool VendorResolver::findCached(const uint8_t mac[6], Result& out) {
  for (auto& entry : _cache) {
    if (entry.valid && memcmp(entry.mac, mac, 6) == 0) {
      entry.lastUsed = ++_clock;
      out = entry.result;
      return true;
    }
  }
  return false;
}

void VendorResolver::remember(const uint8_t mac[6], const Result& result) {
  // Never cache transient failures (timeout or unavailable SD).
  if (result.status != static_cast<uint8_t>(VendorDatabase::Status::Found) &&
      result.status != static_cast<uint8_t>(VendorDatabase::Status::NotFound) &&
      result.status != static_cast<uint8_t>(VendorDatabase::Status::LocallyAdministered)) return;
  CacheEntry* victim = nullptr;
  for (auto& entry : _cache) {
    if (entry.valid && memcmp(entry.mac, mac, 6) == 0) { victim = &entry; break; }
    if (!entry.valid) { victim = &entry; break; }
    if (!victim || entry.lastUsed < victim->lastUsed) victim = &entry;
  }
  if (!victim) return;
  memcpy(victim->mac, mac, 6);
  victim->result = result;
  victim->lastUsed = ++_clock;
  victim->valid = true;
}

bool VendorResolver::request(const uint8_t mac[6]) {
  if (!_started) {
    _mutex = xSemaphoreCreateMutex();
    _requests = xQueueCreate(1, sizeof(Message));
    _results = xQueueCreate(1, sizeof(Message));
    if (!_mutex || !_requests || !_results) return false;
    if (xTaskCreate(worker, "vendor-lookup", 6144, this, 1, nullptr) != pdPASS) return false;
    _started = true;
  }
  Message m = {};
  memcpy(m.mac, mac, 6);
  if (xSemaphoreTake(_mutex, portMAX_DELAY) != pdTRUE) return false;
  memcpy(_activeMac, mac, 6);
  _active = true;
  xQueueReset(_results);
  if (findCached(mac, m.result)) {
    xQueueReset(_requests); // discard pending lookup for a previous screen
    xQueueOverwrite(_results, &m);
    xSemaphoreGive(_mutex);
    return true;
  }
  xSemaphoreGive(_mutex);
  return xQueueOverwrite(_requests, &m) == pdTRUE;
}

bool VendorResolver::poll(const uint8_t mac[6], Result& out) {
  if (!_started) return false;
  Message m;
  if (xSemaphoreTake(_mutex, portMAX_DELAY) != pdTRUE) return false;
  const bool ready = xQueuePeek(_results, &m, 0) == pdTRUE &&
                     memcmp(m.mac, mac, 6) == 0 &&
                     xQueueReceive(_results, &m, 0) == pdTRUE;
  if (ready) out = m.result;
  xSemaphoreGive(_mutex);
  return ready;
}

void VendorResolver::worker(void* arg) {
  auto* self = static_cast<VendorResolver*>(arg);
  Message m;
  for (;;) {
    if (xQueueReceive(self->_requests, &m, portMAX_DELAY) != pdTRUE) continue;
    const uint32_t started = millis();
    auto result = VendorDatabase::lookup(Uni.StorageSD, m.mac, started + 5000);
    m.result.found = result.found;
    m.result.status = static_cast<uint8_t>(result.status);
    memcpy(m.result.name, result.name, sizeof(m.result.name));
    if (xSemaphoreTake(self->_mutex, portMAX_DELAY) == pdTRUE) {
      self->remember(m.mac, m.result);
      // A result for an old screen must not replace the current result.
      if (self->_active && memcmp(self->_activeMac, m.mac, 6) == 0)
        xQueueOverwrite(self->_results, &m);
      xSemaphoreGive(self->_mutex);
    }
    if (result.status == VendorDatabase::Status::DatabaseUnavailable ||
        result.status == VendorDatabase::Status::Timeout) {
      Serial.printf("[VendorLookup] error status=%u elapsed=%lu ms\n",
                    static_cast<unsigned>(result.status),
                    static_cast<unsigned long>(millis() - started));
    }
  }
}
