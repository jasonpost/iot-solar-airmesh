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
RTC_DATA_ATTR uint32_t bootCount = 0;

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
unsigned long lastLocalSensorInitAttemptMs = 0;
bool ina219Ready = false;
bool temperaturesReady = false;
uint8_t temperatureSensorCount = 0;
bool victronLinkStatePublished = false;
bool victronLinkOnline = false;
bool wifiConnectInProgress = false;
unsigned long wifiConnectStartedMs = 0;
bool routerRestartInProgress = false;
unsigned long routerRestartResumeMs = 0;
constexpr char kTopicAvailability[] = "littlelodge/routerbox/availability";
constexpr char kTopicRelayPinLevel[] = "littlelodge/routerbox/router_relay/pin_level";
constexpr char kTopicTempSensorCount[] = "littlelodge/routerbox/temp/debug/count";
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
void initializeSensors();
void ensureLocalSensorsInitialized();
void processRouterRestart();

void cancelRouterRestart() {
  routerRestartInProgress = false;
  routerRestartResumeMs = 0;
}

const char *routerPowerStateString() {
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

void setRouterPowerState(bool routerPowered) {
  desiredRouterPowered = routerPowered;
  const bool pinLevel = coilLevelFor(relayCoilShouldBeEnergized(routerPowered));
  digitalWrite(PIN_RELAY, pinLevel);
  logFormatted("Router power state set to %s, GPIO level=%s, digitalRead=%s",
               routerPowerStateString(), pinLevel ? "HIGH" : "LOW",
               digitalRead(PIN_RELAY) ? "HIGH" : "LOW");

  if (mqttClient.connected()) {
    publishRetained(kTopicRelayPinLevel, pinLevel ? "HIGH" : "LOW");
  }
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
  clearRetained(TOPIC_VICTRON_BATTERY_VOLTS);
  clearRetained(TOPIC_VICTRON_CHARGE_AMPS);
  clearRetained(TOPIC_VICTRON_SOLAR_WATTS);
  clearRetained(TOPIC_VICTRON_YIELD_TODAY_WH);
  clearRetained(TOPIC_VICTRON_LOAD_AMPS);
  clearRetained(TOPIC_VICTRON_CHARGE_STATE);
  clearRetained(TOPIC_VICTRON_CHARGE_STATE_CODE);
  clearRetained(TOPIC_VICTRON_ERROR_CODE);
  clearRetained(TOPIC_VICTRON_RSSI);
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
    return;
  }

  if (!forcePublish && !victronMonitor.hasPendingPublish()) {
    return;
  }

  if (!victronLinkStatePublished || !victronLinkOnline) {
    publishVictronLinkState(true);
  }

  const VictronTelemetry &telemetry = victronMonitor.telemetry();
  if (!isnan(telemetry.batteryVoltage)) {
    publishFloat(TOPIC_VICTRON_BATTERY_VOLTS, telemetry.batteryVoltage, 2);
  } else {
    clearRetained(TOPIC_VICTRON_BATTERY_VOLTS);
  }
  if (!isnan(telemetry.chargeCurrent)) {
    publishFloat(TOPIC_VICTRON_CHARGE_AMPS, telemetry.chargeCurrent, 1);
  } else {
    clearRetained(TOPIC_VICTRON_CHARGE_AMPS);
  }
  if (!isnan(telemetry.solarPower)) {
    publishFloat(TOPIC_VICTRON_SOLAR_WATTS, telemetry.solarPower, 0);
  } else {
    clearRetained(TOPIC_VICTRON_SOLAR_WATTS);
  }
  if (!isnan(telemetry.yieldTodayWh)) {
    publishFloat(TOPIC_VICTRON_YIELD_TODAY_WH, telemetry.yieldTodayWh, 0);
  } else {
    clearRetained(TOPIC_VICTRON_YIELD_TODAY_WH);
  }
  if (!isnan(telemetry.loadCurrent)) {
    publishFloat(TOPIC_VICTRON_LOAD_AMPS, telemetry.loadCurrent, 1);
  } else {
    clearRetained(TOPIC_VICTRON_LOAD_AMPS);
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
  setRouterPowerState(false);
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
  setRouterPowerState(true);
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
  publishAvailability("online");
  publishHomeAssistantDiscovery();
  publishRelayState();
  publishStatus(desiredRouterPowered ? "router_running" : "router_off");
  return true;
}

void publishTelemetry() {
  if (!ensureMqttConnected()) {
    return;
  }

  ensureLocalSensorsInitialized();

  publishInt(kTopicTempSensorCount, temperatureSensorCount);

  if (temperaturesReady) {
    publishLogged(kTopicTempSensorState, "ready");
    temperatureSensors.requestTemperatures();

    const char *const tempTopics[2] = {TOPIC_TEMP_BOX1, TOPIC_TEMP_BOX2};
    for (uint8_t i = 0; i < 2; ++i) {
      if (!tempSensorPresent[i]) {
        clearRetained(tempTopics[i]);
        continue;
      }

      const float tempC = temperatureSensors.getTempC(tempSensorAddresses[i]);
      if (tempC != DEVICE_DISCONNECTED_C) {
        const float tempF = DallasTemperature::toFahrenheit(tempC);
        logFormatted("DS18B20 #%u tempF=%.2f", i + 1, tempF);
        publishFloat(tempTopics[i], tempF, 2);
      } else {
        logFormatted("DS18B20 #%u read failed", i + 1);
        clearRetained(tempTopics[i]);
      }
    }
  } else {
    logLine("Skipping DS18B20 publish because no sensors were initialized");
    publishLogged(kTopicTempSensorState, "not_ready");
    clearRetained(TOPIC_TEMP_BOX1);
    clearRetained(TOPIC_TEMP_BOX2);
  }

  if (ina219Ready) {
    const float busVoltage = ina219.getBusVoltage_V();
    const float shuntMillivolts = ina219.getShuntVoltage_mV();
    const float currentMilliamps = ina219.getCurrent_mA();
    const float loadVoltage = busVoltage + (shuntMillivolts / 1000.0f);
    float currentAmps = currentMilliamps / 1000.0f;
    float watts = loadVoltage * currentAmps;

    if (fabsf(currentAmps) < kIna219CurrentDeadbandAmps) {
      currentAmps = 0.0f;
    }
    if (fabsf(watts) < kIna219PowerDeadbandWatts) {
      watts = 0.0f;
    }

    logFormatted("INA219 loadV=%.3f currentA=%.3f watts=%.3f", loadVoltage, currentAmps,
                 watts);
    publishLogged(kTopicIna219State, "ready");
    publishFloat(TOPIC_ROUTER_VOLTS, loadVoltage, 3);
    publishFloat(TOPIC_ROUTER_AMPS, currentAmps, 3);
    publishFloat(TOPIC_ROUTER_WATTS, watts, 3);
  } else {
    logLine("Skipping INA219 publish because sensor was not initialized");
    publishLogged(kTopicIna219State, "not_ready");
  }

  publishVictronTelemetry(true);
  publishRelayState();
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

void initializeSensors() {
  lastLocalSensorInitAttemptMs = millis();
  Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);
  ina219Ready = ina219.begin();
  logFormatted("INA219 ready: %s", ina219Ready ? "yes" : "no");

  temperatureSensors.begin();
  temperatureSensors.setWaitForConversion(true);
  const uint8_t deviceCount = temperatureSensors.getDeviceCount();
  temperatureSensorCount = min<uint8_t>(deviceCount, 2);
  tempSensorPresent[0] = false;
  tempSensorPresent[1] = false;
  temperaturesReady = false;
  logFormatted("DS18B20 bus pin GPIO%u", PIN_ONEWIRE);
  logFormatted("DS18B20 count: %u", deviceCount);

  for (uint8_t i = 0; i < temperatureSensorCount; ++i) {
    if (!temperatureSensors.getAddress(tempSensorAddresses[i], i)) {
      logFormatted("Failed to read DS18B20 address at slot %u", i);
      continue;
    }

    tempSensorPresent[i] = true;
    temperaturesReady = true;
    temperatureSensors.setResolution(tempSensorAddresses[i], 12);
    logFormatted("DS18B20 #%u address: %s", i + 1,
                 formatDeviceAddress(tempSensorAddresses[i]).c_str());
  }

  if (deviceCount < 2) {
    logLine("Expected 2 DS18B20 sensors on the shared 1-wire bus");
  }
}

void ensureLocalSensorsInitialized() {
  if (ina219Ready && temperaturesReady) {
    return;
  }

  const unsigned long now = millis();
  if (lastLocalSensorInitAttemptMs != 0 &&
      now - lastLocalSensorInitAttemptMs < kLocalSensorRetryIntervalMs) {
    return;
  }

  logLine("Retrying local sensor initialization");
  initializeSensors();
}
}  // namespace

void setup() {
  ++bootCount;
  Serial.begin(115200);
  delay(250);
  logFormatted("Boot count: %lu", static_cast<unsigned long>(bootCount));

  pinMode(PIN_RELAY, OUTPUT);
  setRouterPowerState(desiredRouterPowered);

  WiFi.mode(WIFI_STA);
  mqttClient.setServer(MQTT_HOST, MQTT_PORT);
  mqttClient.setBufferSize(kMqttBufferSize);
  mqttClient.setSocketTimeout((MQTT_CONNECT_TIMEOUT_MS + 999) / 1000);
  mqttClient.setCallback(mqttCallback);
  initializeSensors();
  victronMonitor.begin();

  publishStatus("booting");
  ensureWifiConnected();
  ensureMqttConnected();
  publishTelemetry();
  lastPublishMs = millis();
}

void loop() {
  processRouterRestart();
  ensureMqttConnected();
  mqttClient.loop();
  victronMonitor.poll();
  publishVictronTelemetry(false);
  ensureLocalSensorsInitialized();

  const unsigned long now = millis();
  if (now - lastPublishMs >= SENSOR_PUBLISH_INTERVAL_MS) {
    publishTelemetry();
    lastPublishMs = now;
  }

  delay(50);
}
