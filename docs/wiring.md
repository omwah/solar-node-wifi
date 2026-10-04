# Wiring

Two parts: the **Grove harness** (always built) and the **battery-sense wire** (built only
if the node-activity watchdog fails its Phase 0 test). Take every reading in the
checklists **before** the XIAO is connected for the first time.

Items marked *Phase 0* are measured with the steps in `docs/bench_testing.md`; copy the
results here once that guide's results sheet is filled in.

| Phase 0 result | Value |
|---|---|
| Grove SDA/SCL idle voltage | *Phase 0* (decides the level shifter) |
| Grove 5 V sag at 350 mA during LoRa TX | *Phase 0* (decides Grove power vs. a separate cell) |
| Grove 5 V during Meshtastic low-battery shutdown | *Phase 0* |
| SDA/SCL levels while the node is shut down | *Phase 0* |
| Battery + solder point | *Phase 0* |

## 1. Grove harness

Grove plug on the Solar Node side, bare wires soldered to the XIAO ESP32-C3.

### Parts

| Qty | Part | Note |
|---|---|---|
| 1 | Seeed Grove 4-pin cable, cut in half (or Grove to female jumper, ends cut off) | 10–15 cm after cutting |
| 1 | Schottky diode, 1N5817 or SS14 (1 A) | 5 V line, band toward the XIAO |
| 1 | 470 µF / 10 V low-ESR electrolytic | across 5V–GND at the XIAO |
| 1 | BSS138 bidirectional level shifter module | only if SDA/SCL idle at 5 V |
| — | Heat-shrink, Kapton tape, foam tape | |

### Pin map

| Grove pin | Usual colour | Signal | XIAO pad | ESP32-C3 GPIO |
|---|---|---|---|---|
| 1 | yellow | SCL | `D5` | GPIO7 |
| 2 | white | SDA | `D4` | GPIO6 |
| 3 | red | 5 V | `5V` through the diode | — |
| 4 | black | GND | `GND` | — |

Grove colours are a convention. Pin 1 is marked on the plug; the pin numbers win.

```
 Solar Node Grove socket                          XIAO ESP32-C3
 ┌───────────┐
 │ 1 SCL yel ├──────────────────────────────────────► D5  (GPIO7)
 │ 2 SDA wht ├──────────────────────────────────────► D4  (GPIO6)
 │ 3 5V  red ├──── DS1 ─►|──────────────┬──────────► 5V
 │ 4 GND blk ├───────────────────────────│──┬───────► GND
 └───────────┘  DS1 = 1N5817 / SS14      │  │
                 (band toward the XIAO)  └─CB─┘  CB = 470 µF/10 V, at the XIAO

   If SDA/SCL idle at 5 V, insert the BSS138 module on lines 1 and 2:
   HV = Grove 5 V (before the diode) + SCL/SDA;  LV = XIAO 3V3 + D5/D4;  shared GND.
```

### Rules

- **Diode.** The XIAO's `5V` pad is wired to USB VBUS. Without the diode, plugging USB
  in on the bench backfeeds 5 V between the PC and the Solar Node.
- **Capacitor.** Absorbs the WiFi start-up current spike so it can't reset the node.
- **No pull-ups on the XIAO side.** The node provides them; the firmware leaves the
  ESP32's internal pull-ups off.
- **Level shifter.** ESP32-C3 pins are not 5 V tolerant. Fit it if SDA/SCL idle at 5 V.
- **Power source.** Grove 5 V only if the Phase 0 sag test passes. Otherwise power the
  XIAO from its own cell and regulator and connect only SDA, SCL and GND.
- **Strain relief.** Heat-shrink each joint; anchor the cable to the board.

### Build

1. Cut the cable, strip 3 mm, tin the wires.
2. Solder the diode into the red wire (band toward the XIAO end), heat-shrink it.
3. Solder yellow to `D5`, white to `D4`, black to `GND`, the diode to `5V`.
4. Solder the capacitor across `5V` (+) and `GND` (−) on the XIAO.
5. Run the checklist.

### Checklist (Solar Node on, XIAO not connected)

1. Continuity: each Grove pin to its XIAO pad; no short between any pair.
2. At the free harness end: pin 3 to GND ≈ 5 V. Pins 1 and 2 to GND = pull-up
   voltage; record it in the table above.
3. With only USB plugged into the XIAO, Grove pin 3 shows **no** voltage.

## 2. Battery-sense wire

Build only if the watchdog fails its Phase 0 test. Enables the `LOW_BATT` interlock
(`set batt_enabled true`).

### Parts

| Qty | Part |
|---|---|
| 2 | 1 MΩ resistor, 1 % |
| 1 | 100 nF ceramic capacitor |
| — | 26–28 AWG wire, heat-shrink |

### Connections

| From | Component | To |
|---|---|---|
| Battery + (point identified in Phase 0) | **R1 1 MΩ, at the battery end** | sense wire |
| sense wire | — | XIAO `D1` (GPIO3) |
| XIAO `D1` | R2 1 MΩ | XIAO `GND` |
| XIAO `D1` | C1 100 nF | XIAO `GND` |

```
 Battery + ──[R1 1M]──┬──────── sense wire ────────┬── D1 (GPIO3)
  (at the cell)       │                            ├─[R2 1M]── GND
          heat-shrink over R1                      └─[C1 100n]─ GND
 Ground reference: the Grove GND wire.
```

### Rules

- **R1 at the battery end.** A chafed wire can then pass only a few µA; no fuse needed.
- **Never use the XIAO's `BAT` pads**: they are the XIAO's own charger input.
- **`D1` only.** `D0`, `D8` and `D9` are boot-strapping pins.
- Route the wire inside the enclosure, tied along the Grove harness, away from the LoRa
  and GNSS antennas.

### Checklist (XIAO not connected)

1. Sense wire to GND through a 1 MΩ load: about half the battery voltage (1.6–2.1 V).
2. Sense wire shorted to GND through a meter on the µA range: ≤ 5 µA.

### Calibration

Measure the battery with a multimeter, compare with `battery_v` from `status`, then
`set batt_cal <measured / reported>` and `commit`.

## 3. Mounting inside the enclosure

- XIAO on foam tape or a nylon standoff, Kapton underneath; ≥ 5 cm from the battery
  pack and the GNSS module.
- WiFi FPC antenna on the inside of a plastic wall facing the access point, ≥ 5 cm from
  the LoRa and GNSS antennas, never against metal or the cells.
- No new holes. Reseat the gasket when closing.
- Check with the lid closed: `rssi` in `status` ≥ −75 dBm; GNSS satellite count with the
  XIAO running vs. unplugged.
