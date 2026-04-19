#include <Arduino.h>
#include <WiFi.h>
#include <Wire.h>

#include <Adafruit_INA219.h>
#include <DallasTemperature.h>
#include <OneWire.h>
#include <PubSubClient.h>

#include "router_box_config.h"

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

unsigned long lastPublishMs = 0;
unsigned long lastWifiAttemptMs = 0;
unsigned long lastMqttAttemptMs = 0;
bool ina219Ready = false;
bool temperaturesReady = false;
uint8_t temperatureSensorCount = 0;
constexpr char kTopicAvailability[] = "littlelodge/routerbox/availability";
constexpr char kTopicRelayPinLevel[] = "littlelodge/routerbox/router_relay/pin_level";
constexpr char kTopicTempSensorCount[] = "littlelodge/routerbox/temp/debug/count";
constexpr char kTopicTempSensorState[] = "littlelodge/routerbox/temp/debug/state";
constexpr char kDiscoveryNodeId[] = "router_box";
constexpr uint16_t kMqttBufferSize = 1024;

void logLine(const String &message) {
  Serial.println("[router-box] " + message);
}

bool publishRetained(const char *topic, const String &payload);
bool publishLogged(const char *topic, const String &payload);
void publishHomeAssistantDiscovery();

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
  logLine(String("Router power state set to ") + routerPowerStateString() +
          ", GPIO level=" + (pinLevel ? "HIGH" : "LOW") +
          ", digitalRead=" + (digitalRead(PIN_RELAY) ? "HIGH" : "LOW"));

  if (mqttClient.connected()) {
    publishRetained(kTopicRelayPinLevel, pinLevel ? "HIGH" : "LOW");
  }
}

bool publishRetained(const char *topic, const String &payload) {
  return mqttClient.publish(topic, payload.c_str(), true);
}

bool publishLogged(const char *topic, const String &payload) {
  const bool success = publishRetained(topic, payload);
  logLine(String("MQTT publish ") + (success ? "ok" : "failed") +
          " topic=" + topic + " payload=" + payload);
  return success;
}

String discoveryDeviceJson() {
  return String("{\"ids\":[\"") + MQTT_CLIENT_ID +
         "\"],\"name\":\"Router Box\",\"mf\":\"Jason Post\",\"mdl\":\"Seeed XIAO ESP32C3 Router Box\"}";
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

void publishHomeAssistantDiscovery() {
  const String device = discoveryDeviceJson();
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
          "\"stat_t\":\"" + TOPIC_TEMP_BOX1 +
          "\",\"dev_cla\":\"temperature\",\"unit_of_meas\":\"°F\",\"stat_cla\":\"measurement\","
          + availability + ",\"dev\":" + device + "}");

  publishDiscoveryConfig(
      "sensor",
      "temp_box2",
      String("{\"name\":\"Box 2 Temperature\",\"uniq_id\":\"router_box_temp_box2\",") +
          "\"stat_t\":\"" + TOPIC_TEMP_BOX2 +
          "\",\"dev_cla\":\"temperature\",\"unit_of_meas\":\"°F\",\"stat_cla\":\"measurement\","
          + availability + ",\"dev\":" + device + "}");

  publishDiscoveryConfig(
      "sensor",
      "router_voltage",
      String("{\"name\":\"Router Voltage\",\"uniq_id\":\"router_box_router_voltage\",") +
          "\"stat_t\":\"" + TOPIC_ROUTER_VOLTS +
          "\",\"dev_cla\":\"voltage\",\"unit_of_meas\":\"V\",\"stat_cla\":\"measurement\","
          + availability + ",\"dev\":" + device + "}");

  publishDiscoveryConfig(
      "sensor",
      "router_current",
      String("{\"name\":\"Router Current\",\"uniq_id\":\"router_box_router_current\",") +
          "\"stat_t\":\"" + TOPIC_ROUTER_AMPS +
          "\",\"dev_cla\":\"current\",\"unit_of_meas\":\"A\",\"stat_cla\":\"measurement\","
          + availability + ",\"dev\":" + device + "}");

  publishDiscoveryConfig(
      "sensor",
      "router_power",
      String("{\"name\":\"Router Power Draw\",\"uniq_id\":\"router_box_router_power_draw\",") +
          "\"stat_t\":\"" + TOPIC_ROUTER_WATTS +
          "\",\"dev_cla\":\"power\",\"unit_of_meas\":\"W\",\"stat_cla\":\"measurement\","
          + availability + ",\"dev\":" + device + "}");
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

void restartRouter() {
  logLine("Restart command received, cycling router power");
  setRouterPowerState(false);
  publishRelayState();
  publishStatus("router_restarting");
  delay(RESTART_OFF_MS);
  setRouterPowerState(true);
  publishRelayState();
  publishStatus("router_running");
}

bool ensureWifiConnected() {
  if (WiFi.status() == WL_CONNECTED) {
    return true;
  }

  const unsigned long now = millis();
  if (now - lastWifiAttemptMs < MQTT_RECONNECT_INTERVAL_MS) {
    return false;
  }

  lastWifiAttemptMs = now;
  logLine(String("Connecting to Wi-Fi SSID: ") + WIFI_SSID);
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  const unsigned long startedAt = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - startedAt < WIFI_CONNECT_TIMEOUT_MS) {
    delay(250);
  }

  if (WiFi.status() == WL_CONNECTED) {
    logLine(String("Wi-Fi connected, IP: ") + WiFi.localIP().toString());
    logLine(String("Subnet mask: ") + WiFi.subnetMask().toString());
    logLine(String("Gateway: ") + WiFi.gatewayIP().toString());
    logLine(String("RSSI: ") + WiFi.RSSI());
  } else {
    logLine(String("Wi-Fi connection failed, status=") + WiFi.status());
  }

  return WiFi.status() == WL_CONNECTED;
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
  logLine(String("Connecting to MQTT broker ") + MQTT_HOST + ":" + MQTT_PORT);
  mqttClient.setServer(MQTT_HOST, MQTT_PORT);

  WiFiClient probeClient;
  if (probeClient.connect(MQTT_HOST, MQTT_PORT)) {
    logLine("TCP connect to broker succeeded");
    probeClient.stop();
  } else {
    logLine("TCP connect to broker failed");
  }

  WiFiClient homeAssistantProbe;
  if (homeAssistantProbe.connect(MQTT_HOST, 8123)) {
    logLine("TCP connect to Home Assistant port 8123 succeeded");
    homeAssistantProbe.stop();
  } else {
    logLine("TCP connect to Home Assistant port 8123 failed");
  }

  const bool connected = mqttClient.connect(
      MQTT_CLIENT_ID,
      MQTT_USERNAME[0] == '\0' ? nullptr : MQTT_USERNAME,
      MQTT_PASSWORD[0] == '\0' ? nullptr : MQTT_PASSWORD,
      kTopicAvailability,
      1,
      true,
      "offline");

  if (!connected) {
    logLine(String("MQTT connection failed, state=") + mqttClient.state());
    return false;
  }

  mqttClient.subscribe(TOPIC_RELAY_SET);
  logLine(String("MQTT connected and subscribed to ") + TOPIC_RELAY_SET);
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

  publishLogged(kTopicTempSensorCount, String(temperatureSensorCount));

  if (temperaturesReady) {
    publishLogged(kTopicTempSensorState, "ready");
    temperatureSensors.requestTemperatures();

    const float temp0C = temperatureSensorCount >= 1
                             ? temperatureSensors.getTempC(tempSensorAddresses[0])
                             : DEVICE_DISCONNECTED_C;
    const float temp1C = temperatureSensorCount >= 2
                             ? temperatureSensors.getTempC(tempSensorAddresses[1])
                             : DEVICE_DISCONNECTED_C;

    if (temp0C != DEVICE_DISCONNECTED_C) {
      const String payload = String(DallasTemperature::toFahrenheit(temp0C), 2);
      logLine(String("DS18B20 #1 tempF=") + payload);
      publishLogged(TOPIC_TEMP_BOX1, payload);
    } else if (temperatureSensorCount >= 1) {
      logLine("DS18B20 #1 read failed");
    }
    if (temp1C != DEVICE_DISCONNECTED_C) {
      const String payload = String(DallasTemperature::toFahrenheit(temp1C), 2);
      logLine(String("DS18B20 #2 tempF=") + payload);
      publishLogged(TOPIC_TEMP_BOX2, payload);
    } else if (temperatureSensorCount >= 2) {
      logLine("DS18B20 #2 read failed");
    }
  } else {
    logLine("Skipping DS18B20 publish because no sensors were initialized");
    publishLogged(kTopicTempSensorState, "not_ready");
  }

  if (ina219Ready) {
    const float busVoltage = ina219.getBusVoltage_V();
    const float shuntMillivolts = ina219.getShuntVoltage_mV();
    const float currentMilliamps = ina219.getCurrent_mA();
    const float loadVoltage = busVoltage + (shuntMillivolts / 1000.0f);
    const float watts = loadVoltage * (currentMilliamps / 1000.0f);

    publishRetained(TOPIC_ROUTER_VOLTS, String(loadVoltage, 3));
    publishRetained(TOPIC_ROUTER_AMPS, String(currentMilliamps / 1000.0f, 3));
    publishRetained(TOPIC_ROUTER_WATTS, String(watts, 3));
  }

  publishRelayState();
}

void handleRelayCommand(const String &commandRaw) {
  String command = commandRaw;
  command.trim();
  command.toUpperCase();
  logLine(String("MQTT command received: ") + command);

  if (command == "ON") {
    setRouterPowerState(true);
    publishRelayState();
    publishStatus("router_running");
    return;
  }

  if (command == "OFF") {
    setRouterPowerState(false);
    publishRelayState();
    publishStatus("router_off");
    return;
  }

  if (command == "TOGGLE") {
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
  Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);
  ina219Ready = ina219.begin();
  logLine(String("INA219 ready: ") + (ina219Ready ? "yes" : "no"));

  temperatureSensors.begin();
  temperatureSensors.setWaitForConversion(true);
  const uint8_t deviceCount = temperatureSensors.getDeviceCount();
  temperatureSensorCount = min<uint8_t>(deviceCount, 2);
  temperaturesReady = temperatureSensorCount > 0;
  logLine(String("DS18B20 bus pin GPIO") + PIN_ONEWIRE);
  logLine(String("DS18B20 count: ") + deviceCount);

  for (uint8_t i = 0; i < temperatureSensorCount; ++i) {
    if (!temperatureSensors.getAddress(tempSensorAddresses[i], i)) {
      logLine(String("Failed to read DS18B20 address at slot ") + i);
      temperaturesReady = false;
      temperatureSensorCount = i;
      break;
    }

    temperatureSensors.setResolution(tempSensorAddresses[i], 12);
    logLine(String("DS18B20 #") + (i + 1) + " address: " +
            formatDeviceAddress(tempSensorAddresses[i]));
  }

  if (deviceCount < 2) {
    logLine("Expected 2 DS18B20 sensors on the shared 1-wire bus");
  }
}
}  // namespace

void setup() {
  ++bootCount;
  Serial.begin(115200);
  delay(250);
  logLine(String("Boot count: ") + bootCount);

  pinMode(PIN_RELAY, OUTPUT);
  setRouterPowerState(desiredRouterPowered);

  mqttClient.setBufferSize(kMqttBufferSize);
  mqttClient.setCallback(mqttCallback);
  initializeSensors();

  publishStatus("booting");
  ensureWifiConnected();
  ensureMqttConnected();
  publishTelemetry();
  lastPublishMs = millis();
}

void loop() {
  ensureMqttConnected();
  mqttClient.loop();

  const unsigned long now = millis();
  if (now - lastPublishMs >= SENSOR_PUBLISH_INTERVAL_MS) {
    publishTelemetry();
    lastPublishMs = now;
  }

  delay(50);
}
