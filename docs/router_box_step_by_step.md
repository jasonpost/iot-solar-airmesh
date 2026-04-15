# Router Box Step-By-Step Assembly

This is the simplest build order for assembling the router box by hand.

In this guide:

- `Battery/DC input` means the higher-voltage power coming into the system before the Tobsun
- `Tobsun 5V output` means the regulated `5V` coming out of the Tobsun converter

Use this together with:

- [Simple schematic](C:/Users/jason/Projects/iot/iot-solar-airmesh/docs/router_box_schematic.md)
- [Simple layout SVG](C:/Users/jason/Projects/iot/iot-solar-airmesh/docs/router_box_simple_layout.svg)

## Before You Start

Parts used in this guide:

- `XIAO ESP32C3`
- `INA219`
- `5V relay module`
- `2x DS18B20`
- `12/24V -> 5V regulator`
- `470uF capacitor`
- `0.1uF capacitor`
- `4.7k resistor`
- `Wago connectors`

Suggested Wago groups:

- `Wago A` = `BATTERY/DC IN +`, `BATTERY/DC IN -`
- `Wago B` = `5V`, `GND`
- `Wago C` = `3V3`, `DATA`, `GND`
- `Wago D` = `ROUTER+`, `ROUTER-`

## Step 1: Build The Battery/DC Input Wago

Make the first landing point for incoming power.

- Put battery or main DC input positive into `Wago A BATTERY/DC IN +`
- Put battery or main DC input negative into `Wago A BATTERY/DC IN -`

When done, you should have one easy place to grab the incoming DC power before it reaches the Tobsun converter.

## Step 2: Build The Router Power Path

Wire the high-current switched path first.

- `Wago A BATTERY/DC IN +` -> `INA219 VIN+`
- `INA219 VIN-` -> `Relay COM`
- `Relay NO` -> `Wago D ROUTER+`
- `Wago A BATTERY/DC IN -` -> `Wago D ROUTER-`
- `Wago D ROUTER+` -> `Router +`
- `Wago D ROUTER-` -> `Router -`

When done, the router power path should be:

`Battery/DC input + -> INA219 -> Relay -> ROUTER+ -> Router`

and

`Battery/DC input - -> ROUTER- -> Router`

## Step 3: Wire The 5V Regulator Input

Now feed the regulator from the same source input.

- `Wago A BATTERY/DC IN +` -> `Tobsun IN+`
- `Wago A BATTERY/DC IN -` -> `Tobsun IN-`

When done, the Tobsun converter is connected to the incoming DC power but its 5V output is not yet distributed to the rest of the system.

## Step 4: Build The 5V / GND Power Wago

Create the low-voltage distribution point.

- `Tobsun 5V OUT +` -> `Wago B 5V`
- `Tobsun 5V OUT -` -> `Wago B GND`

When done, `Wago B` becomes your main low-voltage power bus.

## Step 5: Power The XIAO

Use the Tobsun 5V output, not the battery/DC input voltage.

- `Wago B 5V` -> `XIAO 5V` or `VIN`
- `Wago B GND` -> `XIAO GND`

When done, the XIAO is powered from the Tobsun and can create the `3V3` logic rail.

## Step 6: Add The Relay Power Wires

Power the relay module from the 5V bus.

- `Wago B 5V` -> `Relay VCC`
- `Wago B GND` -> `Relay GND`

When done, the relay has coil power and shared ground.

## Step 7: Add The INA219 Logic Wires

Now wire the low-voltage logic side of the INA219.

- `XIAO 3V3` -> `INA219 VCC`
- `Wago B GND` -> `INA219 GND`
- `XIAO GPIO6` -> `INA219 SDA`
- `XIAO GPIO7` -> `INA219 SCL`

When done, the INA219 is connected on both sides:

- power path on `VIN+ / VIN-`
- logic path on `VCC / GND / SDA / SCL`

## Step 8: Add The Capacitors Across The 5V Bus

These are parallel across power, not in series.

- `470uF +` -> `Wago B 5V`
- `470uF -` -> `Wago B GND`
- one leg of `0.1uF` -> `Wago B 5V`
- other leg of `0.1uF` -> `Wago B GND`

When done, both capacitors are across the same two Wago B terminals.

## Step 9: Build The Sensor Wago

Create a small patch point for the temperature sensors.

- `XIAO 3V3` -> `Wago C 3V3`
- `XIAO GPIO2` -> `Wago C DATA`
- `XIAO GND` -> `Wago C GND`

When done, `Wago C` is your sensor landing strip.

## Step 10: Add The DS18B20 Pull-Up Resistor

The data bus must be pulled up to `3V3`.

- One side of `4.7k resistor` -> `Wago C 3V3`
- Other side of `4.7k resistor` -> `Wago C DATA`

When done, the shared temperature data line has the required pull-up.

## Step 11: Connect DS18B20 Sensor #1

- `Sensor #1 VDD` -> `Wago C 3V3`
- `Sensor #1 DATA` -> `Wago C DATA`
- `Sensor #1 GND` -> `Wago C GND`

When done, sensor #1 is fully wired.

## Step 12: Connect DS18B20 Sensor #2

- `Sensor #2 VDD` -> `Wago C 3V3`
- `Sensor #2 DATA` -> `Wago C DATA`
- `Sensor #2 GND` -> `Wago C GND`

When done, both sensors share the same 1-wire bus.

## Step 13: Connect Relay Control

Add the final control wire.

- `XIAO GPIO3` -> `Relay IN`

When done, the XIAO can switch the relay.

## Step 14: Final Check Before Powering Up

Verify these items:

- `Battery/DC input voltage` does not go directly to the XIAO
- `Tobsun 5V output` goes to the XIAO `5V/VIN`
- `Router +` goes through `INA219` and `Relay`
- `Router -` goes straight back to battery/DC input negative
- `INA219 VCC` is on `3V3`, not battery/DC input voltage
- `DS18B20 DATA` is pulled up to `3V3` through `4.7k`
- Both capacitors are across `5V` and `GND`
- All module grounds are common

## Fast Troubleshooting

If the relay does not switch:

- Check `Relay VCC` really has `5V`
- Check `Relay GND` is common with the XIAO
- Check whether the relay module accepts `3.3V` logic on `IN`

If the temperature sensors do not show up:

- Check both sensors share the same `DATA` row
- Check the `4.7k` resistor is between `DATA` and `3V3`
- Check sensor ground is actually connected

If the INA219 does not read:

- Check `SDA` is on `GPIO6`
- Check `SCL` is on `GPIO7`
- Check `VCC` is on `3V3`
- Check the high-current path is really passing through `VIN+` and `VIN-`
