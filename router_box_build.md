
# IoT Router Box - Build Summary

## System Overview
You are building a smart router monitoring and control system using:
- XIAO ESP32C3
- MQTT + Home Assistant
- Victron BLE (with encryption key)

### Features
- Monitor 2 temperatures
- Monitor router voltage/current/power
- Restart router via MQTT
- Read Victron BLE data (phase 2)

---

## Parts List

### Core
- XIAO ESP32C3
- Tobsun 12V/24V → 5V 3A regulator

### Sensors
- 2x DS18B20
- INA219 current sensor

### Control
- Relay module (Songle-based, with IN/VCC/GND)

### Supporting
- 4.7k resistor
- 470µF capacitor
- 0.1µF capacitor
- Wago connectors

---

## Box Layout

### Box 1
- Battery
- Victron charger
- DS18B20 (temp sensor #1)

### Box 2
- XIAO ESP32C3
- DS18B20 (temp sensor #2)
- INA219
- Relay
- Router
- Regulator
- Capacitors

---

## Power Flow

Power + → INA219 VIN+  
INA219 VIN- → Relay COM  
Relay NO → Router +  

Power - → Router -

---

## XIAO Pin Map

- GPIO2 → DS18B20 bus
- GPIO6 → SDA (INA219)
- GPIO7 → SCL (INA219)
- GPIO3 → Relay control

---

## Capacitor Placement

Both capacitors go in parallel:

- 470µF: + → 5V, - → GND
- 0.1µF: between 5V and GND

Placed in Wago connectors with XIAO power.

---

## MQTT Topics

### Temps
- garden/routerbox/temp/box1
- garden/routerbox/temp/box2

### Power
- garden/routerbox/router/volts
- garden/routerbox/router/amps
- garden/routerbox/router/watts

### Relay
- garden/routerbox/router_relay/set
- garden/routerbox/router_relay/state

---

## Build Order

1. Power + WiFi + MQTT
2. Relay control
3. Temp sensors
4. INA219
5. Victron BLE

---

## Notes

- Ensure common ground across all components
- Use 4.7k resistor on DS18B20 data line
- Verify relay logic (active low/high)
- Place capacitors close to XIAO

