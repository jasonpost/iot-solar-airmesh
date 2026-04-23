# IoT Solar Airmesh Router Box

Firmware for a `Seeed XIAO ESP32C3` that monitors and power-cycles a field router over MQTT. The project publishes local box telemetry, exposes Home Assistant discovery entities, and can optionally decode Victron SmartSolar BLE advertisements for solar-side status.

## What it does

- Switches router power through a relay
- Publishes router voltage, current, watts, and accumulated watt-hours from an `INA219`
- Publishes two `DS18B20` temperature sensors on a shared 1-wire bus
- Accepts MQTT commands for `ON`, `OFF`, `TOGGLE`, and `RESTART`
- Publishes Home Assistant MQTT discovery for switch, button, and sensor entities
- Publishes retained availability, relay state, and runtime status topics for automation
- Retries Wi-Fi, MQTT, INA219, and DS18B20 initialization automatically after failures
- Clears stale retained telemetry when local sensors or Victron data go invalid
- Optionally reads Victron BLE telemetry when `VICTRON_ENABLED` is set
- Optionally estimates battery percent from Victron battery voltage when `BATTERY_SOC_ENABLED` is set

## Hardware

Core hardware used by this repo:

- `Seeed XIAO ESP32C3`
- `INA219` current sensor
- `2x DS18B20` temperature sensors
- `5V relay module`
- `12V/24V -> 5V` regulator

Default pin mapping:

- `GPIO2` -> DS18B20 1-wire data
- `GPIO3` -> relay control
- `GPIO6` -> INA219 SDA
- `GPIO7` -> INA219 SCL

More build details:

- [Build summary](router_box_build.md)
- [Wiring guide](docs/router_box_wiring.md)
- [Simple schematic](docs/router_box_schematic.md)
- [Step-by-step assembly](docs/router_box_step_by_step.md)

## Project layout

- [`src/main.cpp`](src/main.cpp) contains the main control loop, MQTT handling, relay logic, and local sensor publishing
- [`src/victron_monitor.cpp`](src/victron_monitor.cpp) handles Victron BLE scan/decode
- [`include/router_box_config.example.h`](include/router_box_config.example.h) is the reference config template
- [`include/router_box_config.local.example.h`](include/router_box_config.local.example.h) is a safer local-values template
- [`platformio.ini`](platformio.ini) defines the `seeed_xiao_esp32c3` PlatformIO environment and library dependencies

## Configuration

The firmware builds against `include/router_box_config.h`.

Use `include/router_box_config.example.h` as the reference for required values:

- Wi-Fi SSID and password
- MQTT host, port, username, password, and client ID
- MQTT topic names
- Pin assignments and timing constants
- Relay polarity and default power state
- Upload/monitor serial port should match your PlatformIO environment
- Optional Victron BLE settings:
  - `VICTRON_ENABLED`
  - `VICTRON_KEY`
  - `VICTRON_MAC_ADDRESS`
  - `VICTRON_DEVICE_ID`
  - `VICTRON_DEVICE_NAME`
- Optional battery state-of-charge estimation settings:
  - `BATTERY_SOC_ENABLED`
  - `BATTERY_CAPACITY_AH`
  - `BATTERY_SERIES_CELLS`
  - `BATTERY_SOC_SMOOTHING_ALPHA`
  - `BATTERY_SOC_VOLTAGE_DEADBAND`
  - `BATTERY_SOC_USE_CURRENT_ASSIST`
  - `BATTERY_SOC_CURRENT_ASSIST_VOLTS_PER_AMP`
  - `BATTERY_SOC_CURRENT_DEADBAND_AMPS`

If you want a non-committed scratch template, start from `include/router_box_config.local.example.h` and copy the values you need into `include/router_box_config.h`.

## Build and flash

This project uses PlatformIO.

```powershell
pio run -e seeed_xiao_esp32c3
pio run -e seeed_xiao_esp32c3 --target upload
pio device monitor --baud 115200
```

The current `platformio.ini` uses:

- board: `seeed_xiao_esp32c3`
- framework: `arduino`
- upload port: `COM3`
- monitor port: `COM3`
- monitor speed: `115200`

Libraries pulled by PlatformIO:

- `Adafruit INA219`
- `NimBLE-Arduino`
- `PubSubClient`
- `DallasTemperature`
- `OneWire`

## MQTT behavior

The firmware:

- connects to Wi-Fi and MQTT with retry logic
- publishes retained state and telemetry topics
- subscribes to the relay command topic
- republishes Home Assistant discovery on reconnect
- marks device availability `online` or `offline` using an MQTT last-will
- publishes router runtime state such as `booting`, `router_running`, `router_off`, and `router_restarting`
- publishes local sensor debug/health state so Home Assistant can show when sensors are `ready`, `waiting_for_valid_read`, or `not_ready`
- republishes telemetry after reconnect so retained topics recover cleanly

Relay command payloads accepted on `TOPIC_RELAY_SET`:

- `ON`
- `OFF`
- `TOGGLE`
- `RESTART`

Key runtime topics are configured in `router_box_config.h`, including:

- temperature topics for `box1` and `box2`
- router `volts`, `amps`, `watts`, and `wh`
- relay `set` and `state`
- device `status`
- MQTT availability topic
- optional Victron telemetry topics

Additional built-in debug/status topics published by the firmware:

- `littlelodge/routerbox/availability`
- `littlelodge/routerbox/router_relay/pin_level`
- `littlelodge/routerbox/temp/debug/count`
- `littlelodge/routerbox/temp/debug/raw_count`
- `littlelodge/routerbox/temp/debug/valid_count`
- `littlelodge/routerbox/temp/debug/state`
- `littlelodge/routerbox/router/debug/ina219_state`

## Home Assistant

On MQTT connect, the device publishes discovery config for:

- router power switch
- router restart button
- box temperature sensors
- temperature debug count/state sensors
- router voltage/current/power/energy sensors
- optional Victron battery voltage, charge current, solar power, yield today, load current, charge state, charge state code, charger error, RSSI, and link sensors
- optional Victron battery percent sensor when `BATTERY_SOC_ENABLED` is enabled

This makes it straightforward to drop the device into a Home Assistant MQTT setup without manual entity creation.

## Victron BLE support

When enabled, the firmware scans for BLE advertisements from the configured Victron device, decrypts the instant-readout payload, and publishes values such as:

- battery voltage
- charge current
- solar power
- yield today
- load current
- charge state
- charge state code
- charger error code
- RSSI
- link state

If `BATTERY_SOC_ENABLED` is turned on, the firmware also estimates battery percentage from battery voltage using a smoothed LiFePO4-style voltage curve with optional current-based compensation.

If the Victron link goes stale, the firmware clears retained telemetry and marks the link state accordingly.

## Local Sensor Resilience

The firmware now treats local sensors as recoverable rather than one-shot startup dependencies:

- `INA219` initialization is retried if startup fails or repeated reads become invalid
- `DS18B20` discovery is retried if the bus is missing, no valid temperatures are seen for an extended period, or reads repeatedly fail
- invalid sensor readings clear retained MQTT values instead of leaving stale numbers behind
- temperature sensor count, raw count, valid count, and state are published for field debugging
- relay GPIO pin level is published so you can verify active-high/active-low behavior remotely

## Notes

- The relay configuration defaults to `RELAY_ACTIVE_HIGH = true` and `RELAY_USE_NC_CONTACT = true`; make sure that matches your hardware wiring
- The DS18B20 bus expects a `4.7k` pull-up to `3.3V`
- All modules must share a common ground
- `include/router_box_config.h` likely contains local credentials and should stay out of version control
