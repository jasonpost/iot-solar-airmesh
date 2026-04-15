# Router Box Simple Build Schematic

This version is meant for hand assembly with:

- `XIAO ESP32C3` dev board
- `INA219` module
- `5V relay module`
- `2x DS18B20`
- `Wago connectors`
- `12/24V -> 5V regulator Tobsun Converter`

It is not a PCB schematic. It is a practical "what wire goes where" guide.

Naming used here:

- `Battery/DC input` = the higher-voltage supply coming into the box before the Tobsun converter
- `5V` = the regulated output coming out of the Tobsun converter

## Use This Drawing First

Open the simple wiring picture:

![Router Box Simple Wiring](C:/Users/jason/Projects/iot/iot-solar-airmesh/docs/router_box_simple_layout.svg)

If you want to build in order, use the step-by-step guide:

- [Router box step-by-step assembly](C:/Users/jason/Projects/iot/iot-solar-airmesh/docs/router_box_step_by_step.md)

## The Four Wago Groups

Build the wiring around four connector groups.

### Wago A: battery/DC input

- `BATTERY/DC IN +`
- `BATTERY/DC IN -`

### Wago B: 5V distribution

- `5V`
- `GND`

### Wago C: sensor / logic distribution

- `3V3`
- `DATA`
- `GND`

### Wago D: router output

- `ROUTER+`
- `ROUTER-`

## Quick Wiring Order

Wire it in this order so the build stays simple.

### 1. Make the switched router power path

- `BATTERY/DC IN +` -> `INA219 VIN+`
- `INA219 VIN-` -> `Relay COM`
- `Relay NO` -> `ROUTER+`
- `BATTERY/DC IN -` -> `ROUTER-`

This is the only high-current path.

### 2. Make the 5V power bus

- `BATTERY/DC IN +` -> `Tobsun IN+`
- `BATTERY/DC IN -` -> `Tobsun IN-`
- `Tobsun 5V OUT +` -> `Wago B 5V`
- `Tobsun 5V OUT -` -> `Wago B GND`

Then connect these to `Wago B`:

- `XIAO 5V/VIN` -> `5V`
- `XIAO GND` -> `GND`
- `Relay VCC` -> `5V`
- `Relay GND` -> `GND`
- `INA219 GND` -> `GND`

### 3. Add the capacitors across the 5V bus

Both capacitors go across the same two points, not inline.

- `470uF +` -> `Wago B 5V`
- `470uF -` -> `Wago B GND`
- `0.1uF` one leg -> `Wago B 5V`
- `0.1uF` other leg -> `Wago B GND`

### 4. Make the 3V3 / sensor Wago

From the XIAO, create the small logic bus:

- `XIAO 3V3` -> `Wago C 3V3`
- `XIAO GPIO2` -> `Wago C DATA`
- `XIAO GND` -> `Wago C GND`

Add the pull-up:

- `4.7k resistor` between `Wago C 3V3` and `Wago C DATA`

### 5. Connect both DS18B20 sensors

For each sensor:

- `VDD` -> `Wago C 3V3`
- `DATA` -> `Wago C DATA`
- `GND` -> `Wago C GND`

Both sensors share the same `DATA` row.

### 6. Connect the INA219 logic pins

- `INA219 VCC` -> `XIAO 3V3`
- `INA219 SDA` -> `XIAO GPIO6`
- `INA219 SCL` -> `XIAO GPIO7`
- `INA219 GND` -> `Wago B GND`

### 7. Connect relay control

- `Relay IN` -> `XIAO GPIO3`
- `Relay VCC` -> `Wago B 5V`
- `Relay GND` -> `Wago B GND`

## One-Line Rule For Each Part

- `Battery/source`: feeds router power path and regulator input
- `Tobsun converter`: makes the `5V` bus from the battery/DC input
- `Wago B`: main low-voltage power distribution
- `XIAO`: creates `3V3` and all control signals
- `Wago C`: easy landing point for both temp sensors
- `INA219`: measures current on the positive router line
- `Relay`: switches router positive on/off

## XIAO Pin Map

- `GPIO2` -> `DS18B20 DATA bus`
- `GPIO3` -> `Relay IN`
- `GPIO6` -> `INA219 SDA`
- `GPIO7` -> `INA219 SCL`
- `3V3` -> `INA219 VCC` and both `DS18B20 VDD`

## Assembly Notes

- Put the `INA219` and relay close together.
- Put the `XIAO`, pull-up resistor, and `Wago C` close together.
- Use thicker wire for `SRC+`, `SRC-`, `ROUTER+`, and `ROUTER-`.
- Use smaller wire for `GPIO`, `I2C`, and sensor wiring.
- Label the two DS18B20 probes before final install.

## Common Mistakes To Avoid

- Do not feed battery/DC input voltage directly into the XIAO.
- Do not place the capacitors inline with the 5V wire.
- Do not pull the DS18B20 data line up to `5V`; pull it up to `3V3`.
- Do not leave module grounds separate; everything must share ground.

## Bench Checklist

- Power path wired: `BATTERY/DC IN + -> INA219 -> relay -> ROUTER+`
- Negative return wired: `BATTERY/DC IN - -> ROUTER-`
- Tobsun wired to battery/DC input
- `5V` and `GND` Wago built
- Capacitors across `5V` and `GND`
- XIAO powered from `5V/VIN`
- `3V3 / DATA / GND` sensor Wago built
- `4.7k` pull-up installed
- Both DS18B20 sensors landed on same bus
- Relay control on `GPIO3`
- INA219 I2C on `GPIO6/GPIO7`
