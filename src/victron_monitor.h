#pragma once

#include <Arduino.h>

struct VictronTelemetry {
  bool valid = false;
  bool hasPendingPublish = false;
  uint8_t chargeStateCode = 0xFF;
  uint8_t chargerErrorCode = 0xFF;
  float batteryVoltage = NAN;
  float chargeCurrent = NAN;
  float solarPower = NAN;
  float yieldTodayWh = NAN;
  float loadCurrent = NAN;
  int rssi = 0;
  unsigned long lastUpdateMs = 0;
};

class VictronMonitor {
 public:
  void begin();
  void poll();

  bool enabled() const;
  bool hasTelemetry() const;
  bool hasPendingPublish() const;
  bool isStale(unsigned long nowMs) const;
  const VictronTelemetry &telemetry() const;
  void markPublished();

  static const char *chargeStateName(uint8_t stateCode);

 private:
  class AdvertisedDeviceCallbacks;

  void handleAdvertisement(const String &address, int rssi,
                           const uint8_t *data, size_t length);
  bool matchesTargetAddress(const String &address) const;
  bool decodePacket(const uint8_t *data, size_t length, VictronTelemetry &telemetry);
  bool decodeHexKey(uint8_t *keyBytes, size_t keyLength) const;
  static String normalizeAddress(const String &value);

  bool initialized_ = false;
  unsigned long lastScanStartedMs_ = 0;
  String normalizedTargetAddress_;
  VictronTelemetry telemetry_;
};
