#include <Arduino.h>
#include <math.h>
#include <stdarg.h>
#include <WiFi.h>
#include <Wire.h>

#include <Adafruit_INA219.h>
#include <DallasTemperature.h>
#include <OneWire.h>
#include <PubSubClient.h>

#include "router_box_config.h"
#include "victron_monitor.h"

namespace {
using namespace RouterBoxConfig;

RTC_DATA_ATTR bool desiredRouterPowered = ROUTER_DEFAULT_POWERED;
RTC_DATA_ATTR bool actualRouterPowered = ROUTER_DEFAULT_POWERED;
RTC_DATA_ATTR uint32_t bootCount = 0;
RTC_DATA_ATTR bool routerRestartInProgress = false;
RTC_DATA_ATTR float routerEnergyWh = 0.0f;

WiFiClient wifiClient;
PubSubClient mqttClient(wifiClient);
OneWire oneWire(PIN_ONEWIRE);
DallasTemperature temperatureSensors(&oneWire);
Adafruit_INA219 ina219;
DeviceAddress tempSensorAddresses[2];
bool tempSensorPresent[2] = {false, false};
VictronMonitor victronMonitor;

unsigned long lastPublishMs = 0;
unsigned long lastWifiAttemptMs = 0;
unsigned long lastMqttAttemptMs = 0;
unsigned long lastIna219InitAttemptMs = 0;
unsigned long lastIna219SampleMs = 0;
unsigned long lastRouterTelemetryPublishMs = 0;
unsigned long lastTemperatureInitAttemptMs = 0;
bool ina219Ready = false;
bool temperaturesReady = false;
uint8_t temperatureSensorCount = 0;
uint8_t rawTemperatureDeviceCount = 0;
float latestTempSensorF[2] = {NAN, NAN};
bool tempSensorReadingValid[2] = {false, false};
unsigned long lastTemperatureSampleMs = 0;
unsigned long lastTemperatureReadSuccessMs = 0;
unsigned long lastTemperatureFailureStartedMs = 0;
unsigned long lastIna219ReadSuccessMs = 0;
bool victronLinkStatePublished = false;
bool victronLinkOnline = false;
bool victronRepublishRequested = false;
bool wifiConnectInProgress = false;
unsigned long wifiConnectStartedMs = 0;
unsigned long routerRestartResumeMs = 0;
bool victronTelemetryClearPending = false;
bool telemetryPublishRequested = false;
float latestRouterVoltage = NAN;
float latestRouterCurrentAmps = NAN;
float latestRouterWatts = NAN;
bool routerTelemetryValid = false;
float latestBatteryPercent = NAN;
float smoothedBatteryVoltage = NAN;
bool batterySocValid = false;
constexpr char kTopicAvailability[] = "littlelodge/routerbox/availability";
constexpr char kTopicRelayPinLevel[] = "littlelodge/routerbox/router_relay/pin_level";
constexpr char kTopicTempSensorCount[] = "littlelodge/routerbox/temp/debug/count";
constexpr char kTopicTempSensorRawCount[] = "littlelodge/routerbox/temp/debug/raw_count";
constexpr char kTopicTempSensorValidCount[] = "littlelodge/routerbox/temp/debug/valid_count";
constexpr char kTopicTempSensorState[] = "littlelodge/routerbox/temp/debug/state";
constexpr char kTopicIna219State[] = "littlelodge/routerbox/router/debug/ina219_state";
constexpr char kDiscoveryNodeId[] = "router_box";
constexpr char kVictronDiscoveryNodePrefix[] = "router_box_";
constexpr char kUnitFahrenheit[] = "\xC2\xB0"
                                   "F";
constexpr uint16_t kMqttBufferSize = 1024;
constexpr float kIna219CurrentDeadbandAmps = 0.01f;
constexpr float kIna219PowerDeadbandWatts = 0.05f;
constexpr uint32_t kLocalSensorRetryIntervalMs = 10000;
constexpr uint32_t kIna219SampleIntervalMs = 1000;
constexpr uint32_t kTemperatureSampleIntervalMs = 5000;
constexpr uint16_t kMqttSocketTimeoutSeconds = 1;
constexpr uint32_t kTemperatureReadFailureReinitMs = 15000;
constexpr uint32_t kIna219ReadFailureReinitMs = 15000;

void logLine(const String &message) {
  Serial.println("[router-box] " + message);
}

void logFormatted(const char *format, ...) {
  char buffer[160];
  va_list args;
  va_start(args, format);
  vsnprintf(buffer, sizeof(buffer), format, args);
  va_end(args);
  Serial.print("[router-box] ");
  Serial.println(buffer);
}

bool publishRetained(const char *topic, const char *payload);
bool publishRetained(const char *topic, const String &payload);
bool publishLogged(const char *topic, const char *payload);
bool publishLogged(const char *topic, const String &payload);
bool clearRetained(const char *topic);
bool publishFloat(const char *topic, float value, uint8_t decimals);
bool publishInt(const char *topic, int value);
void publishHomeAssistantDiscovery();
void publishVictronTelemetry(bool forcePublish);
bool ensureMqttConnected();
void initializeIna219();
void initializeTemperatureSensors();
void ensureLocalSensorsInitialized();
void processRouterRestart();
void processTemperatureSensors();
void processRouterTelemetry();
void clearRouterTelemetry();
void publishRouterTelemetry();
void ensureVictronTelemetryCleared();
bool clearVictronTelemetryTopic(const char *topic);
bool hasAnyValidTemperatureReading();
bool ina219ReadingLooksValid(float busVoltage, float shuntMillivolts, float currentMilliamps,
                             float loadVoltage, float currentAmps, float watts);
float estimateBatteryPercent(const VictronTelemetry &telemetry);
void clearBatterySocState();
float clampFloat(float value, float minimum, float maximum);
float interpolateBatteryPercent(float voltage);
float computeBatteryCurrentAssistVolts(const VictronTelemetry &telemetry);

void cancelRouterRestart() {
  routerRestartInProgress = false;
  routerRestartResumeMs = 0;
}

const char *routerPowerStateString() {
  return actualRouterPowered ? "ON" : "OFF";
}

const char *desiredRouterPowerStateString() {
  return desiredRouterPowered ? "ON" : "OFF";
}

String formatDeviceAddress(const DeviceAddress address) {
  char buffer[17];
  for (uint8_t i = 0; i < 8; ++i) {
    snprintf(&buffer[i * 2], sizeof(buffer) - (i * 2), "%02X", address[i]);
  }
  return String(buffer);
}

bool coilLevelFor(bool energized) {
  return energized == RELAY_ACTIVE_HIGH ? HIGH : LOW;
}

bool relayCoilShouldBeEnergized(bool routerPowered) {
  return RELAY_USE_NC_CONTACT ? !routerPowered : routerPowered;
}

void driveRouterPowerOutput(bool routerPowered) {
  actualRouterPowered = routerPowered;
  const bool pinLevel = coilLevelFor(relayCoilShouldBeEnergized(routerPowered));
  digitalWrite(PIN_RELAY, pinLevel);
  logFormatted("Router power output set to %s (desired=%s), GPIO level=%s, digitalRead=%s",
               routerPowerStateString(), desiredRouterPowerStateString(),
               pinLevel ? "HIGH" : "LOW",
               digitalRead(PIN_RELAY) ? "HIGH" : "LOW");

  if (mqttClient.connected()) {
    publishRetained(kTopicRelayPinLevel, pinLevel ? "HIGH" : "LOW");
  }
}

void setRouterPowerState(bool routerPowered) {
  desiredRouterPowered = routerPowered;
  driveRouterPowerOutput(routerPowered);
}

bool publishRetained(const char *topic, const String &payload) {
  return mqttClient.publish(topic, payload.c_str(), true);
}

bool publishRetained(const char *topic, const char *payload) {
  return mqttClient.publish(topic, payload, true);
}

bool publishLogged(const char *topic, const char *payload) {
  const bool success = publishRetained(topic, payload);
  logFormatted("MQTT publish %s topic=%s payload=%s", success ? "ok" : "failed", topic,
               payload);
  return success;
}

bool publishLogged(const char *topic, const String &payload) {
  return publishLogged(topic, payload.c_str());
}

bool clearRetained(const char *topic) {
  const bool success = mqttClient.publish(topic, "", true);
  logFormatted("MQTT clear %s topic=%s", success ? "ok" : "failed", topic);
  return success;
}

bool publishFloat(const char *topic, float value, uint8_t decimals) {
  char payload[24];
  dtostrf(value, 0, decimals, payload);
  return publishLogged(topic, payload);
}

bool publishInt(const char *topic, int value) {
  char payload[16];
  snprintf(payload, sizeof(payload), "%d", value);
  return publishLogged(topic, payload);
}

bool hasAnyValidTemperatureReading() {
  return tempSensorReadingValid[0] || tempSensorReadingValid[1];
}

uint8_t validTemperatureReadingCount() {
  uint8_t count = 0;
  for (uint8_t i = 0; i < 2; ++i) {
    if (tempSensorReadingValid[i]) {
      ++count;
    }
  }
  return count;
}

bool ina219ReadingLooksValid(float busVoltage, float shuntMillivolts, float currentMilliamps,
                             float loadVoltage, float currentAmps, float watts) {
  return !isnan(busVoltage) && !isnan(shuntMillivolts) && !isnan(currentMilliamps) &&
         !isnan(loadVoltage) && !isnan(currentAmps) && !isnan(watts) &&
         !isinf(busVoltage) && !isinf(shuntMillivolts) && !isinf(currentMilliamps) &&
         !isinf(loadVoltage) && !isinf(currentAmps) && !isinf(watts);
}

float clampFloat(float value, float minimum, float maximum) {
  if (value < minimum) {
    return minimum;
  }
  if (value > maximum) {
    return maximum;
  }
  return value;
}

float computeBatteryCurrentAssistVolts(const VictronTelemetry &telemetry) {
  if (!BATTERY_SOC_USE_CURRENT_ASSIST) {
    return 0.0f;
  }

  float netCurrentAmps = 0.0f;
  bool hasCurrentSample = false;
  if (!isnan(telemetry.chargeCurrent)) {
    netCurrentAmps += telemetry.chargeCurrent;
    hasCurrentSample = true;
  }
  if (!isnan(telemetry.loadCurrent)) {
    netCurrentAmps -= telemetry.loadCurrent;
    hasCurrentSample = true;
  }

  if (!hasCurrentSample || fabsf(netCurrentAmps) < BATTERY_SOC_CURRENT_DEADBAND_AMPS) {
    return 0.0f;
  }

  return netCurrentAmps * BATTERY_SOC_CURRENT_ASSIST_VOLTS_PER_AMP;
}

float interpolateBatteryPercent(float voltage) {
  struct VoltageSocPoint {
    float voltage;
    float percent;
  };

  // Approximate 4S LiFePO4 open-circuit curve tuned for a 12V pack.
  static constexpr VoltageSocPoint kBatterySocCurve[] = {
      {14.40f, 100.0f}, {13.60f, 100.0f}, {13.45f, 99.0f}, {13.35f, 95.0f},
      {13.30f, 90.0f},  {13.20f, 70.0f},  {13.13f, 55.0f}, {13.07f, 40.0f},
      {13.00f, 30.0f},  {12.90f, 20.0f},  {12.80f, 12.0f}, {12.60f, 5.0f},
      {12.00f, 0.0f},
  };

  if (voltage >= kBatterySocCurve[0].voltage) {
    return 100.0f;
  }

  constexpr size_t kCurvePointCount = sizeof(kBatterySocCurve) / sizeof(kBatterySocCurve[0]);
  for (size_t i = 1; i < kCurvePointCount; ++i) {
    const VoltageSocPoint &upper = kBatterySocCurve[i - 1];
    const VoltageSocPoint &lower = kBatterySocCurve[i];
    if (voltage > lower.voltage) {
      const float span = upper.voltage - lower.voltage;
      if (span <= 0.0f) {
        return lower.percent;
      }

      const float ratio = (voltage - lower.voltage) / span;
      return lower.percent + (upper.percent - lower.percent) * ratio;
    }
  }

  return 0.0f;
}

void clearBatterySocState() {
  smoothedBatteryVoltage = NAN;
  latestBatteryPercent = NAN;
  batterySocValid = false;
}

float estimateBatteryPercent(const VictronTelemetry &telemetry) {
  if (!BATTERY_SOC_ENABLED || isnan(telemetry.batteryVoltage)) {
    clearBatterySocState();
    return NAN;
  }

  float voltage = telemetry.batteryVoltage;
  const float assistedVoltage = voltage - computeBatteryCurrentAssistVolts(telemetry);
  if (isnan(smoothedBatteryVoltage)) {
    smoothedBatteryVoltage = assistedVoltage;
  } else if (fabsf(assistedVoltage - smoothedBatteryVoltage) >= BATTERY_SOC_VOLTAGE_DEADBAND) {
    const float alpha = clampFloat(BATTERY_SOC_SMOOTHING_ALPHA, 0.0f, 1.0f);
    smoothedBatteryVoltage += (assistedVoltage - smoothedBatteryVoltage) * alpha;
  }

  latestBatteryPercent = clampFloat(interpolateBatteryPercent(smoothedBatteryVoltage), 0.0f,
                                    100.0f);
  batterySocValid = true;
  return latestBatteryPercent;
}

String discoveryDeviceJson() {
  return String("{\"ids\":[\"") + MQTT_CLIENT_ID +
         "\"],\"name\":\"Router Box\",\"mf\":\"Jason Post\",\"mdl\":\"Seeed XIAO ESP32C3 Router Box\"}";
}

String discoveryVictronDeviceJson() {
  return String("{\"ids\":[\"") + MQTT_CLIENT_ID + "_" + VICTRON_DEVICE_ID +
         "\"],\"name\":\"" + VICTRON_DEVICE_NAME +
         "\",\"mf\":\"Victron Energy\",\"mdl\":\"SmartSolar BLE\"}";
}

String discoveryAvailabilityJson() {
  return String("\"availability_topic\":\"") + kTopicAvailability +
         "\",\"payload_available\":\"online\",\"payload_not_available\":\"offline\"";
}

void publishDiscoveryConfig(const char *component, const char *objectId,
                            const String &payload) {
  const String topic = String("homeassistant/") + component + "/" + kDiscoveryNodeId +
                       "/" + objectId + "/config";
  publishLogged(topic.c_str(), payload);
}

void publishVictronDiscoveryConfig(const char *component, const char *objectId,
                                   const String &payload) {
  const String topic = String("homeassistant/") + component + "/" +
                       kVictronDiscoveryNodePrefix + VICTRON_DEVICE_ID + "/" +
                       objectId + "/config";
  publishLogged(topic.c_str(), payload);
}

void publishHomeAssistantDiscovery() {
  const String device = discoveryDeviceJson();
  const String victronDevice = discoveryVictronDeviceJson();
  const String availability = discoveryAvailabilityJson();

  publishDiscoveryConfig(
      "switch",
      "router_power",
      String("{\"name\":\"Router Power\",\"uniq_id\":\"router_box_router_power\",") +
          "\"stat_t\":\"" + TOPIC_RELAY_STATE + "\",\"cmd_t\":\"" + TOPIC_RELAY_SET +
          "\",\"pl_on\":\"ON\",\"pl_off\":\"OFF\"," + availability +
          ",\"dev\":" + device + "}");

  publishDiscoveryConfig(
      "button",
      "router_restart",
      String("{\"name\":\"Router Restart\",\"uniq_id\":\"router_box_router_restart\",") +
          "\"cmd_t\":\"" + TOPIC_RELAY_SET + "\",\"pl_prs\":\"RESTART\"," +
          availability + ",\"dev\":" + device + "}");

  publishDiscoveryConfig(
      "sensor",
      "temp_box1",
      String("{\"name\":\"Box 1 Temperature\",\"uniq_id\":\"router_box_temp_box1\",") +
          "\"stat_t\":\"" + TOPIC_TEMP_BOX1 + "\",\"dev_cla\":\"temperature\"," +
          "\"unit_of_meas\":\"" + kUnitFahrenheit + "\",\"stat_cla\":\"measurement\"," +
          availability + ",\"dev\":" + device + "}");

  publishDiscoveryConfig(
      "sensor",
      "temp_box2",
      String("{\"name\":\"Box 2 Temperature\",\"uniq_id\":\"router_box_temp_box2\",") +
          "\"stat_t\":\"" + TOPIC_TEMP_BOX2 + "\",\"dev_cla\":\"temperature\"," +
          "\"unit_of_meas\":\"" + kUnitFahrenheit + "\",\"stat_cla\":\"measurement\"," +
          availability + ",\"dev\":" + device + "}");

  publishDiscoveryConfig(
      "sensor",
      "temp_debug_raw_count",
      String("{\"name\":\"Temperature Raw Count\",\"uniq_id\":\"router_box_temp_raw_count\",") +
          "\"stat_t\":\"" + kTopicTempSensorRawCount +
          "\",\"icon\":\"mdi:counter\",\"stat_cla\":\"measurement\"," +
          availability + ",\"dev\":" + device + "}");

  publishDiscoveryConfig(
      "sensor",
      "temp_debug_count",
      String("{\"name\":\"Temperature Usable Count\",\"uniq_id\":\"router_box_temp_count\",") +
          "\"stat_t\":\"" + kTopicTempSensorCount +
          "\",\"icon\":\"mdi:counter\",\"stat_cla\":\"measurement\"," +
          availability + ",\"dev\":" + device + "}");

  publishDiscoveryConfig(
      "sensor",
      "temp_debug_valid_count",
      String("{\"name\":\"Temperature Valid Count\",\"uniq_id\":\"router_box_temp_valid_count\",") +
          "\"stat_t\":\"" + kTopicTempSensorValidCount +
          "\",\"icon\":\"mdi:counter\",\"stat_cla\":\"measurement\"," +
          availability + ",\"dev\":" + device + "}");

  publishDiscoveryConfig(
      "sensor",
      "temp_debug_state",
      String("{\"name\":\"Temperature State\",\"uniq_id\":\"router_box_temp_state\",") +
          "\"stat_t\":\"" + kTopicTempSensorState +
          "\",\"icon\":\"mdi:thermometer-alert\"," + availability +
          ",\"dev\":" + device + "}");

  publishDiscoveryConfig(
      "sensor",
      "router_voltage",
      String("{\"name\":\"Router Voltage\",\"uniq_id\":\"router_box_router_voltage\",") +
          "\"stat_t\":\"" + TOPIC_ROUTER_VOLTS +
          "\",\"dev_cla\":\"voltage\",\"unit_of_meas\":\"V\",\"stat_cla\":\"measurement\"," +
          availability + ",\"dev\":" + device + "}");

  publishDiscoveryConfig(
      "sensor",
      "router_current",
      String("{\"name\":\"Router Current\",\"uniq_id\":\"router_box_router_current\",") +
          "\"stat_t\":\"" + TOPIC_ROUTER_AMPS +
          "\",\"dev_cla\":\"current\",\"unit_of_meas\":\"A\",\"stat_cla\":\"measurement\"," +
          availability + ",\"dev\":" + device + "}");

  publishDiscoveryConfig(
      "sensor",
      "router_power",
      String("{\"name\":\"Router Power Draw\",\"uniq_id\":\"router_box_router_power_draw\",") +
          "\"stat_t\":\"" + TOPIC_ROUTER_WATTS +
          "\",\"dev_cla\":\"power\",\"unit_of_meas\":\"W\",\"stat_cla\":\"measurement\"," +
          availability + ",\"dev\":" + device + "}");

  publishDiscoveryConfig(
      "sensor",
      "router_energy",
      String("{\"name\":\"Router Energy\",\"uniq_id\":\"router_box_router_energy\",") +
          "\"stat_t\":\"" + TOPIC_ROUTER_WH +
          "\",\"unit_of_meas\":\"Wh\",\"stat_cla\":\"total_increasing\"," + availability +
          ",\"dev\":" + device + "}");

  if (VICTRON_ENABLED) {
    publishVictronDiscoveryConfig(
        "sensor",
        "victron_battery_voltage",
        (String("{\"name\":\"") + VICTRON_DEVICE_NAME +
         " Battery Voltage\",\"uniq_id\":\"" + VICTRON_DEVICE_ID +
         "_battery_voltage\",") +
            "\"stat_t\":\"" + TOPIC_VICTRON_BATTERY_VOLTS +
            "\",\"dev_cla\":\"voltage\",\"unit_of_meas\":\"V\",\"stat_cla\":\"measurement\"," +
            availability + ",\"dev\":" + victronDevice + "}");

    if (BATTERY_SOC_ENABLED) {
      publishVictronDiscoveryConfig(
          "sensor",
          "victron_battery_percent",
          (String("{\"name\":\"") + VICTRON_DEVICE_NAME +
           " Battery Level\",\"uniq_id\":\"" + VICTRON_DEVICE_ID +
           "_battery_percent\",") +
              "\"stat_t\":\"" + TOPIC_VICTRON_BATTERY_PERCENT +
              "\",\"dev_cla\":\"battery\",\"unit_of_meas\":\"%\",\"stat_cla\":\"measurement\"," +
              availability + ",\"dev\":" + victronDevice + "}");
    }

    publishVictronDiscoveryConfig(
        "sensor",
        "victron_charge_current",
        (String("{\"name\":\"") + VICTRON_DEVICE_NAME +
         " Charge Current\",\"uniq_id\":\"" + VICTRON_DEVICE_ID +
         "_charge_current\",") +
            "\"stat_t\":\"" + TOPIC_VICTRON_CHARGE_AMPS +
            "\",\"dev_cla\":\"current\",\"unit_of_meas\":\"A\",\"stat_cla\":\"measurement\"," +
            availability + ",\"dev\":" + victronDevice + "}");

    publishVictronDiscoveryConfig(
        "sensor",
        "victron_solar_power",
        (String("{\"name\":\"") + VICTRON_DEVICE_NAME +
         " Solar Power\",\"uniq_id\":\"" + VICTRON_DEVICE_ID +
         "_solar_power\",") +
            "\"stat_t\":\"" + TOPIC_VICTRON_SOLAR_WATTS +
            "\",\"dev_cla\":\"power\",\"unit_of_meas\":\"W\",\"stat_cla\":\"measurement\"," +
            availability + ",\"dev\":" + victronDevice + "}");

    publishVictronDiscoveryConfig(
        "sensor",
        "victron_yield_today",
        (String("{\"name\":\"") + VICTRON_DEVICE_NAME +
         " Yield Today\",\"uniq_id\":\"" + VICTRON_DEVICE_ID +
         "_yield_today\",") +
            "\"stat_t\":\"" + TOPIC_VICTRON_YIELD_TODAY_WH +
            "\",\"unit_of_meas\":\"Wh\",\"stat_cla\":\"total_increasing\"," +
            availability + ",\"dev\":" + victronDevice + "}");

    publishVictronDiscoveryConfig(
        "sensor",
        "victron_load_current",
        (String("{\"name\":\"") + VICTRON_DEVICE_NAME +
         " Load Current\",\"uniq_id\":\"" + VICTRON_DEVICE_ID +
         "_load_current\",") +
            "\"stat_t\":\"" + TOPIC_VICTRON_LOAD_AMPS +
            "\",\"dev_cla\":\"current\",\"unit_of_meas\":\"A\",\"stat_cla\":\"measurement\"," +
            availability + ",\"dev\":" + victronDevice + "}");

    publishVictronDiscoveryConfig(
        "sensor",
        "victron_charge_state",
        (String("{\"name\":\"") + VICTRON_DEVICE_NAME +
         " Charge State\",\"uniq_id\":\"" + VICTRON_DEVICE_ID +
         "_charge_state\",") +
            "\"stat_t\":\"" + TOPIC_VICTRON_CHARGE_STATE +
            "\",\"icon\":\"mdi:solar-power\"," + availability +
            ",\"dev\":" + victronDevice + "}");

    publishVictronDiscoveryConfig(
        "sensor",
        "victron_error_code",
        (String("{\"name\":\"") + VICTRON_DEVICE_NAME +
         " Error Code\",\"uniq_id\":\"" + VICTRON_DEVICE_ID +
         "_error_code\",") +
            "\"stat_t\":\"" + TOPIC_VICTRON_ERROR_CODE +
            "\",\"stat_cla\":\"measurement\",\"icon\":\"mdi:alert-circle-outline\"," +
            availability + ",\"dev\":" + victronDevice + "}");

    publishVictronDiscoveryConfig(
        "sensor",
        "victron_rssi",
        (String("{\"name\":\"") + VICTRON_DEVICE_NAME +
         " BLE RSSI\",\"uniq_id\":\"" + VICTRON_DEVICE_ID +
         "_rssi\",") +
            "\"stat_t\":\"" + TOPIC_VICTRON_RSSI +
            "\",\"dev_cla\":\"signal_strength\",\"unit_of_meas\":\"dBm\",\"stat_cla\":\"measurement\"," +
            availability + ",\"dev\":" + victronDevice + "}");

    publishVictronDiscoveryConfig(
        "binary_sensor",
        "victron_link",
        (String("{\"name\":\"") + VICTRON_DEVICE_NAME +
         " Link\",\"uniq_id\":\"" + VICTRON_DEVICE_ID +
         "_link\",") +
            "\"stat_t\":\"" + TOPIC_VICTRON_LINK_STATE +
            "\",\"pl_on\":\"online\",\"pl_off\":\"stale\"," + availability +
            ",\"dev\":" + victronDevice + "}");
  }
}

void publishRelayState() {
  publishRetained(TOPIC_RELAY_STATE, routerPowerStateString());
}

void publishAvailability(const char *status) {
  publishRetained(kTopicAvailability, status);
}

void publishStatus(const char *status) {
  publishRetained(TOPIC_STATUS, status);
}

void publishVictronLinkState(bool online) {
  victronLinkStatePublished = true;
  victronLinkOnline = online;
  publishRetained(TOPIC_VICTRON_LINK_STATE, online ? "online" : "stale");
}

void clearVictronTelemetryTopics() {
  bool cleared = true;
  cleared = clearVictronTelemetryTopic(TOPIC_VICTRON_BATTERY_VOLTS) && cleared;
  if (BATTERY_SOC_ENABLED) {
    cleared = clearVictronTelemetryTopic(TOPIC_VICTRON_BATTERY_PERCENT) && cleared;
    clearBatterySocState();
  }
  cleared = clearVictronTelemetryTopic(TOPIC_VICTRON_CHARGE_AMPS) && cleared;
  cleared = clearVictronTelemetryTopic(TOPIC_VICTRON_SOLAR_WATTS) && cleared;
  cleared = clearVictronTelemetryTopic(TOPIC_VICTRON_YIELD_TODAY_WH) && cleared;
  cleared = clearVictronTelemetryTopic(TOPIC_VICTRON_LOAD_AMPS) && cleared;
  cleared = clearVictronTelemetryTopic(TOPIC_VICTRON_CHARGE_STATE) && cleared;
  cleared = clearVictronTelemetryTopic(TOPIC_VICTRON_CHARGE_STATE_CODE) && cleared;
  cleared = clearVictronTelemetryTopic(TOPIC_VICTRON_ERROR_CODE) && cleared;
  cleared = clearVictronTelemetryTopic(TOPIC_VICTRON_RSSI) && cleared;
  victronTelemetryClearPending = !cleared;
}

bool clearVictronTelemetryTopic(const char *topic) {
  const bool success = clearRetained(topic);
  if (!success) {
    victronTelemetryClearPending = true;
  }
  return success;
}

void ensureVictronTelemetryCleared() {
  if (!victronTelemetryClearPending || !mqttClient.connected()) {
    return;
  }

  logLine("Retrying stale Victron telemetry clears");
  clearVictronTelemetryTopics();
}

void publishVictronTelemetry(bool forcePublish) {
  if (!VICTRON_ENABLED || !ensureMqttConnected()) {
    return;
  }

  const unsigned long now = millis();
  const bool stale = victronMonitor.isStale(now);
  if (stale) {
    if (!victronLinkStatePublished || victronLinkOnline) {
      publishVictronLinkState(false);
      clearVictronTelemetryTopics();
    }
    victronRepublishRequested = false;
    ensureVictronTelemetryCleared();
    return;
  }

  if (!forcePublish && !victronRepublishRequested && !victronMonitor.hasPendingPublish()) {
    return;
  }

  if (!victronLinkStatePublished || !victronLinkOnline) {
    publishVictronLinkState(true);
  }
  victronTelemetryClearPending = false;
  victronRepublishRequested = false;

  const VictronTelemetry &telemetry = victronMonitor.telemetry();
  if (!isnan(telemetry.batteryVoltage)) {
    publishFloat(TOPIC_VICTRON_BATTERY_VOLTS, telemetry.batteryVoltage, 2);
  } else {
    clearVictronTelemetryTopic(TOPIC_VICTRON_BATTERY_VOLTS);
  }
  const float batteryPercent = estimateBatteryPercent(telemetry);
  if (!isnan(batteryPercent)) {
    publishFloat(TOPIC_VICTRON_BATTERY_PERCENT, batteryPercent, 0);
  } else if (BATTERY_SOC_ENABLED) {
    clearVictronTelemetryTopic(TOPIC_VICTRON_BATTERY_PERCENT);
  }
  if (!isnan(telemetry.chargeCurrent)) {
    publishFloat(TOPIC_VICTRON_CHARGE_AMPS, telemetry.chargeCurrent, 1);
  } else {
    clearVictronTelemetryTopic(TOPIC_VICTRON_CHARGE_AMPS);
  }
  if (!isnan(telemetry.solarPower)) {
    publishFloat(TOPIC_VICTRON_SOLAR_WATTS, telemetry.solarPower, 0);
  } else {
    clearVictronTelemetryTopic(TOPIC_VICTRON_SOLAR_WATTS);
  }
  if (!isnan(telemetry.yieldTodayWh)) {
    publishFloat(TOPIC_VICTRON_YIELD_TODAY_WH, telemetry.yieldTodayWh, 0);
  } else {
    clearVictronTelemetryTopic(TOPIC_VICTRON_YIELD_TODAY_WH);
  }
  if (!isnan(telemetry.loadCurrent)) {
    publishFloat(TOPIC_VICTRON_LOAD_AMPS, telemetry.loadCurrent, 1);
  } else {
    clearVictronTelemetryTopic(TOPIC_VICTRON_LOAD_AMPS);
  }

  publishRetained(TOPIC_VICTRON_CHARGE_STATE,
                  VictronMonitor::chargeStateName(telemetry.chargeStateCode));
  publishInt(TOPIC_VICTRON_CHARGE_STATE_CODE, telemetry.chargeStateCode);
  publishInt(TOPIC_VICTRON_ERROR_CODE, telemetry.chargerErrorCode);
  publishInt(TOPIC_VICTRON_RSSI, telemetry.rssi);
  victronMonitor.markPublished();
}

void restartRouter() {
  if (routerRestartInProgress) {
    logLine("Restart already in progress");
    return;
  }

  logLine("Restart command received, cycling router power");
  desiredRouterPowered = true;
  driveRouterPowerOutput(false);
  publishRelayState();
  publishStatus("router_restarting");
  routerRestartInProgress = true;
  routerRestartResumeMs = millis() + RESTART_OFF_MS;
}

void processRouterRestart() {
  if (!routerRestartInProgress) {
    return;
  }

  if (static_cast<long>(millis() - routerRestartResumeMs) < 0) {
    return;
  }

  logLine("Restart delay elapsed, restoring router power");
  cancelRouterRestart();
  driveRouterPowerOutput(true);
  publishRelayState();
  publishStatus("router_running");
}

bool ensureWifiConnected() {
  const wl_status_t status = WiFi.status();
  if (status == WL_CONNECTED) {
    if (wifiConnectInProgress) {
      wifiConnectInProgress = false;
      logFormatted("Wi-Fi connected, IP: %s", WiFi.localIP().toString().c_str());
      logFormatted("Subnet mask: %s", WiFi.subnetMask().toString().c_str());
      logFormatted("Gateway: %s", WiFi.gatewayIP().toString().c_str());
      logFormatted("RSSI: %d", WiFi.RSSI());
    }
    return true;
  }

  const unsigned long now = millis();
  if (wifiConnectInProgress) {
    if (now - wifiConnectStartedMs < WIFI_CONNECT_TIMEOUT_MS) {
      return false;
    }

    logFormatted("Wi-Fi connection timed out, status=%d", static_cast<int>(status));
    WiFi.disconnect();
    wifiConnectInProgress = false;
  }

  if (now - lastWifiAttemptMs < MQTT_RECONNECT_INTERVAL_MS) {
    return false;
  }

  lastWifiAttemptMs = now;
  logFormatted("Connecting to Wi-Fi SSID: %s", WIFI_SSID);
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  wifiConnectInProgress = true;
  wifiConnectStartedMs = now;
  return false;
}

bool ensureMqttConnected() {
  if (mqttClient.connected()) {
    return true;
  }

  if (!ensureWifiConnected()) {
    return false;
  }

  const unsigned long now = millis();
  if (now - lastMqttAttemptMs < MQTT_RECONNECT_INTERVAL_MS) {
    return false;
  }

  lastMqttAttemptMs = now;
  logFormatted("Connecting to MQTT broker %s:%u", MQTT_HOST, MQTT_PORT);

  const bool connected = mqttClient.connect(
      MQTT_CLIENT_ID,
      MQTT_USERNAME[0] == '\0' ? nullptr : MQTT_USERNAME,
      MQTT_PASSWORD[0] == '\0' ? nullptr : MQTT_PASSWORD,
      kTopicAvailability,
      1,
      true,
      "offline");

  if (!connected) {
    logFormatted("MQTT connection failed, state=%d", mqttClient.state());
    return false;
  }

  if (!mqttClient.subscribe(TOPIC_RELAY_SET)) {
    logFormatted("MQTT subscribe failed for %s", TOPIC_RELAY_SET);
    mqttClient.disconnect();
    return false;
  }

  logFormatted("MQTT connected and subscribed to %s", TOPIC_RELAY_SET);
  victronLinkStatePublished = false;
  victronLinkOnline = false;
  victronRepublishRequested = VICTRON_ENABLED;
  publishAvailability("online");
  publishHomeAssistantDiscovery();
  publishRelayState();
  publishStatus(routerRestartInProgress ? "router_restarting"
                                        : (actualRouterPowered ? "router_running"
                                                               : "router_off"));
  telemetryPublishRequested = true;
  return true;
}

void publishTelemetry() {
  if (!ensureMqttConnected()) {
    return;
  }

  ensureLocalSensorsInitialized();
  processTemperatureSensors();
  processRouterTelemetry();

  publishInt(kTopicTempSensorCount, temperatureSensorCount);
  publishInt(kTopicTempSensorRawCount, rawTemperatureDeviceCount);
  publishInt(kTopicTempSensorValidCount, validTemperatureReadingCount());

  if (temperaturesReady) {
    publishLogged(kTopicTempSensorState,
                  hasAnyValidTemperatureReading() ? "ready" : "waiting_for_valid_read");

    const char *const tempTopics[2] = {TOPIC_TEMP_BOX1, TOPIC_TEMP_BOX2};
    for (uint8_t i = 0; i < 2; ++i) {
      if (!tempSensorPresent[i]) {
        clearRetained(tempTopics[i]);
        continue;
      }

      if (tempSensorReadingValid[i]) {
        publishFloat(tempTopics[i], latestTempSensorF[i], 2);
      } else {
        clearRetained(tempTopics[i]);
      }
    }
  } else {
    logLine("Skipping DS18B20 publish because no sensors were initialized");
    publishLogged(kTopicTempSensorState, "not_ready");
    clearRetained(TOPIC_TEMP_BOX1);
    clearRetained(TOPIC_TEMP_BOX2);
  }

  if (routerTelemetryValid) {
    publishLogged(kTopicIna219State, "ready");
    publishRouterTelemetry();
  } else if (ina219Ready) {
    publishLogged(kTopicIna219State, "waiting_for_valid_read");
    clearRouterTelemetry();
  } else {
    logLine("Skipping INA219 publish because sensor was not initialized");
    publishLogged(kTopicIna219State, "not_ready");
    clearRouterTelemetry();
  }

  publishVictronTelemetry(true);
  publishRelayState();
}

void clearRouterTelemetry() {
  clearRetained(TOPIC_ROUTER_VOLTS);
  clearRetained(TOPIC_ROUTER_AMPS);
  clearRetained(TOPIC_ROUTER_WATTS);
  clearRetained(TOPIC_ROUTER_WH);
}

void publishRouterTelemetry() {
  publishFloat(TOPIC_ROUTER_VOLTS, latestRouterVoltage, 3);
  publishFloat(TOPIC_ROUTER_AMPS, latestRouterCurrentAmps, 3);
  publishFloat(TOPIC_ROUTER_WATTS, latestRouterWatts, 3);
  publishFloat(TOPIC_ROUTER_WH, routerEnergyWh, 3);
}

void processRouterTelemetry() {
  if (!ina219Ready) {
    routerTelemetryValid = false;
    lastIna219SampleMs = 0;
    return;
  }

  const unsigned long now = millis();
  if (lastIna219SampleMs != 0 && now - lastIna219SampleMs < kIna219SampleIntervalMs) {
    return;
  }

  const float busVoltage = ina219.getBusVoltage_V();
  const float shuntMillivolts = ina219.getShuntVoltage_mV();
  const float currentMilliamps = ina219.getCurrent_mA();
  const float loadVoltage = busVoltage + (shuntMillivolts / 1000.0f);
  float currentAmps = currentMilliamps / 1000.0f;
  float watts = loadVoltage * currentAmps;

  if (!ina219ReadingLooksValid(busVoltage, shuntMillivolts, currentMilliamps, loadVoltage,
                               currentAmps, watts)) {
    logLine("INA219 read failed sanity check");
    ina219Ready = false;
    routerTelemetryValid = false;
    lastIna219SampleMs = 0;
    return;
  }

  if (fabsf(currentAmps) < kIna219CurrentDeadbandAmps) {
    currentAmps = 0.0f;
  }
  if (fabsf(watts) < kIna219PowerDeadbandWatts) {
    watts = 0.0f;
  }

  if (lastIna219SampleMs != 0 && routerTelemetryValid) {
    const float elapsedHours = static_cast<float>(now - lastIna219SampleMs) / 3600000.0f;
    routerEnergyWh += latestRouterWatts * elapsedHours;
  }

  latestRouterVoltage = loadVoltage;
  latestRouterCurrentAmps = currentAmps;
  latestRouterWatts = watts;
  routerTelemetryValid = true;
  lastIna219ReadSuccessMs = now;
  lastIna219SampleMs = now;

  logFormatted("INA219 loadV=%.3f currentA=%.3f watts=%.3f wh=%.3f", latestRouterVoltage,
               latestRouterCurrentAmps, latestRouterWatts, routerEnergyWh);

  if (mqttClient.connected() &&
      (lastRouterTelemetryPublishMs == 0 ||
       now - lastRouterTelemetryPublishMs >= kIna219SampleIntervalMs)) {
    publishLogged(kTopicIna219State, "ready");
    publishRouterTelemetry();
    lastRouterTelemetryPublishMs = now;
  }
}

void handleRelayCommand(const String &commandRaw) {
  String command = commandRaw;
  command.trim();
  command.toUpperCase();
  logLine(String("MQTT command received: ") + command);

  if (command == "ON") {
    cancelRouterRestart();
    setRouterPowerState(true);
    publishRelayState();
    publishStatus("router_running");
    return;
  }

  if (command == "OFF") {
    cancelRouterRestart();
    setRouterPowerState(false);
    publishRelayState();
    publishStatus("router_off");
    return;
  }

  if (command == "TOGGLE") {
    cancelRouterRestart();
    setRouterPowerState(!desiredRouterPowered);
    publishRelayState();
    publishStatus(desiredRouterPowered ? "router_running" : "router_off");
    return;
  }

  if (command == "RESTART") {
    restartRouter();
  }
}

void mqttCallback(char *topic, byte *payload, unsigned int length) {
  String message;
  message.reserve(length);
  for (unsigned int i = 0; i < length; ++i) {
    message += static_cast<char>(payload[i]);
  }

  if (strcmp(topic, TOPIC_RELAY_SET) == 0) {
    handleRelayCommand(message);
  }
}

void initializeIna219() {
  lastIna219InitAttemptMs = millis();
  Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);
  ina219Ready = ina219.begin();
  if (ina219Ready) {
    lastIna219ReadSuccessMs = millis();
    lastIna219SampleMs = 0;
    lastRouterTelemetryPublishMs = 0;
    routerTelemetryValid = false;
    latestRouterVoltage = NAN;
    latestRouterCurrentAmps = NAN;
    latestRouterWatts = NAN;
  }
  logFormatted("INA219 ready: %s", ina219Ready ? "yes" : "no");
}

void initializeTemperatureSensors() {
  lastTemperatureInitAttemptMs = millis();
  const uint8_t busPresent = oneWire.reset();
  oneWire.reset_search();
  temperatureSensors.begin();
  temperatureSensors.setWaitForConversion(true);
  rawTemperatureDeviceCount = temperatureSensors.getDeviceCount();
  temperatureSensorCount = 0;
  tempSensorPresent[0] = false;
  tempSensorPresent[1] = false;
  tempSensorReadingValid[0] = false;
  tempSensorReadingValid[1] = false;
  latestTempSensorF[0] = NAN;
  latestTempSensorF[1] = NAN;
  lastTemperatureSampleMs = 0;
  lastTemperatureReadSuccessMs = 0;
  lastTemperatureFailureStartedMs = 0;
  temperaturesReady = false;
  logFormatted("DS18B20 bus pin GPIO%u", PIN_ONEWIRE);
  logFormatted("DS18B20 raw 1-wire presence: %s", busPresent ? "detected" : "missing");
  logFormatted("DS18B20 enumerated count: %u", rawTemperatureDeviceCount);

  const uint8_t slotsToProbe = min<uint8_t>(rawTemperatureDeviceCount, 2);
  for (uint8_t i = 0; i < slotsToProbe; ++i) {
    if (!temperatureSensors.getAddress(tempSensorAddresses[i], i)) {
      logFormatted("Failed to read DS18B20 address at slot %u", i);
      continue;
    }

    tempSensorPresent[i] = true;
    ++temperatureSensorCount;
    temperaturesReady = true;
    temperatureSensors.setResolution(tempSensorAddresses[i], 12);
    logFormatted("DS18B20 #%u address: %s", i + 1,
                 formatDeviceAddress(tempSensorAddresses[i]).c_str());
  }

  logFormatted("DS18B20 usable sensor count: %u", temperatureSensorCount);

  if (rawTemperatureDeviceCount < 2) {
    logLine("Expected 2 DS18B20 sensors on the shared 1-wire bus");
  }

  processTemperatureSensors();
}

void ensureLocalSensorsInitialized() {
  const unsigned long now = millis();

  if (!ina219Ready &&
      (lastIna219InitAttemptMs == 0 ||
       now - lastIna219InitAttemptMs >= kLocalSensorRetryIntervalMs)) {
    logLine("Retrying INA219 initialization");
    initializeIna219();
  }

  if (ina219Ready && lastIna219ReadSuccessMs != 0 &&
      now - lastIna219ReadSuccessMs >= kIna219ReadFailureReinitMs &&
      now - lastIna219InitAttemptMs >= kLocalSensorRetryIntervalMs) {
    logLine("Retrying INA219 initialization after repeated read failures");
    initializeIna219();
  }

  if (!temperaturesReady &&
      (lastTemperatureInitAttemptMs == 0 ||
       now - lastTemperatureInitAttemptMs >= kLocalSensorRetryIntervalMs)) {
    logLine("Retrying DS18B20 initialization");
    initializeTemperatureSensors();
    return;
  }

  if (temperaturesReady && lastTemperatureReadSuccessMs != 0 &&
      now - lastTemperatureReadSuccessMs >= kTemperatureReadFailureReinitMs &&
      now - lastTemperatureInitAttemptMs >= kLocalSensorRetryIntervalMs) {
    logLine("Retrying DS18B20 initialization after repeated read failures");
    initializeTemperatureSensors();
    return;
  }

  if (temperaturesReady) {
    if (hasAnyValidTemperatureReading()) {
      lastTemperatureFailureStartedMs = 0;
    } else if (lastTemperatureSampleMs != 0) {
      if (lastTemperatureFailureStartedMs == 0) {
        lastTemperatureFailureStartedMs = lastTemperatureSampleMs;
      }

      if (now - lastTemperatureFailureStartedMs >= kTemperatureReadFailureReinitMs &&
          now - lastTemperatureInitAttemptMs >= kLocalSensorRetryIntervalMs) {
        logLine("Retrying DS18B20 initialization after sustained invalid readings");
        initializeTemperatureSensors();
      }
    }
  }
}

void processTemperatureSensors() {
  if (!temperaturesReady) {
    return;
  }

  const unsigned long now = millis();
  if (lastTemperatureSampleMs != 0 && now - lastTemperatureSampleMs < kTemperatureSampleIntervalMs) {
    return;
  }

  temperatureSensors.requestTemperatures();

  bool anyReadSucceeded = false;
  for (uint8_t i = 0; i < 2; ++i) {
    if (!tempSensorPresent[i]) {
      tempSensorReadingValid[i] = false;
      latestTempSensorF[i] = NAN;
      continue;
    }

    const float tempC = temperatureSensors.getTempC(tempSensorAddresses[i]);
    if (tempC != DEVICE_DISCONNECTED_C) {
      latestTempSensorF[i] = DallasTemperature::toFahrenheit(tempC);
      tempSensorReadingValid[i] = true;
      anyReadSucceeded = true;
      logFormatted("DS18B20 #%u tempF=%.2f", i + 1, latestTempSensorF[i]);
    } else {
      tempSensorReadingValid[i] = false;
      latestTempSensorF[i] = NAN;
      logFormatted("DS18B20 #%u read failed", i + 1);
    }
  }

  lastTemperatureSampleMs = now;
  if (anyReadSucceeded) {
    lastTemperatureReadSuccessMs = now;
    lastTemperatureFailureStartedMs = 0;
  } else if (lastTemperatureFailureStartedMs == 0) {
    lastTemperatureFailureStartedMs = now;
  }
}
}  // namespace

void setup() {
  ++bootCount;
  Serial.begin(115200);
  delay(250);
  logFormatted("Boot count: %lu", static_cast<unsigned long>(bootCount));

  pinMode(PIN_RELAY, OUTPUT);
  if (routerRestartInProgress) {
    logLine("Boot detected during router restart; restoring power and clearing transient state");
    cancelRouterRestart();
    desiredRouterPowered = true;
  }
  driveRouterPowerOutput(desiredRouterPowered);

  WiFi.mode(WIFI_STA);
  mqttClient.setServer(MQTT_HOST, MQTT_PORT);
  mqttClient.setBufferSize(kMqttBufferSize);
  mqttClient.setSocketTimeout(kMqttSocketTimeoutSeconds);
  mqttClient.setCallback(mqttCallback);
  initializeIna219();
  initializeTemperatureSensors();
  victronMonitor.begin();

  ensureWifiConnected();
  ensureMqttConnected();
  if (mqttClient.connected()) {
    publishStatus("booting");
    publishTelemetry();
  } else {
    telemetryPublishRequested = true;
  }
  lastPublishMs = millis();
}

void loop() {
  processRouterRestart();
  ensureMqttConnected();
  mqttClient.loop();
  processTemperatureSensors();
  processRouterTelemetry();
  victronMonitor.poll();
  publishVictronTelemetry(false);
  ensureLocalSensorsInitialized();
  ensureVictronTelemetryCleared();

  const unsigned long now = millis();
  if (telemetryPublishRequested && mqttClient.connected()) {
    publishTelemetry();
    telemetryPublishRequested = false;
    lastPublishMs = now;
  } else if (now - lastPublishMs >= SENSOR_PUBLISH_INTERVAL_MS) {
    publishTelemetry();
    lastPublishMs = now;
  }

  delay(50);
}
