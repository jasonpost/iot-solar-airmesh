# Router Box Wiring Schematic

This is the practical wiring layout for building the router box with dev boards, Wago connectors, and loose wiring.

## Visual top-down picture

Open this drawing for the clearest view:

![Router Box Top-Down Wiring](C:/Users/jason/Projects/iot/iot-solar-airmesh/docs/router_box_topdown.svg)

Capacitor placement rule:

- The `470uF` capacitor is not inline with the 5V feed.
- The `0.1uF` capacitor is not inline with the 5V feed.
- Both capacitors bridge across `5V` and `GND` in parallel, close to the XIAO power entry and 5V Wago distribution point.

### Capacitor hookup only

```text
CORRECT:

5V rail  --------------------+--------------------> to XIAO 5V/VIN
                             |
                             +---- (+) 470uF (-) ----+
                             |                       |
                             +---- 0.1uF ceramic ----+
                                                     |
GND rail --------------------+-----------------------> to XIAO GND


WRONG:

5V rail ---- capacitor ----> XIAO
```

Plain English:

- One leg of the `470uF` goes to `5V`, and the other leg goes to `GND`.
- One leg of the `0.1uF` goes to `5V`, and the other leg goes to `GND`.
- They both connect to the same two nodes: `5V` and `GND`.
- The `470uF` electrolytic is polarized:
  - capacitor `+` lead -> `5V`
  - capacitor `-` lead -> `GND`
- The `0.1uF` ceramic capacitor is typically non-polarized, so either direction is fine.

## 1. Main DC power path

```text
BATTERY / DC SOURCE
  + -------------------------------------> INA219 VIN+
                                             INA219 VIN- -----> Relay COM
                                                                Relay NO -----> Router +

  - -------------------------------------------------------------------------> Router -
  |
  +-----> Regulator IN-

BATTERY / DC SOURCE +
  |
  +-----> Regulator IN+
```

Router power is switched on the positive side:

- Source `+` -> `INA219 VIN+`
- `INA219 VIN-` -> `Relay COM`
- `Relay NO` -> `Router +`
- Source `-` -> `Router -`

That lets the INA219 measure router current while the relay disconnects router power.

## 2. Low-voltage control power

```text
12V/24V -> 5V REGULATOR

5V OUT  -----> Wago 5V bus -----> XIAO 5V/VIN
                               -> Relay VCC
                               -> 470uF capacitor (+)
                               -> 0.1uF capacitor

GND OUT -----> Wago GND bus ---> XIAO GND
                               -> Relay GND
                               -> INA219 GND
                               -> DS18B20 #1 GND
                               -> DS18B20 #2 GND
                               -> 470uF capacitor (-)
                               -> 0.1uF capacitor
```

Important:

- Feed the `XIAO ESP32C3` from regulated `5V` into its `5V`/`VIN` input, not from the battery directly.
- Use the XIAO `3V3` pin only as the sensor logic supply rail.
- Every module must share the same ground.

## 3. XIAO signal wiring

```text
XIAO ESP32C3

GPIO2  -----> DS18B20 data bus
GPIO3  -----> Relay IN
GPIO6  -----> INA219 SDA
GPIO7  -----> INA219 SCL
3V3    -----> INA219 VCC
3V3    -----> DS18B20 #1 VDD
3V3    -----> DS18B20 #2 VDD
GND    -----> Common GND bus
```

## 4. DS18B20 bus wiring

Both temperature sensors sit on the same 1-wire bus:

```text
XIAO GPIO2 ----------------+-----> DS18B20 #1 DATA
                           |
                           +-----> DS18B20 #2 DATA
                           |
                           +-----> 4.7k resistor -----> XIAO 3V3

XIAO 3V3  -----------------+-----> DS18B20 #1 VDD
                           +-----> DS18B20 #2 VDD

XIAO GND  -----------------+-----> DS18B20 #1 GND
                           +-----> DS18B20 #2 GND
```

Use a 3-position Wago or terminal group for:

- `3V3`
- `DATA`
- `GND`

Then land both sensor cables into that same group.

## 5. INA219 wiring

```text
INA219 POWER / I2C

INA219 VCC  -----> XIAO 3V3
INA219 GND  -----> Common GND
INA219 SDA  -----> XIAO GPIO6
INA219 SCL  -----> XIAO GPIO7

INA219 CURRENT PATH

Source +       -----> INA219 VIN+
INA219 VIN-    -----> Relay COM
```

Use the INA219 as a high-side current sensor. Keep its logic side at `3.3V` so the I2C pull-ups stay safe for the XIAO.

## 6. Relay wiring

```text
RELAY MODULE (Songle-style module)

Relay VCC -----> Wago 5V bus
Relay GND -----> Wago GND bus
Relay IN  -----> XIAO GPIO3

Relay COM -----> INA219 VIN-
Relay NO  -----> Router +
Relay NC  -----> Not used
```

Important relay note:

- Many 5V relay modules work with a 3.3V control signal on `IN`, but not all do.
- If your relay module does not trigger reliably from the XIAO, add a transistor or MOSFET driver stage, or use a relay board advertised as `3.3V logic compatible`.

## 7. Suggested Wago grouping

If you are building this with loose parts, this grouping is clean and easy to service:

### Wago group A: incoming source

- `SRC+`
- `SRC-`

### Wago group B: regulator output

- `5V`
- `GND`

### Wago group C: XIAO/sensor logic

- `3V3`
- `1WIRE`
- `GND`

### Wago group D: switched router output

- `ROUTER+`
- `ROUTER-`

## 8. One-page connection list

### High current / power path

- `Source +` -> `INA219 VIN+`
- `INA219 VIN-` -> `Relay COM`
- `Relay NO` -> `Router +`
- `Source -` -> `Router -`

### Regulator

- `Source +` -> `Regulator IN+`
- `Source -` -> `Regulator IN-`
- `Regulator 5V OUT` -> `XIAO 5V/VIN`
- `Regulator 5V OUT` -> `Relay VCC`
- `Regulator GND` -> common ground

### XIAO to modules

- `GPIO2` -> both `DS18B20 DATA`
- `GPIO3` -> `Relay IN`
- `GPIO6` -> `INA219 SDA`
- `GPIO7` -> `INA219 SCL`
- `3V3` -> `INA219 VCC`
- `3V3` -> both `DS18B20 VDD`
- `GND` -> `INA219 GND`, relay ground, both sensor grounds

### Passive parts

- `4.7k resistor` between `GPIO2 / 1WIRE bus` and `3V3`
- `470uF capacitor` across `5V` and `GND`
- `0.1uF capacitor` across `5V` and `GND`

## 9. Full plain-text schematic

```text
HIGH CURRENT PATH
=================

   Source/Battery +
          |
          +---------------------------> INA219 VIN+
                                           |
                                           +------> INA219 VIN-
                                                      |
                                                      +------> Relay COM
                                                                 |
                                                                 +------> Relay NO
                                                                            |
                                                                            +------> Router +

   Source/Battery ----------------------------------------------------------> Router -


REGULATOR / LOW VOLTAGE POWER
=============================

   Source/Battery + ---------------------> Regulator IN+
   Source/Battery - ---------------------> Regulator IN-

   Regulator 5V OUT ---------------------> Wago 5V bus
                                            |-> XIAO 5V/VIN
                                            |-> Relay VCC
                                            |-> 470uF capacitor (+)
                                            +-> 0.1uF capacitor

   Regulator GND ------------------------> Wago GND bus
                                            |-> XIAO GND
                                            |-> Relay GND
                                            |-> INA219 GND
                                            |-> DS18B20 #1 GND
                                            |-> DS18B20 #2 GND
                                            |-> 470uF capacitor (-)
                                            +-> 0.1uF capacitor


XIAO SIGNALS AND SENSOR POWER
=============================

   XIAO 3V3 -----------------------------> INA219 VCC
   XIAO GPIO6 ---------------------------> INA219 SDA
   XIAO GPIO7 ---------------------------> INA219 SCL

   XIAO GPIO3 ---------------------------> Relay IN

   XIAO GPIO2 ---------------------------> DS18B20 bus DATA
                                            |-> DS18B20 #1 DATA
                                            +-> DS18B20 #2 DATA

   XIAO 3V3 -----------------------------> 4.7k resistor
                                            |
                                            +-> DS18B20 bus DATA pull-up

   XIAO 3V3 -----------------------------> DS18B20 #1 VDD
                                            +-> DS18B20 #2 VDD

   XIAO GND -----------------------------> DS18B20 #1 GND
                                            +-> DS18B20 #2 GND


RELAY TERMINALS
===============

   Relay VCC  -> 5V bus
   Relay GND  -> GND bus
   Relay IN   -> XIAO GPIO3
   Relay COM  -> INA219 VIN-
   Relay NO   -> Router +
   Relay NC   -> not used
```

## 10. Build advice for dev-board construction

- Put the `5V` and `GND` distribution on separate Wago groups first, then branch outward.
- Keep the high-current router path wires thicker than the signal wires.
- Keep the INA219 and relay physically close to each other because they sit in the switched positive path.
- Keep the XIAO, DS18B20 pull-up resistor, and INA219 I2C wiring fairly short.
- Label the two DS18B20 probes before sealing the box so you know which one is Box 1 and Box 2.

## 11. What is not included

This is a wiring schematic, not a manufactured PCB schematic. Since you are assembling with dev boards and connectors, this is the right level to wire it by hand. If you want, the next useful step is a box-layout drawing that shows exactly which Wago block each wire lands in.
