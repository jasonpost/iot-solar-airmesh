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

constexpr bool VICTRON_ENABLED = false;
constexpr char VICTRON_KEY[] = "replace-with-32-char-hex-key";
constexpr char VICTRON_MAC_ADDRESS[] = "replace-with-12-hex-char-mac";
constexpr char VICTRON_DEVICE_ID[] = "victron_solar_1";
constexpr char VICTRON_DEVICE_NAME[] = "Victron Solar 1";

constexpr char TOPIC_TEMP_BOX1[] = "littlelodge/routerbox/temp/box1";
constexpr char TOPIC_TEMP_BOX2[] = "littlelodge/routerbox/temp/box2";
constexpr char TOPIC_ROUTER_VOLTS[] = "littlelodge/routerbox/router/volts";
constexpr char TOPIC_ROUTER_AMPS[] = "littlelodge/routerbox/router/amps";
constexpr char TOPIC_ROUTER_WATTS[] = "littlelodge/routerbox/router/watts";
constexpr char TOPIC_ROUTER_WH[] = "littlelodge/routerbox/router/wh";
constexpr char TOPIC_VICTRON_BATTERY_VOLTS[] = "littlelodge/routerbox/victron/battery_volts";
constexpr char TOPIC_VICTRON_BATTERY_PERCENT[] = "littlelodge/routerbox/victron/battery_percent";
constexpr char TOPIC_VICTRON_CHARGE_AMPS[] = "littlelodge/routerbox/victron/charge_amps";
constexpr char TOPIC_VICTRON_SOLAR_WATTS[] = "littlelodge/routerbox/victron/solar_watts";
constexpr char TOPIC_VICTRON_YIELD_TODAY_WH[] = "littlelodge/routerbox/victron/yield_today_wh";
constexpr char TOPIC_VICTRON_LOAD_AMPS[] = "littlelodge/routerbox/victron/load_amps";
constexpr char TOPIC_VICTRON_CHARGE_STATE[] = "littlelodge/routerbox/victron/charge_state";
constexpr char TOPIC_VICTRON_CHARGE_STATE_CODE[] = "littlelodge/routerbox/victron/charge_state_code";
constexpr char TOPIC_VICTRON_ERROR_CODE[] = "littlelodge/routerbox/victron/error_code";
constexpr char TOPIC_VICTRON_RSSI[] = "littlelodge/routerbox/victron/rssi";
constexpr char TOPIC_VICTRON_LINK_STATE[] = "littlelodge/routerbox/victron/link_state";
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
constexpr uint32_t VICTRON_SCAN_INTERVAL_MS = 5000;
constexpr uint8_t VICTRON_SCAN_DURATION_SECONDS = 1;
constexpr uint32_t VICTRON_STALE_TIMEOUT_MS = 30000;

constexpr bool BATTERY_SOC_ENABLED = false;
constexpr float BATTERY_CAPACITY_AH = 20.0f;
constexpr uint8_t BATTERY_SERIES_CELLS = 4;
constexpr float BATTERY_SOC_SMOOTHING_ALPHA = 0.20f;
constexpr float BATTERY_SOC_VOLTAGE_DEADBAND = 0.01f;
constexpr bool BATTERY_SOC_USE_CURRENT_ASSIST = true;
constexpr float BATTERY_SOC_CURRENT_ASSIST_VOLTS_PER_AMP = 0.015f;
constexpr float BATTERY_SOC_CURRENT_DEADBAND_AMPS = 0.2f;
}
