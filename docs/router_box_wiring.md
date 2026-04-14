# Router Box — Wiring Diagram & BOM

## Purpose
Detailed wiring connections for the `XIAO ESP32C3` dev board + sensors + INA219 + relay, using a mix of dev-board headers and Wago terminals.

## Safety / assumptions
- XIAO GPIOs are 3.3V logic. Power sensors and I2C pull-ups at **3.3V** unless you add proper level-shifting.
- Relay coil is recommended at 5V; keep relay coil supply separate from XIAO Vcc but share common GND.
- Common ground required between Battery, regulator output, XIAO, INA219, sensors, and relay module ground.

## Recommended relay
- Songle SRD-05VDC-SL-C on a driver module (with transistor/optocoupler) or a 5V relay module rated >=10A at 30VDC/250VAC. Ensure module input accepts 3.3V logic or include a transistor driver.

## BOM (core)
- 1x XIAO ESP32C3 dev board
- 1x INA219 I2C current sensor module
- 2x DS18B20 sensors (+ waterproof probes if needed)
- 1x 5V relay module (driver onboard) or relay + transistor
- 1x 12/24→5V regulator (Tobsun 5V 3A)
- 1x 470µF electrolytic capacitor
- 1x 0.1µF ceramic capacitor
- 1x 4.7k resistor (1/4W) for 1-wire pull-up
- Wago connectors / screw terminals

## Pin mapping (from your notes)
- XIAO GPIO2: DS18B20 1-wire data
- XIAO GPIO6: I2C SDA (INA219)
- XIAO GPIO7: I2C SCL (INA219)
- XIAO GPIO3: Relay control

## Power rails
- Battery + → 12/24 charger → regulator input
- Regulator 5V output → used for 5V devices (relay coil, optional 5V sensors)
- Use regulator output to feed a 3.3V regulator on the XIAO dev board (XIAO VIN → on-board regulator) OR feed XIAO USB 5V pin if using USB power. XIAO Vcc (3.3V) will be derived on-board.
- Place 470µF and 0.1µF across the 5V output near the Wago/XIAO power entry.

## Wiring table (concise)
- Battery (Power +) → INA219 VIN+
- INA219 VIN- → Relay COM
- Relay NO → Router +
- Router - → Battery - (common negative)
- Battery - → common GND (all devices)

- Regulator 5V OUT → Wago `5V` terminal
- Wago `5V` → Relay VCC (relay coil supply)
- Wago `GND` → Relay GND, INA219 GND, XIAO GND, DS18B20 GND

- INA219 VCC → XIAO 3.3V (or 5V if using 5V modules; prefer 3.3V)
- INA219 SDA → XIAO GPIO6
- INA219 SCL → XIAO GPIO7

- DS18B20 (box1) Data → wire to XIAO GPIO2 (bus)
- DS18B20 (box2) Data → same 1-wire bus to XIAO GPIO2
- DS18B20 VCC → 3.3V
- DS18B20 GND → GND
- Add 4.7k pull-up between 3.3V and the DS18B20 data line at the Wago/power entry point.

- Relay IN (signal) → XIAO GPIO3
- Relay VCC → 5V
- Relay GND → GND
- Relay COM → INA219 VIN- (per your earlier power flow)
- Relay NO → Router +
- (Optional) Relay NC → unused

## Wago terminal labeling (suggested)
- `Power+` (Battery + input)
- `5V` (Regulator 5V out)
- `3V3` (optional if distributing 3.3V separately)
- `GND`
- `Router+` (to router positive)
- `Router-` (to router negative)
- `DS18B20` (3-pin or single-wire + GND/3.3V)
- `BLE-Victron` (power & antenna location note)

## Example step-by-step wiring (high level)
1. Mount regulator in Box 2; wire battery + to regulator input and put capacitors across regulator output.
2. Connect regulator `5V` out to Wago `5V` terminal and to relay VCC.
3. Connect Wago `GND` to common ground bus in Box 2 and to Box1 ground.
4. Connect INA219 VIN+ to Battery + (before relay), INA219 VIN- to relay COM.
5. Connect relay NO to `Router+`; connect `Router-` to common GND.
6. Wire XIAO power: VIN or USB to regulated 5V so XIAO's on-board regulator makes 3.3V; connect XIAO GND to common GND.
7. Wire I2C: INA219 SDA/SCL → XIAO GPIO6/GPIO7. Use short, twisted pair if possible.
8. Wire DS18B20s in parallel to XIAO GPIO2; place 4.7k pull-up to 3.3V near the XIAO/Wago.
9. Wire XIAO GPIO3 to Relay IN (through level shifter or transistor if relay input requires 5V drive).

## Notes & gotchas
- If your relay module input requires a HIGH of 5V to release/activate and doesn't accept 3.3V, use a small NPN (2N2222) or N-channel MOSFET as a low-side driver, or choose a relay module advertised as 3.3V-compatible.
- INA219 can measure high-side current; follow module docs for shunt placement. Confirm shunt rating is suitable for router current.
- If you prefer all sensors at 5V, ensure XIAO I/O will be safe — DO NOT pull I/O above 3.3V. Instead, run pull-ups to 3.3V and power sensors that accept 3.3V.
- For Victron BLE: do not store encryption keys in plaintext on public repos. Keep key in `secrets` or Home Assistant secure storage.

## Next steps I can take now
- Produce a clear visual wiring SVG/PNG showing each wire and terminal mapping.
- Produce a PDF and a printable 1:1 sticker for box placement.

---
File: `docs/router_box_wiring.md`
