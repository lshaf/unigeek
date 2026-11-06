#include "utils/ble/BleTagRing.h"

namespace {

// AirTag (Apple proprietary): one byte, no notifications; the tag closes the
// connection when it accepts.
const NimBLEUUID kAirtagService("7dfc9000-7d1c-4951-86aa-8d9728f8d66c");
const NimBLEUUID kAirtagChar   ("7dfc9001-7d1c-4951-86aa-8d9728f8d66c");
constexpr uint8_t kAirtagPlay = 0xAF;

// DULT (open standard): Sound_Start 0x0300 / Sound_Stop 0x0301, little-endian.
const NimBLEUUID kDultService("15190001-12f4-c226-88ed-2ac5579f2a85");
const NimBLEUUID kDultChar   ("8e0c0001-1d68-fb92-bf61-48377421680e");
const uint8_t kDultStart[2] = {0x00, 0x03};
const uint8_t kDultStop[2]  = {0x01, 0x03};

bool writeChar(NimBLEClient* client, const NimBLEUUID& svc, const NimBLEUUID& chr,
               const uint8_t* data, size_t len) {
  NimBLERemoteService* service = client->getService(svc);
  if (!service) return false;
  NimBLERemoteCharacteristic* c = service->getCharacteristic(chr);
  if (!c) return false;
  return c->writeValue(data, len, true);  // write with response
}

}  // namespace

BleTagRing::Result BleTagRing::play(const NimBLEAddress& address) { return send(address, true); }
BleTagRing::Result BleTagRing::stop(const NimBLEAddress& address) { return send(address, false); }

BleTagRing::Result BleTagRing::send(const NimBLEAddress& address, bool start) {
  NimBLEClient* client = NimBLEDevice::createClient();
  if (!client) return Result::NotReachable;
  client->setConnectTimeout(6);  // seconds

  Result result = Result::NotReachable;
  if (client->connect(address, false)) {
    // AirTag first (its own service), then DULT.
    if (client->getService(kAirtagService)) {
      const uint8_t b = kAirtagPlay;  // AirTag has no documented non-owner stop
      const bool ok = start ? writeChar(client, kAirtagService, kAirtagChar, &b, 1) : true;
      // The tag drops the link on success; either outcome means "handled".
      result = ok || !client->isConnected() ? Result::Playing : Result::RejectedOwnerNearby;
    } else if (client->getService(kDultService)) {
      const uint8_t* cmd = start ? kDultStart : kDultStop;
      result = writeChar(client, kDultService, kDultChar, cmd, 2)
                 ? Result::Playing : Result::RejectedOwnerNearby;
    } else {
      result = Result::NotSupported;
    }
  }

  if (client->isConnected()) client->disconnect();
  NimBLEDevice::deleteClient(client);
  return result;
}

const char* BleTagRing::resultText(Result r) {
  switch (r) {
    case Result::Playing:             return "Playing";
    case Result::RejectedOwnerNearby: return "Rejected (owner nearby?)";
    case Result::NotReachable:        return "Not reachable";
    default:                          return "Not supported";
  }
}
