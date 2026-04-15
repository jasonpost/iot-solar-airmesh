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

unsigned long lastPublishMs = 0;
unsigned long lastWifiAttemptMs = 0;
unsigned long lastMqttAttemptMs = 0;
bool ina219Ready = false;
bool temperaturesReady = false;

bool coilLevelFor(bool energized) {
  return energized == RELAY_ACTIVE_HIGH ? HIGH : LOW;
}

bool relayCoilShouldBeEnergized(bool routerPowered) {
  return RELAY_USE_NC_CONTACT ? !routerPowered : routerPowered;
}

void setRouterPowerState(bool routerPowered) {
  desiredRouterPowered = routerPowered;
  digitalWrite(PIN_RELAY, coilLevelFor(relayCoilShouldBeEnergized(routerPowered)));
}

const char *routerPowerStateString() {
  return desiredRouterPowered ? "ON" : "OFF";
}

bool publishRetained(const char *topic, const String &payload) {
  return mqttClient.publish(topic, payload.c_str(), true);
}

void publishRelayState() {
  publishRetained(TOPIC_RELAY_STATE, routerPowerStateString());
}

void publishStatus(const char *status) {
  publishRetained(TOPIC_STATUS, status);
}

void restartRouter() {
  setRouterPowerState(false);
  publishRelayState();
  publishStatus("router_restarting");
  delay(RESTART_OFF_MS);
  setRouterPowerState(true);
  publishRelayState();
  publishStatus("router_running");
}

void enterDeepSleepIfNeeded() {
  if (desiredRouterPowered || !ENABLE_SLEEP_WHEN_ROUTER_OFF) {
    return;
  }

  publishStatus("sleeping");
  delay(50);
  esp_sleep_enable_timer_wakeup(static_cast<uint64_t>(SLEEP_WHEN_OFF_SECONDS) * 1000000ULL);
  esp_deep_sleep_start();
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
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  const unsigned long startedAt = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - startedAt < WIFI_CONNECT_TIMEOUT_MS) {
    delay(250);
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
  mqttClient.setServer(MQTT_HOST, MQTT_PORT);

  const bool connected = mqttClient.connect(
      MQTT_CLIENT_ID,
      MQTT_USERNAME[0] == '\0' ? nullptr : MQTT_USERNAME,
      MQTT_PASSWORD[0] == '\0' ? nullptr : MQTT_PASSWORD,
      TOPIC_STATUS,
      1,
      true,
      "offline");

  if (!connected) {
    return false;
  }

  mqttClient.subscribe(TOPIC_RELAY_SET);
  publishStatus("online");
  publishRelayState();
  return true;
}

void publishTelemetry() {
  if (!ensureMqttConnected()) {
    return;
  }

  if (temperaturesReady) {
    temperatureSensors.requestTemperatures();

    const float temp0 = temperatureSensors.getTempCByIndex(0);
    const float temp1 = temperatureSensors.getTempCByIndex(1);

    if (temp0 > -100.0f) {
      publishRetained(TOPIC_TEMP_BOX1, String(temp0, 2));
    }
    if (temp1 > -100.0f) {
      publishRetained(TOPIC_TEMP_BOX2, String(temp1, 2));
    }
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

  temperatureSensors.begin();
  temperaturesReady = temperatureSensors.getDeviceCount() > 0;
}
}  // namespace

void setup() {
  ++bootCount;
  Serial.begin(115200);
  delay(250);

  pinMode(PIN_RELAY, OUTPUT);
  setRouterPowerState(desiredRouterPowered);

  mqttClient.setCallback(mqttCallback);
  initializeSensors();

  ensureWifiConnected();
  ensureMqttConnected();
  publishStatus("booting");
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

  enterDeepSleepIfNeeded();
  delay(50);
}
