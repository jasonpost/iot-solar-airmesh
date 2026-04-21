#include "victron_monitor.h"

#include <NimBLEDevice.h>
#include <ctype.h>
#include <mbedtls/aes.h>

#include "router_box_config.h"

namespace {
using namespace RouterBoxConfig;

constexpr uint16_t kVictronCompanyId = 0x02E1;
constexpr uint8_t kVictronInstantReadoutRecord = 0x10;
constexpr uint8_t kVictronSolarChargerRecord = 0x01;
constexpr size_t kVictronHeaderLength = 8;
constexpr size_t kVictronAesKeyLength = 16;
constexpr size_t kVictronNonceLength = 16;
constexpr size_t kVictronCipherLength = 12;
constexpr uint16_t kInvalidSigned16 = 0x7FFF;
constexpr uint16_t kInvalidUnsigned16 = 0xFFFF;
constexpr uint16_t kInvalidUnsigned9 = 0x01FF;

void logVictronLine(const String &message) {
  Serial.println("[victron] " + message);
}

bool sameFloatValue(float left, float right) {
  if (isnan(left) && isnan(right)) {
    return true;
  }

  if (isnan(left) || isnan(right)) {
    return false;
  }

  return left == right;
}

bool sameTelemetryPayload(const VictronTelemetry &left, const VictronTelemetry &right) {
  return left.chargeStateCode == right.chargeStateCode &&
         left.chargerErrorCode == right.chargerErrorCode &&
         sameFloatValue(left.batteryVoltage, right.batteryVoltage) &&
         sameFloatValue(left.chargeCurrent, right.chargeCurrent) &&
         sameFloatValue(left.solarPower, right.solarPower) &&
         sameFloatValue(left.yieldTodayWh, right.yieldTodayWh) &&
         sameFloatValue(left.loadCurrent, right.loadCurrent) && left.rssi == right.rssi;
}

class BitReader {
 public:
  BitReader(const uint8_t *data, size_t length) : data_(data), length_(length) {}

  uint32_t readUnsigned(uint8_t bitCount) {
    uint32_t value = 0;
    for (uint8_t i = 0; i < bitCount; ++i) {
      const size_t bitIndex = bitOffset_ + i;
      const size_t byteIndex = bitIndex / 8;
      if (byteIndex >= length_) {
        break;
      }

      const uint8_t bitInByte = bitIndex % 8;
      if ((data_[byteIndex] >> bitInByte) & 0x01) {
        value |= (1UL << i);
      }
    }

    bitOffset_ += bitCount;
    return value;
  }

  int32_t readSigned(uint8_t bitCount) {
    const uint32_t raw = readUnsigned(bitCount);
    if ((raw & (1UL << (bitCount - 1))) == 0) {
      return static_cast<int32_t>(raw);
    }

    const uint32_t signExtended = raw | (~0UL << bitCount);
    return static_cast<int32_t>(signExtended);
  }

 private:
  const uint8_t *data_;
  size_t length_;
  size_t bitOffset_ = 0;
};
}  // namespace

class VictronMonitor::AdvertisedDeviceCallbacks : public NimBLEAdvertisedDeviceCallbacks {
 public:
  explicit AdvertisedDeviceCallbacks(VictronMonitor &monitor) : monitor_(monitor) {}

  void onResult(NimBLEAdvertisedDevice *advertisedDevice) override {
    if (advertisedDevice == nullptr || !advertisedDevice->haveManufacturerData()) {
      return;
    }

    const std::string manufacturerData = advertisedDevice->getManufacturerData();
    if (manufacturerData.empty()) {
      return;
    }

    monitor_.handleAdvertisement(String(advertisedDevice->getAddress().toString().c_str()),
                                 advertisedDevice->getRSSI(),
                                 reinterpret_cast<const uint8_t *>(manufacturerData.data()),
                                 manufacturerData.size());
  }

 private:
  VictronMonitor &monitor_;
};

void VictronMonitor::begin() {
  if (!enabled() || initialized_) {
    return;
  }

  normalizedTargetAddress_ = normalizeAddress(VICTRON_MAC_ADDRESS);
  if (normalizedTargetAddress_.isEmpty()) {
    logVictronLine("Victron monitor disabled because MAC address is empty");
    return;
  }

  NimBLEDevice::init("");
  NimBLEScan *scan = NimBLEDevice::getScan();
  scan->setAdvertisedDeviceCallbacks(new AdvertisedDeviceCallbacks(*this), true);
  scan->setActiveScan(false);
  scan->setInterval(160);
  scan->setWindow(80);
  initialized_ = true;
  logVictronLine("BLE scan initialized for Victron charger telemetry");
}

void VictronMonitor::poll() {
  if (!initialized_) {
    return;
  }

  const unsigned long now = millis();
  if (now - lastScanStartedMs_ < VICTRON_SCAN_INTERVAL_MS) {
    return;
  }

  lastScanStartedMs_ = now;
  NimBLEScan *scan = NimBLEDevice::getScan();
  scan->start(VICTRON_SCAN_DURATION_SECONDS, false);
  scan->clearResults();
}

bool VictronMonitor::enabled() const {
  return VICTRON_ENABLED;
}

bool VictronMonitor::hasTelemetry() const {
  return telemetry_.valid;
}

bool VictronMonitor::hasPendingPublish() const {
  return telemetry_.hasPendingPublish;
}

bool VictronMonitor::isStale(unsigned long nowMs) const {
  return !telemetry_.valid || (nowMs - telemetry_.lastUpdateMs > VICTRON_STALE_TIMEOUT_MS);
}

const VictronTelemetry &VictronMonitor::telemetry() const {
  return telemetry_;
}

void VictronMonitor::markPublished() {
  telemetry_.hasPendingPublish = false;
}

const char *VictronMonitor::chargeStateName(uint8_t stateCode) {
  switch (stateCode) {
    case 0:
      return "off";
    case 2:
      return "fault";
    case 3:
      return "bulk";
    case 4:
      return "absorption";
    case 5:
      return "float";
    case 6:
      return "storage";
    case 7:
      return "equalize";
    case 245:
      return "starting";
    case 247:
      return "recondition";
    case 252:
      return "external_control";
    default:
      return "unknown";
  }
}

void VictronMonitor::handleAdvertisement(const String &address, int rssi,
                                         const uint8_t *data, size_t length) {
  if (!matchesTargetAddress(address)) {
    return;
  }

  VictronTelemetry candidate;
  if (!decodePacket(data, length, candidate)) {
    return;
  }

  candidate.rssi = rssi;
  const bool changed = !telemetry_.valid || !sameTelemetryPayload(candidate, telemetry_);
  candidate.valid = true;
  candidate.hasPendingPublish = changed;
  candidate.lastUpdateMs = millis();
  telemetry_ = candidate;
}

bool VictronMonitor::matchesTargetAddress(const String &address) const {
  return normalizeAddress(address) == normalizedTargetAddress_;
}

bool VictronMonitor::decodePacket(const uint8_t *data, size_t length,
                                  VictronTelemetry &telemetry) {
  if (length < kVictronHeaderLength + kVictronCipherLength) {
    return false;
  }

  size_t offset = 0;
  if (length >= 2 && (static_cast<uint16_t>(data[1]) << 8 | data[0]) == kVictronCompanyId) {
    offset = 2;
  }

  if (length < offset + kVictronHeaderLength + kVictronCipherLength) {
    return false;
  }

  if (data[offset] != kVictronInstantReadoutRecord) {
    return false;
  }

  if (data[offset + 4] != kVictronSolarChargerRecord) {
    return false;
  }

  uint8_t key[kVictronAesKeyLength];
  if (!decodeHexKey(key, sizeof(key))) {
    static bool loggedInvalidKey = false;
    if (!loggedInvalidKey) {
      loggedInvalidKey = true;
      logVictronLine("Victron key is not a 32-character hex string");
    }
    return false;
  }

  if (data[offset + 7] != key[0]) {
    return false;
  }

  uint8_t nonceCounter[kVictronNonceLength] = {};
  nonceCounter[0] = data[offset + 5];
  nonceCounter[1] = data[offset + 6];

  uint8_t streamBlock[16] = {};
  uint8_t decrypted[kVictronCipherLength] = {};
  size_t ncOff = 0;

  mbedtls_aes_context aesContext;
  mbedtls_aes_init(&aesContext);
  const int setKeyResult = mbedtls_aes_setkey_enc(&aesContext, key, 128);
  if (setKeyResult != 0) {
    mbedtls_aes_free(&aesContext);
    return false;
  }

  const int cryptResult = mbedtls_aes_crypt_ctr(
      &aesContext, kVictronCipherLength, &ncOff, nonceCounter, streamBlock,
      data + offset + kVictronHeaderLength, decrypted);
  mbedtls_aes_free(&aesContext);
  if (cryptResult != 0) {
    return false;
  }

  BitReader reader(decrypted, sizeof(decrypted));
  const uint32_t chargeState = reader.readUnsigned(8);
  const uint32_t chargerError = reader.readUnsigned(8);
  const int32_t batteryVoltageRaw = reader.readSigned(16);
  const int32_t chargeCurrentRaw = reader.readSigned(16);
  const uint32_t yieldTodayRaw = reader.readUnsigned(16);
  const uint32_t solarPowerRaw = reader.readUnsigned(16);
  const uint32_t loadCurrentRaw = reader.readUnsigned(9);

  telemetry.chargeStateCode = static_cast<uint8_t>(chargeState);
  telemetry.chargerErrorCode = static_cast<uint8_t>(chargerError);
  telemetry.batteryVoltage =
      batteryVoltageRaw == kInvalidSigned16 ? NAN : batteryVoltageRaw / 100.0f;
  telemetry.chargeCurrent =
      chargeCurrentRaw == kInvalidSigned16 ? NAN : chargeCurrentRaw / 10.0f;
  telemetry.yieldTodayWh =
      yieldTodayRaw == kInvalidUnsigned16 ? NAN : yieldTodayRaw * 10.0f;
  telemetry.solarPower =
      solarPowerRaw == kInvalidUnsigned16 ? NAN : static_cast<float>(solarPowerRaw);
  telemetry.loadCurrent =
      loadCurrentRaw == kInvalidUnsigned9 ? NAN : loadCurrentRaw / 10.0f;
  return true;
}

bool VictronMonitor::decodeHexKey(uint8_t *keyBytes, size_t keyLength) const {
  const String key = VICTRON_KEY;
  if (key.length() != keyLength * 2) {
    return false;
  }

  for (size_t i = 0; i < keyLength; ++i) {
    const char high = key.charAt(i * 2);
    const char low = key.charAt(i * 2 + 1);
    auto hexNibble = [](char value) -> int {
      if (value >= '0' && value <= '9') {
        return value - '0';
      }
      if (value >= 'a' && value <= 'f') {
        return value - 'a' + 10;
      }
      if (value >= 'A' && value <= 'F') {
        return value - 'A' + 10;
      }
      return -1;
    };

    const int highNibble = hexNibble(high);
    const int lowNibble = hexNibble(low);
    if (highNibble < 0 || lowNibble < 0) {
      return false;
    }

    keyBytes[i] = static_cast<uint8_t>((highNibble << 4) | lowNibble);
  }

  return true;
}

String VictronMonitor::normalizeAddress(const String &value) {
  String normalized;
  normalized.reserve(value.length());
  for (size_t i = 0; i < value.length(); ++i) {
    const char c = value.charAt(i);
    if (isxdigit(static_cast<unsigned char>(c))) {
      normalized += static_cast<char>(toupper(static_cast<unsigned char>(c)));
    }
  }
  return normalized;
}
