#pragma once

// Reference-only local template. Copy values manually into router_box_config.h if desired.

namespace RouterBoxConfig {
constexpr char WIFI_SSID[] = "replace-with-your-ssid";
constexpr char WIFI_PASSWORD[] = "replace-with-your-password";

constexpr char MQTT_HOST[] = "192.168.1.10";
constexpr uint16_t MQTT_PORT = 1883;
constexpr char MQTT_USERNAME[] = "replace-with-your-mqtt-user";
constexpr char MQTT_PASSWORD[] = "replace-with-your-mqtt-password";
constexpr char MQTT_CLIENT_ID[] = "router-box";

constexpr char TOPIC_TEMP_BOX1[] = "littlelodge/routerbox/temp/box1";
constexpr char TOPIC_TEMP_BOX2[] = "littlelodge/routerbox/temp/box2";
constexpr char TOPIC_ROUTER_VOLTS[] = "littlelodge/routerbox/router/volts";
constexpr char TOPIC_ROUTER_AMPS[] = "littlelodge/routerbox/router/amps";
constexpr char TOPIC_ROUTER_WATTS[] = "littlelodge/routerbox/router/watts";
constexpr char TOPIC_RELAY_SET[] = "littlelodge/routerbox/router_relay/set";
constexpr char TOPIC_RELAY_STATE[] = "littlelodge/routerbox/router_relay/state";
constexpr char TOPIC_STATUS[] = "littlelodge/routerbox/status";

constexpr uint8_t PIN_ONEWIRE = 2;  // XIAO D0
constexpr uint8_t PIN_RELAY = 3;    // XIAO D1
constexpr uint8_t PIN_I2C_SDA = 6;  // XIAO D4
constexpr uint8_t PIN_I2C_SCL = 7;  // XIAO D5

constexpr bool RELAY_ACTIVE_HIGH = true;
constexpr bool RELAY_USE_NC_CONTACT = true;
constexpr bool ROUTER_DEFAULT_POWERED = true;

constexpr uint32_t SENSOR_PUBLISH_INTERVAL_MS = 60000;
constexpr uint32_t MQTT_RECONNECT_INTERVAL_MS = 10000;
constexpr uint32_t WIFI_CONNECT_TIMEOUT_MS = 20000;
constexpr uint32_t MQTT_CONNECT_TIMEOUT_MS = 10000;
constexpr uint32_t RESTART_OFF_MS = 15000;
}
