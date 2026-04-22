# IoT Solar Airmesh Router Box

Firmware for a `Seeed XIAO ESP32C3` that monitors and power-cycles a field router over MQTT. The project publishes local box telemetry, exposes Home Assistant discovery entities, and can optionally decode Victron SmartSolar BLE advertisements for solar-side status.

## What it does

- Switches router power through a relay
- Publishes router voltage, current, and watts from an `INA219`
- Publishes two `DS18B20` temperature sensors on a shared 1-wire bus
- Accepts MQTT commands for `ON`, `OFF`, `TOGGLE`, and `RESTART`
- Publishes Home Assistant MQTT discovery for switch, button, and sensor entities
- Optionally reads Victron BLE telemetry when `VICTRON_ENABLED` is set

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
- Optional Victron BLE settings:
  - `VICTRON_ENABLED`
  - `VICTRON_KEY`
  - `VICTRON_MAC_ADDRESS`
  - `VICTRON_DEVICE_ID`
  - `VICTRON_DEVICE_NAME`

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
- marks device availability `online` or `offline`

Relay command payloads accepted on `TOPIC_RELAY_SET`:

- `ON`
- `OFF`
- `TOGGLE`
- `RESTART`

Key runtime topics are configured in `router_box_config.h`, including:

- temperature topics for `box1` and `box2`
- router `volts`, `amps`, and `watts`
- relay `set` and `state`
- device `status`
- optional Victron telemetry topics

## Home Assistant

On MQTT connect, the device publishes discovery config for:

- router power switch
- router restart button
- box temperature sensors
- router voltage/current/power sensors
- optional Victron battery, solar, charge-state, RSSI, and link sensors

This makes it straightforward to drop the device into a Home Assistant MQTT setup without manual entity creation.

## Victron BLE support

When enabled, the firmware scans for BLE advertisements from the configured Victron device, decrypts the instant-readout payload, and publishes values such as:

- battery voltage
- charge current
- solar power
- yield today
- load current
- charge state
- charger error code
- RSSI

If the Victron link goes stale, the firmware clears retained telemetry and marks the link state accordingly.

## Notes

- The relay configuration defaults to `RELAY_ACTIVE_HIGH = true` and `RELAY_USE_NC_CONTACT = true`; make sure that matches your hardware wiring
- The DS18B20 bus expects a `4.7k` pull-up to `3.3V`
- All modules must share a common ground
- `include/router_box_config.h` likely contains local credentials and should stay out of version control
