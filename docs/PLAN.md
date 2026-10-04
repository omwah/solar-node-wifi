# PLAN.md — Solar Node WiFi Telemetry Bridge

Firmware for a Seeed XIAO ESP32-C3 that takes weather and air-quality readings from Home Assistant
over MQTT and presents them to a Seeed SenseCAP Solar Node P1-Pro, running stock Meshtastic, as an I²C
air-quality sensor. The node broadcasts them to the mesh as standard telemetry. Firmware facts cited
here were checked against `meshtastic/firmware` @ `727d8c3`.

**Status:** design complete. Next: Phase 0 hardware spike when the hardware arrives.

---

## 1. Requirements

| ID | Requirement | Where |
|---|---|---|
| REQ-1 | **Bridge.** A XIAO ESP32-C3 delivers Home Assistant telemetry to a SenseCAP Solar Node P1-Pro running **stock, unmodified Meshtastic**, which broadcasts it as standard `AirQualityMetrics` telemetry on the default channel. | §2.4, §4.8 |
| REQ-2 | **Interface.** The C3 connects to the node's Grove port and emulates a **Sensirion SEN66 at I²C address `0x6B`**. | §2.1, §3, §3a |
| REQ-3 | **Build.** PlatformIO project on ESP-IDF; dependencies and tasks managed with **Pixi**. | §4.9 |
| REQ-4 | **Data source.** One retained JSON message on a **Mosquitto** broker, published by **Home Assistant** from the **AirGradient Open Air** and **Tempest** devices, using a generic, prefix-stripping publisher. | §4.2, `docs/mqtt_payload.md` |
| REQ-5 | **Fields.** PM1/2.5/4/10, CO2, VOC index, NOx index (AirGradient); temperature, humidity (Tempest). Missing or stale values are reported as unknown, never as zero or as an old value. | §2.3, §4.2 |
| REQ-6 | **Power.** The C3's I²C slave is always on; WiFi runs only during polls; the idle floor is minimised. The system must sustain itself on the node's solar and battery budget, and the C3 must not drain the node's battery. | §2.2, §4.3, §4.6, §6 |
| REQ-7 | **Unattended operation.** The node detects the C3 at every boot, and the C3 recovers from broker, network and power failures without intervention. | §2.2, §4.1, §4.4 |
| REQ-8 | **Remote management.** Configuration, status, logs, live sessions and OTA firmware updates with automatic rollback, all over MQTT, with no inbound connections. | §4.5 |
| REQ-9 | **Bench configuration.** Serial console over USB, with a host tool sharing the remote command set. | §4.5 |
| REQ-10 | **Testing.** Host unit tests, the stock Meshtastic driver run against the emulator, bench end-to-end tests on the real Solar Node, and a 72 h soak. | §5 |
| REQ-11 | **Power budget.** Analysis for a Southern California site: south-facing, ~45° tilt, unshaded. | §6 |
| REQ-12 | **Documentation.** Payload spec and HA blueprint, wiring guide, node configuration, Mosquitto ACL example. | §4.2, §4.7, §4.8, §9 |

## 2. Verified facts (the constraints)

### 2.1 Why I²C instead of UART
- All Meshtastic telemetry sensors are I²C (`TelemetrySensor.h`, `TwoWire *`). There is no UART telemetry parser.
- Solar Node Grove = 5 V, GND, SDA `P0.09`, SCL `P0.10`. UART1 is taken by the GNSS and UART2 does not exist
  (`variants/nrf52840/seeed_solar_node/variant.h:74-75,120-121,148-149`).

### 2.2 Boot-time detection decides the power design
- The firmware scans I²C **once**, in `setup()` (`main.cpp:677-688`), probes each address once, and never
  rescans (`I2C_NO_RESCAN` is set on this variant).
- A deep-sleeping C3 is invisible to that scan, and a missed scan lasts until the next node reboot.
  ⇒ **The C3's I²C slave must be running whenever the node boots.**
- The node reboots on its own a lot: power-up, config changes from the app, watchdog resets, firmware
  updates. An always-on slave covers every case **except a cold power-up where the nRF52 reaches its
  scan before the C3's slave is ready.** That race is the one to engineer for and measure (§4.1, Phase 0).
- No firmware-controlled switch exists for the Grove rail. It is assumed to be on whenever the battery
  is connected. Phase 0 verifies this.

### 2.3 What can reach the mesh
- The emulated SEN66 produces one `AirQualityMetrics` packet with: PM1/2.5/4/10, **CO2**, particle counts 0.5–10 µm,
  `pm_status_flags`, `pm_temperature`, `pm_humidity`, `pm_voc_idx`, `pm_nox_idx`
  (`SENXXSensor.cpp:1088-1196`).
- The C3 has one I²C controller with one slave address, so it emulates exactly one device. The
  emulated SEN66 is the only device on the Grove bus.

### 2.4 Meshtastic configuration facts
- Enable with `telemetry.air_quality_enabled = true`. There is no "sensor type" setting; detection is automatic.
- `air_quality_interval` defaults to **3600 s**. On the default channel it is forced to **≥ 1800 s**
  (`NodeDB.cpp:582-590`).
- With more than 40 online nodes, the interval is stretched for congestion unless the device role is
  `SENSOR`/`TRACKER` (`Default.cpp:41-60`). Sending is also skipped when channel utilisation is high.
  A busy SoCal mesh can hit both.
- The SENxx driver (shared by SEN5x and SEN6x) defaults to **one-shot mode**. Each cycle it wakes the sensor, waits through a
  15–30 s warmup, reads, then idles the sensor (SEN6x: full stop; SEN5x: RHT/gas-only mode).

### 2.5 ESP32-C3 facts that shape the design
- The C3 has AES/SHA/RSA/HMAC hardware acceleration, so TLS is affordable.
- RAM is the tight resource: no PSRAM, ~400 KB SRAM.
- RTC memory survives deep sleep and software reset, but **not power loss**.
- I²C cannot run in light sleep. The idle floor is the CPU clocked with the radio off.

## 3. Sensirion emulator contract: SEN5x baseline

The **deployed target is SEN66 @ `0x6B`**. This section is the shared baseline, written for the
SEN55 (the fallback target). §3a lists what differs for the SEN66, and §3a wins where they differ.

What stock firmware does at `0x69` (SEN55), in order:

1. **Scan ACK**: an empty write to `0x69`. Must ACK.
2. **Ruling out IMUs** (`ScanI2CTwoWire.cpp:1038-1068`): reads one byte from register `0x00`, then from `0x75`.
   Must **not** return `0xEA`, `0x24`, `0xD8` (from `0x00`) or `0x60`, `0x68` (from `0x75`). Return `0x00`.
3. **Probe**: `GET_PRODUCT_NAME 0xD014` → 32 data bytes + CRCs = **48 bytes**, ASCII `"SEN55"`, NUL-padded.
4. **`initDevice`**: `RESET 0xD304`, product name again, `GET_FIRMWARE_VERSION 0xD100` (major ≥ **2**),
   then a switch to idle.
5. **Run-time commands**:

| Cmd | Name | Response |
|---|---|---|
| `0x0021` | Start measurement | — (state → measuring) |
| `0x0037` | Start RHT/gas-only | — |
| `0x0104` | Stop measurement | — |
| `0x0202` | Read data-ready | 2 bytes + CRC; byte[1] = 1 when measuring |
| `0x03C4` | Read measured values | 8 words + CRC = 24 bytes: PM1, PM2.5, PM4, PM10 (×10, uint16); RH (×100, int16); T (×200, int16); VOC (×10); NOx (×10) |
| `0x0413` | Read PM + number concentrations | 10 words + CRC = 30 bytes: PM×4 (×10), then N0.5, N1.0, N2.5, N4.0, N10 (#/cm³ ×10, **cumulative and non-decreasing**), typical size (µm ×1000) |
| `0x6181` | VOC algorithm state read/write | read: 8 bytes + CRC; write: accept and store |
| `0x5607` | Fan cleaning | — |

Rules:
- CRC-8 after every 2 data bytes: polynomial `0x31`, initial value `0xFF`.
- "Unknown" values are `0xFFFF` (unsigned) or `0x7FFF` (signed). Use them for every null field.
- Particle counts must be **non-decreasing** or all unknown. The driver subtracts adjacent bins as
  unsigned 32-bit integers, so a decreasing pair wraps to a huge number.
- AirGradient's Plantower counts measure "> X µm"; Sensirion bins measure "0.3–X µm". **Default: send
  counts as unknown.** Only map them if a correct conversion is written and tested.
- Read buffers are double-buffered: the MQTT task builds a new snapshot and swaps a pointer, so a read
  never sees half-updated values.
- After every new command, clear the transmit buffer, so bytes left over from a short read are never
  sent as part of the next reply.
- **Survive a C3 reboot mid-cycle** (OTA, crash): the node keeps the sensor registered and does not
  re-run `initDevice`. After its own reboot, the emulator answers `READ_DATA_READY` = 1 and serves
  values in any state, rather than demanding a fresh `START_MEASUREMENT`. At worst the node loses one cycle.

## 3a. SEN66 target: differences from §3

Stock firmware also detects the **SEN6x family at `0x6B`** through the same `SENXXSensor` driver
(`SEN6XSensor.h`, `ScanI2CTwoWire.cpp:763-787`, registered unconditionally in `AirQualityTelemetry.cpp:115`).
Reporting product name `"SEN66"` enables `hasRHT`, `hasVOC`, `hasNOx` **and `hasCO2`** (`SENXXSensor.cpp:75-80`).

| | SEN55 @ `0x69` | SEN66 @ `0x6B` |
|---|---|---|
| PM1/2.5/4/10, T/RH, VOC, NOx | ✓ | ✓ |
| **CO2** | — | **✓** (AirGradient `carbon_dioxide`) |
| Particle counts 0.5–10 µm | ✓ | ✓ (the AirGradient doesn't supply them either way) |
| Typical particle size | ✓ | — (not supplied anyway) |
| `pm_status_flags` | — | ✓ (emulator reports 0 = OK) |
| Idle mode | RHT/gas-only | full stop |
| Extra warmup | — | CO2 24 s (`SEN6X_CO2_WARMUP_MS`) |

Differences in the emulator contract (§3) for SEN66:
- **Scan:** reads one byte from registers `0x0A`, `0x14`, `0x0F`. Must not return `0xC0` (`0x0A`), a
  value with low bits `0b10` (`0x14`), or `0x6A`/`0x6B` (`0x0F`). Return `0x00`.
- **Product name** `"SEN66"`; firmware version major ≥ 2.
- **Read measured values `0x0300`:** 9 words + CRC = 27 bytes: PM1, PM2.5, PM4, PM10 (×10), RH (×100),
  T (×200), VOC (×10), NOx (×10), CO2 (ppm, uint16).
- **Number concentrations `0x0316`:** 5 words + CRC = 15 bytes (all unknown by default).
- **Read device status `0xD206`:** 2 words + CRC = 6 bytes, all zero.
- Accept and acknowledge the SEN6x-only admin commands (temperature offset `0x60B2`, ASC `0x6711`,
  altitude `0x6736`, pressure `0x6720`, FRC `0x6707`) without effect. They're only sent if someone uses
  the Meshtastic admin screen for this sensor.
- No `0x0037` RHT/gas mode; `0x0104` stop and `0x0021` start only.

Fallback: if the SEN66 emulation runs into trouble, the SEN55 (§3) uses the same code with a different
model string, address and read layout. It loses only CO2.

## 4. Architecture

```
  Solar Node P1-Pro (stock Meshtastic, nRF52840)
     │  Grove: 5V, GND, SDA, SCL
     ▼
  XIAO ESP32-C3
     ├─ I²C slave @0x6B (SEN66 emulator)  — interrupt-driven, always on
     ├─ Snapshot cache (double-buffered, max-age enforced)
     ├─ Node watchdog → NODE_DOWN (light sleep, wake on I²C edge)
     ├─ [backup] battery ADC on D1 via 1M/1M divider → LOW_BATT
     ├─ Poller: WiFi on → SNTP → MQTT retained read → WiFi off
     ├─ Config: NVS; serial CLI over USB-CDC
     └─ Remote mgmt: MQTT cmd/resp/state topics, live session, OTA with rollback
     ▲
  Mosquitto  ◄── HA automation (publishes one retained JSON) ◄── AirGradient + WeatherFlow integrations
```

### 4.1 Boot sequence (order matters)
1. In `app_main`, before anything else: set up the I²C slave and serve "unknown" values. Goal: the slave
   answers **< 150 ms after power-on**. Phase 0 measures how long the nRF52 takes to reach its scan.
2. Load config from NVS. Start the serial CLI.
3. **Wait ≥ 10 s before the first WiFi start**, so the C3's WiFi startup current spike doesn't land on
   the node's own boot.
4. First poll, then the normal schedule.

### 4.2 MQTT payload — full spec in `docs/mqtt_payload.md`
Based on the reference HA install (`docs/home-assistant-entities.md`):
- **Generic publisher with prefix stripping.** An HA blueprint takes the AirGradient and Tempest
  **devices**, strips the device prefix (`st_00000000_`, `roof_airgradient_open_air_`), and publishes
  every numeric sensor under its suffix (`temperature`, `pm2_5`, `carbon_dioxide`). Prefix =
  `slugify(device name)_`, falling back to the longest common prefix. An optional rename map handles
  other languages and hand-renamed entities. Nothing user-specific is hard-coded.
- **Values are `[value, unit, last_reported]` triples.** The firmware converts units, because the
  reference install reports **°F**, and it checks freshness per value against SNTP time.
- The firmware matches **exact** keys (never suffix matches: `wet_bulb_temperature` ≠ `temperature`) and
  ignores everything it doesn't use.
- Topic `solarnode/<id>/telemetry`; `debug` + `debug_until`.
- Tempest comes from HA's core WeatherFlow integration. **No HACS `weatherflow2mqtt`.**

Field availability in the reference install:

| Source | Available | Reaches the mesh |
|---|---|---|
| AirGradient | `pm1`, `pm2_5`, `pm10`, `voc_index`, `nox_index` | yes |
| AirGradient | `carbon_dioxide` | yes |
| AirGradient | `pm0_3` (Plantower count), `temperature`, `humidity` | no (count can't be mapped) / fallback only |
| AirGradient | PM4, particle counts 0.5–10 µm | not reported → unknown |
| Tempest | `temperature`, `humidity` | yes (°F → °C) |
| Tempest | `irradiance` | `state` topic only |
| Tempest | all other sensors | no |

**Data check (R-17):** in the capture, `pm10` (2.17) < `pm2_5` (6.09), which is physically impossible
for the same air. This suggests HA's `pm2_5` is AirGradient's *corrected* value while `pm10` is raw.
Mesh consumers would see the same inconsistency. Check in the AirGradient integration/device settings;
the firmware forwards what it gets.

### 4.3 Poll schedule
- `poll_interval` default **600 s**, around the clock (configurable 120–3600 s). This costs ≲ 0.1 Wh/day (§6).
- `max_age` default **1800 s**. A block older than that is served as unknown.
- Each poll: start WiFi with stored PHY calibration and a static IP → SNTP (only if the last sync is
  over 6 h old or the clock is invalid) → MQTT connect → wait for the retained message (2 s timeout) →
  disconnect → `esp_wifi_stop()`.

### 4.4 Modes
| Mode | Entry | Behaviour |
|---|---|---|
| `COMMISSIONING` | No valid config, factory reset, or CLI command | Poll every 30 s; ends after 3 good polls or a **1 h timeout** |
| `NORMAL` | Default | Poll at `poll_interval` |
| `BACKOFF` | Connect/auth/parse failure | Interval doubles on each failure up to 6 h; resets on the first success. Auth errors jump straight to the maximum. |
| `SESSION` | `debug: true` in a valid payload, or a `session` command | WiFi and MQTT stay connected for interactive management; ends at `debug_until`, the requested length, or the 2 h cap |
| `NODE_DOWN` | No I²C traffic from the node for `node_silence_s` (§4.6) | WiFi off, light sleep, wake on an I²C line edge; any node traffic → `NORMAL` |
| `LOW_BATT` | Only if the battery-sense wire is fitted: battery < `batt_low_v` (§4.6) | Same as `NODE_DOWN`; leaves only when the battery is ≥ `batt_resume_v` **and** node traffic is seen |

Backstop: a hard cap on WiFi starts per day (default 400), whatever the mode.

### 4.5 Configuration and remote management

One command set, one parser (`cli/commands`), two transports: **USB serial** (bench) and **MQTT** (remote).
The C3 is inside the sealed enclosure on the roof (§4.7.3), so after deployment MQTT is the *only* way in.

**Why MQTT instead of SSH or BLE:**
- The C3 has WiFi off ~99% of the time. An SSH server can only accept connections inside a window
  that someone must open first. MQTT is **outbound-only**: commands wait at the broker and are
  delivered on the next poll. No port forwarding, works from anywhere you can reach the broker
  (HA, VPN).
- It reuses the MQTT client that's already there. No SSH server (wolfSSH ~40–100 KB RAM per session) and
  no BLE stack (~50 KB), on a C3 with no PSRAM.
- BLE range doesn't reach the roof from the house reliably, and it isn't remote.

**Topics** (`<id>` = node slug, e.g. `site-01`):

| Topic | Direction | Retain/QoS | Content |
|---|---|---|---|
| `solarnode/<id>/telemetry` | HA → C3 | retain, QoS 0 | Telemetry payload (§4.2) |
| `solarnode/<id>/cmd` | operator → C3 | no retain, **QoS 1** | `{"id": 42, "cmd": "set", "args": {...}}` |
| `solarnode/<id>/resp` | C3 → operator | no retain, QoS 1 | `{"id": 42, "ok": true, "result": {...}}` |
| `solarnode/<id>/state` | C3 → all | **retain**, QoS 0 | mode, firmware version, uptime, reset reason, `node_seen`, `last_node_activity`, WiFi RSSI, free heap, poll/fail counters, battery voltage if the battery-sense wire is fitted; published every poll |
| `solarnode/<id>/log` | C3 → operator | no retain, QoS 0 | contents of the RAM log buffer (ring buffer), sent on request or after an error |

- The C3 connects with a **persistent session** (`clean_session=false`, fixed client ID). The broker
  queues QoS 1 commands while the C3 is offline and delivers them on the next poll. Latency ≤ `poll_interval` (10 min).
- **Live session:** `session <minutes>` (or `debug: true` + `debug_until` in the payload) keeps WiFi/MQTT
  connected so commands round-trip in about 1 s. Default 30 min, capped at 2 h, ended early by `session end`.
- **Commands:** `status`, `get <key>`, `set <key> <value>`, `commit`, `poll-now`, `log [n]`, `reboot`,
  `session <min>|end`, `ota <url> <sha256>`, `factory-reset <confirm-token>`. Secrets are write-only on
  every transport.
- **OTA firmware update:** `esp_https_ota` from a LAN HTTP(S) server (HA's `/config/www` works). Two
  OTA app partitions. The image must match the given SHA-256. **Automatic rollback:** the new image stays
  "pending verify" until it has (1) served node I²C traffic **or** had the slave up for 15 min, and
  (2) completed one successful MQTT poll. A reset before that boots the previous image. OTA is
  refused below `batt_low_v` when the battery-sense wire is fitted. ESP32 secure boot with signed images: optional, decided in Phase 5.
- **Security:**
  - Mosquitto account per device, with ACLs. The device may read `telemetry` and `cmd` and write `resp`, `state` and `log`. Only the operator account may write `cmd`.
  - **TLS (port 8883, pinned CA)** is now recommended. The C3 has crypto hardware, and the handshake adds ~0.5–1 J per poll (≈ +0.04 Wh/day at 10-min polls), which matters because commands can change config and push firmware.
  - Plain 1883 stays available as a config option for a trusted VLAN.
- **Home Assistant:** optional MQTT Discovery messages, so HA auto-creates entities. Sensors from
  `state`, buttons for *Poll now*, *Reboot* and *Start 30-min session*. This makes the HA dashboard the
  remote-management UI.
- **Host tool:** `tools/remote.py` (paho-mqtt). Queues a command and waits for its `resp`, or with
  `--session` opens a live window and gives an interactive prompt. `tools/configure.py` (pyserial)
  stays for bench use; both share one command definition.
- **Out of scope:** remote management of the *Meshtastic node* itself. The C3 is only an I²C sensor to
  it. Use Meshtastic's own remote admin (admin key over the mesh) for the node.

### 4.6 Protecting the node's battery
The C3 draws power continuously and cannot see battery voltage on Grove. If Meshtastic shuts down on low
battery and the Grove rail stays on, the C3 (~16–32 mA at the battery) would pull the remaining charge
down to the cell-protection cutoff in about 1–3 days. If Phase 0 shows the rail turns off when the node
shuts down, this section is moot and both mechanisms stay disabled.

**Primary — node-activity watchdog (software only).**
- In one-shot mode the stock SENxx driver talks to the sensor every telemetry cycle (wake, ~15–30 s of
  reads, idle). On the default channel a cycle happens at least every 30–60 min.
- The I²C slave interrupt handler stamps `last_node_activity` on every transaction addressed to `0x6B`.
- Silence for `node_silence_s` → `NODE_DOWN`. Default **3 × 3600 s**; set it to 3× the node's actual
  `air_quality_interval`. If no node traffic is seen within 15 min of the C3's own power-up, also go to
  `NODE_DOWN`.
- In `NODE_DOWN`: WiFi off, GPIO wakeup set on SCL/SDA going low, then automatic light sleep
  (~0.15 mA at 3.3 V for the C3; the boost converter's and regulator's own idle draw then dominate).
- When the node reboots, its boot scan probes about 99 addresses before `0x6B`, roughly 10 ms at
  100 kHz. The C3 wakes in about 1 ms and must be ready to ACK and answer `GET_PRODUCT_NAME`.
- First transaction to `0x6B` → back to `NORMAL`, with the normal ≥ 10 s delay before WiFi (§4.1).
- Side benefit: if telemetry is disabled on the node, or the node never detected the C3, the C3 stops
  spending power for no consumer. `status` on the CLI shows `node_seen` and `last_node_activity`.
- Not used outside `NODE_DOWN`: during normal operation, light sleep would risk NAKing a driver read
  mid-cycle. The node is the only one that knows when it will read.

**Backup — battery-sense wire (fitted only if the watchdog fails its Phase 0 gate).**
- Hardware: a 1 MΩ / 1 MΩ divider from the node's battery + to XIAO `D1` (GPIO3, ADC1), with 100 nF from the
  pin to GND. Max 4.2 V → 2.1 V at the pin; divider drain ≈ 2 µA. Avoid `D0`/GPIO2 (a boot-strapping
  pin). Shared GND through Grove.
- Firmware (always built, enabled by `batt_adc_enabled`): a calibrated ADC reading (`esp_adc` oneshot + `adc_cali`, 12 dB attenuation,
  16-sample average) every 60 s and before every WiFi start. Below `batt_low_v` (default **3.40 V**)
  → `LOW_BATT`. Resume needs ≥ `batt_resume_v` (default **3.65 V**, hysteresis) **and** node traffic.
  Thresholds to be set above Meshtastic's own shutdown voltage once it's measured in Phase 0.
- No WiFi start is allowed with a reading below `batt_low_v`, whatever the mode (including `SESSION`/`COMMISSIONING`).
- If the wire is fitted, it **adds to** the watchdog; it does not replace it.

### 4.7 Wiring deliverables (`docs/wiring.md`)

The project delivers a wiring guide in two parts: the **Grove harness** (always built) and the
**battery-sense wire** (built only if the watchdog fails its Phase 0 gate). Each part has a pin table, an ASCII diagram,
a bill of materials, build steps, and a **pre-connection checklist** with multimeter readings to take
before the C3 is ever plugged in. Photos of the finished harness are added after Phase 0.

#### 4.7.1 Grove harness: Grove plug on the Solar Node side, bare wires soldered to the XIAO

Cable: a Seeed Grove 4-pin cable cut in half (or a "Grove to 4-pin female jumper" cable with the
jumper ends cut off). Strip ~3 mm, tin, and solder to the XIAO's castellated pads.

| Grove pin | Usual wire colour | Signal | XIAO ESP32-C3 pad | ESP32-C3 GPIO |
|---|---|---|---|---|
| 1 | yellow | SCL | `D5` | GPIO7 |
| 2 | white | SDA | `D4` | GPIO6 |
| 3 | red | 5 V | `5V` (via the Schottky diode, see below) | — |
| 4 | black | GND | `GND` | — |

```
 Solar Node Grove socket                          XIAO ESP32-C3
 ┌───────────┐
 │ 1 SCL yel ├──────────────────────────────────────► D5  (GPIO7)
 │ 2 SDA wht ├──────────────────────────────────────► D4  (GPIO6)
 │ 3 5V  red ├──── DS1 ─►|──────────────┬──────────► 5V
 │ 4 GND blk ├───────────────────────────│──┬───────► GND
 └───────────┘  DS1 = 1N5817 / SS14      │  │
                 (band toward the XIAO)  └─CB─┘  CB = 470 µF/10 V, at the XIAO

   If Phase 0 measures 5 V on SDA/SCL, insert a BSS138 level shifter on lines 1 and 2:
   HV side = Grove 5 V (before the diode) + SCL/SDA;  LV side = XIAO 3V3 + D5/D4;  shared GND.
```

Rules in the guide:
- **Power source:** the C3 runs from Grove 5 V only if the Phase 0 sag test passes (no node reset with a
  350 mA pulse during LoRa TX). Otherwise it gets its own cell and regulator, and Grove carries SDA, SCL and
  GND only.
- **Verify the colours with a continuity tester.** Grove colours are a convention, not a guarantee;
  pin 1 is marked on the Grove plug. The pin numbers win over the colours.
- **Schottky diode in the 5 V line** (1N5817 or SS14, 1 A; band toward the XIAO). The XIAO's `5V` pad
  is wired straight to USB VBUS. Without the diode, plugging USB in on the bench backfeeds the Solar Node's
  5 V rail from the PC, or the node's rail into the PC. The ~0.3 V drop is harmless ahead of the
  XIAO's 3.3 V regulator.
- **Bulk capacitor**: 470 µF / 10 V low-ESR electrolytic across `5V`–`GND` at the XIAO, to absorb the
  WiFi startup current spike (R-3). Value confirmed by the Phase 0 sag test.
- **No pull-up resistors on the C3 side.** The Solar Node already provides them; Phase 0 confirms their
  presence and voltage.
- **Level shifter depends on Phase 0** (R-2): if SDA/SCL idle at 3.3 V, wire directly; if they idle
  at 5 V, the BSS138 module is mandatory. The C3's GPIOs are not 5 V tolerant.
- The XIAO mounts **inside the Solar Node enclosure** (§4.7.3), so the harness is short: cut to 10–15 cm.
- Strain relief: heat-shrink over each joint, then hot glue or a cable tie anchoring the cable to
  the board.

Pre-connection checklist (Solar Node on, XIAO **not** connected):
1. Continuity: each Grove pin → its intended XIAO pad; no short between any pair.
2. Voltage at the free harness end: pin 3 → GND ≈ 5 V; pins 1 and 2 → GND = the pull-up voltage
   (record it: this decides the level shifter).
3. Diode orientation: 5 V appears at the XIAO side of the diode; with only USB plugged into the
   XIAO, **no** voltage appears back on Grove pin 3.

#### 4.7.2 Battery-sense wire (built only if the watchdog fails its Phase 0 gate)

| From | Component | To |
|---|---|---|
| Solar Node battery **+** (exact point identified in Phase 0 from the Seeed schematic/board: battery-holder + terminal or a BAT test pad) | **R1 1 MΩ, soldered right at the battery end** | sense wire |
| sense wire | — | XIAO `D1` (GPIO3) |
| XIAO `D1` | **R2 1 MΩ** | XIAO `GND` |
| XIAO `D1` | **C1 100 nF** ceramic | XIAO `GND` |

```
 Battery + ──[R1 1M]──┬──────── sense wire ────────┬── D1 (GPIO3)
  (at the cell)       │                            ├─[R2 1M]── GND
          heat-shrink over R1                      └─[C1 100n]─ GND
                                                    (R2, C1 at the XIAO)
 Ground reference: the Grove GND wire (no separate ground wire)
```

Rules in the guide:
- **R1 goes at the battery end**, inside heat-shrink. A chafed or shorted sense wire then carries at
  most ~4 µA instead of shorting the cells. No fuse is needed with R1 placed this way.
- **Never connect the battery wire to the XIAO's `BAT` pads.** Those are the XIAO's own charger input.
- Use `D1`/GPIO3 only. Avoid `D0` (GPIO2, boot-strapping) and `D8`/`D9` (GPIO8/9, boot-strapping).
- The sense wire is a 5th conductor, routed entirely inside the enclosure; 26–28 AWG, tied
  along the Grove harness, kept away from the LoRa and GNSS antenna feeds.
- Calibration step: measure the battery with a multimeter, compare to the CLI `status` reading, and
  store a correction factor with `set batt_cal`.

Pre-connection checklist (XIAO **not** connected):
1. Voltage at the free end of the sense wire, under a 1 MΩ load to GND (R2): about half the battery voltage
   (≈ 1.6–2.1 V). Without the load it reads the full battery voltage on a 10 MΩ meter; that is expected.
2. Short-circuit current, sense wire to GND, through a meter on the µA range: ≤ 5 µA.

#### 4.7.3 Mounting inside the Solar Node enclosure
Part of `docs/wiring.md`:
- **Placement:** the XIAO is fixed with double-sided foam tape or a nylon standoff, with Kapton under it so
  no pad can touch the node's PCB or battery holder. Away from the battery pack and the GNSS module (≥ 5 cm).
- **WiFi antenna:** the XIAO's U.FL FPC antenna is stuck to the inside of a plastic wall facing the access
  point. ≥ 5 cm from the LoRa antenna feed and the GNSS antenna. Never against metal or the battery pack.
- **No new holes:** the enclosure's IP rating is kept by reseating its gasket. An external RP-SMA
  antenna through a bulkhead is the fallback only if the site survey fails (R-11).
- **Heat:** the interior of a sunlit enclosure can reach 60–70 °C. The ESP32-C3 is rated to 85 °C. The
  cells, not the C3, are the limit. The C3 adds about 0.1 W.
- **Checks (Phase 0/6):** room inside the enclosure; WiFi RSSI with the lid **closed** ≥ −75 dBm at
  the mounting spot; GNSS satellite count / SNR with the C3 running vs. off (R-14); IP gasket intact after reassembly.
- **USB access requires opening the enclosure.** After deployment, all management is remote (§4.5).

### 4.8 Solar Node configuration — `docs/node_config.md`

Configuration only; the Meshtastic firmware stays stock. Delivered as `docs/node_config.md` with Meshtastic CLI
commands, the equivalent app screens, and a read-back check.

**Order matters:** choosing a role applies that role's defaults, and only at the moment the role
changes (`AdminModule.cpp:909-912`, `NodeDB.cpp:1703-1708`). So set the role first, let the node
reboot, then apply the rest. Otherwise the role defaults overwrite them.

| Step | Setting | Value | Why |
|---|---|---|---|
| 1 | `lora.region` | `US` | 915 MHz band |
| 1 | channel 0 | default (LongFast, default PSK) | Public telemetry any Meshtastic client can decode |
| 2 | `device.role` | **`SENSOR`** | No congestion stretching, relaxed channel-util gate, `RELIABLE` priority; still relays like `CLIENT` |
| 3 | `power.is_power_saving` | **`false`** | With `SENSOR`, Power Saving deep-sleeps the node, radio included (`BaseTelemetryModule.h:22`) |
| 3 | `telemetry.air_quality_enabled` | `true` | Enables the SEN6X reads |
| 3 | `telemetry.air_quality_interval` | `1800` | Shortest the default channel allows (`NodeDB.cpp:582-590`) |
| 3 | `telemetry.environment_measurement_enabled` | `false` | The `SENSOR` role turns it on; no environment sensor is fitted |

```
meshtastic --set lora.region US
meshtastic --set device.role SENSOR            # node reboots
meshtastic --set power.is_power_saving false \
           --set telemetry.air_quality_enabled true \
           --set telemetry.air_quality_interval 1800 \
           --set telemetry.environment_measurement_enabled false
meshtastic --get device.role --get power.is_power_saving --get telemetry   # read back
```

Why not `ROUTER`: router roles force a ≥ 12 h telemetry interval on the default channel
(`Default.h:17`), which would cut air-quality reports to twice a day. `SENSOR` relays like `CLIENT`:
it holds back when another node has already relayed a packet, and waits until routers have had their turn.

Side effects to know about:
- `SENSOR` marks the node **unmessagable** (`NodeDB.cpp:1704-1705`). It still relays other people's messages.
- The C3's `node_silence_s` (§4.6) defaults to 3 × 3600 s. With `air_quality_interval = 1800`, set it
  to **5400 s**.
- **Verification:** after a reboot, the node log shows `SEN6X found` + `SEN6X: found sensor model SEN66`, and
  another client receives an `AirQualityMetrics` packet within about 30 min.

### 4.9 Build and toolchain

| Item | Choice |
|---|---|
| Framework | **ESP-IDF 6.1** via PlatformIO platform `espressif32 @ 7.1.3` (pinned in `platformio.ini`) |
| Board | `seeed_xiao_esp32c3` |
| Host tools | Pixi (`pixi.toml`, `pixi.lock`): Python 3.12, PlatformIO, pip (for ESP-IDF's Python env), and on Linux the host GCC used by the native tests. macOS uses the system clang. |
| Tasks | `pixi run build` / `test` / `flash` / `monitor` / `clean` |
| Portable code | `lib/` (e.g. `lib/senxx`), so the `native` env compiles it for host tests; ESP-IDF glue lives in `src/` |
| Flash layout | `partitions.csv`: NVS, two 1.875 MB OTA slots, 64 KB core dump (4 MB flash) |
| sdkconfig | `sdkconfig.defaults` is the source of truth (console on USB-Serial-JTAG, OTA rollback, core dump to flash); the generated `sdkconfig.*` is not committed |
| I²C slave API | ESP-IDF's `i2c_slave.h` driver (`on_receive` / `on_request` callbacks); internal pull-ups off (the node provides them) |
| CI | `.github/workflows/ci.yml`: `pixi install --locked`, `pixi run test`, `pixi run build` |

## 5. Test strategy

Pure emulation can't cover the whole path: Espressif's QEMU models neither an I²C slave nor WiFi, and
Meshtastic's Linux build can't run the Solar Node variant. So the software is tested on the host (T1, T2),
and anything involving real I²C timing is tested against the **real Solar Node**, whose nRF52 is the
actual I²C master with the actual driver (T3, T4).

| Level | What | Proves |
|---|---|---|
| **T1 host unit** | C3 emulator logic (commands, CRC, encoding, staleness, mode machine) built as a PlatformIO `native` test env | Protocol and logic correctness |
| **T2 driver-in-the-loop (host)** | Compile Meshtastic's real `SENXXSensor.cpp` + `ScanI2CTwoWire` probe code against a fake `TwoWire` connected to the T1 emulator, in a test harness outside the firmware tree | The stock driver accepts the emulator, with no firmware change |
| **T3 bench end-to-end** | Real C3 on the bench harness, plugged into the real Solar Node (USB-powered, serial log captured). Local Mosquitto + scripted publisher (`test/bench/publisher.py`) drive the payload and inject faults. A second Meshtastic device on USB runs `test/bench/receiver.py` (Meshtastic Python API), which asserts that the expected `AirQualityMetrics` values arrive over the mesh. | Real ESP32 slave timing against the real master; MQTT → I²C → mesh end to end; fault handling |
| **T4 soak** | The same node on battery and solar with the final harness, logged for 72 h | Boot race, rail stability, power draw, and reboots of every kind |

Run T1/T2 in CI via `pixi run test`. T3 runs from `pixi run bench` on the bench PC.

## 6. Power budget

### 6.1 Loads (at the battery)
Conversion path: battery → 5 V boost (~85%) → XIAO linear regulator 5 → 3.3 V (~66%), so **each 1 mA at
3.3 V ≈ 5.9 mW at the battery**.

| Load | Estimate | Wh/day |
|---|---|---|
| C3 idle floor, CPU at 40–80 MHz, radio off, I²C slave active: **10–20 mA** @ 3.3 V | 59–118 mW | **1.4–2.8** |
| WiFi polls every 10 min, **1–3 J each** at the battery (conservative) | 144 × 1–3 J | **0.04–0.12** |
| Live management session, 30 min WiFi on, ~100 mA @ 3.3 V | occasional | ~0.3 per session (2 h max ≈ 1.2) |
| TLS on every poll (+0.5–1 J each, 144/day) | if enabled | ~0.02–0.04 |
| C3 in `NODE_DOWN` (light sleep + regulator/boost idle draw, est. 0.5–1 mA at the battery) | only while the node is down | ~0.05–0.1 |
| Solar Node itself (idle 10.65 mA per Seeed + LoRa TX + GNSS) | estimate | 1–5 |
| **Total (normal)** | | **≈ 2.5–8** |

The idle floor dominates, and polling frequency barely matters. Effort goes into the floor: lower clock
speed, clock-gating unused peripherals, no LEDs, and maybe replacing the XIAO's linear regulator with a
3.3 V buck (−25–30%).

### 6.2 Supply
- Battery: 4 × 18650 3350 mAh, **1S4P** → 49 Wh nominal, ~39 Wh usable (80%).
- Site: Southern California (~34°N), panel south-facing at ~45° tilt, unshaded. Exact coordinates are a
  runtime input to `tools/power_budget.py`, never committed.
- Harvest: 5 W × PSH × 0.7. Southern California at 45° south: roughly **20 Wh/day annual average, 14–16 Wh/day in
  December**. To be replaced by **NREL PVWatts** figures in `tools/power_budget.py`.

### 6.3 Results
- **Sustainability**: December harvest (14 Wh) vs. load (2.5–8 Wh) → it sustains itself with ≥ 1.75× margin.
- **Storm survival**: 39 Wh ÷ 2.5–8 Wh/day → **5–16 days** with no sun (the C3 cuts this roughly in half).
- **Heat**: Li-ion charging stops above ~45 °C. Model summer enclosure temperature; hot days may yield
  little harvest. The C3 adds about 0.1 W of heat.
- Phase 0 measurements replace every estimate here.

## 7. Phases and exit gates

| Phase | Work | Exit gate |
|---|---|---|
| **0 — Hardware spike** | Build the bench Grove harness from the **draft `docs/wiring.md` §4.7.1** (level shifter fitted until the SDA/SCL voltage is known). Identify the battery + point for the battery-sense wire. Measure: Grove SDA/SCL idle voltage (3.3 V or 5 V?), Grove 5 V sag at a 350 mA pulse while the node transmits, nRF52 power-on → scan time, C3 idle current. Minimal C3 slave at `0x6B` answering the SEN66 probe with a **48-byte** reply. **D-3 checks:** (1) whether the Grove 5 V rail stays on during Meshtastic low-battery shutdown, and the battery voltage at which shutdown happens; (2) SDA/SCL levels while the node is shut down: they must stay high, or the GPIO wakeup will fire constantly; (3) the C3 in light sleep with GPIO wakeup answers the boot scan. | Node logs `SEN6X found` + `found sensor model SEN66` on 20/20 cold boots and 20/20 warm reboots; **20/20 detections from a light-sleeping C3** and no false wakes over 1 h of node shutdown, otherwise build and fit the battery-sense wire (§4.7.2); level shifter and capacitor decisions made and recorded in `docs/wiring.md`; Grove power confirmed or switched to a separate C3 cell (§4.7.1) |
| **1 — Scaffold** ✓ | `pixi.toml`, `platformio.ini` (`seeed_xiao_esp32c3` + `native` test env), partition table, `sdkconfig.defaults`, CI workflow; Sensirion CRC-8 as the first tested module | `pixi run build`/`test` green |
| **2 — Emulator** | Full §3/§3a contract; T1 + T2 | Stock `SENXXSensor` passes init + 100 read cycles in T2 |
| **3 — Poller & cache** | WiFi/SNTP/MQTT, payload parser, staleness, modes, serial CLI + `tools/configure.py`, `docs/mqtt_payload.md` + HA automation example | T3 end-to-end green; fault injection (broker down, bad auth, stale `ts`, malformed JSON) behaves as specified in §4.4; `NODE_DOWN` entry/exit on simulated node silence; `LOW_BATT` with a bench supply on the ADC pin |
| **4 — Power** | Lower the floor; `tools/power_budget.py` with PVWatts + measured values | Measured floor ≤ 12 mA @ 3.3 V (stretch: 8 mA); budget report |
| **5 — Remote management** | MQTT cmd/resp/state/log topics, persistent session, live session, OTA with rollback, TLS, Mosquitto ACL example, HA Discovery, `tools/remote.py` | A `set` queued while the C3 is offline is applied on the next poll; OTA of a good image succeeds and a deliberately broken image rolls back by itself; RAM headroom ≥ 40 KB during a TLS session |
| **6 — Field** | Final `docs/wiring.md` (photos, measured values, final BOM); T4 72 h soak with the final harness, then roof deploy | Someone other than the author builds a harness from the guide and passes the pre-connection checklist; no missed detections, no node resets attributable to the C3 |
| **7 — Optional** | ESP32 secure boot with signed OTA images | — |

## 8. Risk register

| # | Risk | Impact | Mitigation |
|---|---|---|---|
| R-1 | C3 slave hardware: 32-byte transmit buffer vs. 48-byte replies, leftover bytes, clock stretching, reply ready within 20 ms during WiFi activity | **High**: no detection at all | Phase 0 spike first; interrupt-driven reply staging; fallbacks: SEN55 (`0x69`), then PMSA003I (`0x12`, 32-byte frame, PM only) |
| R-2 | Grove lines pulled up to 5 V destroy C3 GPIOs | **High** | Measure in Phase 0; BSS138 level shifter if needed |
| R-3 | Grove rail sags on WiFi startup current and resets the node | **High** | Phase 0 pulse test; bulk capacitor; delayed WiFi start; separate C3 cell |
| R-4 | Cold-boot race: nRF52 scans before the C3 slave is ready | Medium | Slave set up first in `app_main`; measure; 20/20 gate |
| R-5 | C3 drains the battery after Meshtastic shuts down | Medium | §4.6: `NODE_DOWN` watchdog; battery-sense wire if Phase 0 shows the watchdog is unreliable |
| R-12 | Light-sleeping C3 misses the node's boot scan after recovery → C3 stays undetected until the next node reboot | Medium | Phase 0 20/20 gate; if it fails, stay awake (WiFi off) in `NODE_DOWN` and rely on the battery-sense wire for the cutoff |
| R-13 | USB plugged into the XIAO while it's on the Grove harness backfeeds 5 V between the PC and the node | Medium | Schottky diode in the harness (§4.7.1); checklist step 3 |
| R-6 | Idle floor higher than estimated | Medium | Phase 4; buck regulator; the budget has margin |
| R-7 | Other HA installs name entities differently (device names, language, manual renames) | Low | Prefix stripping by device + rename map (§4.2); exact-key matching in firmware |
| R-17 | HA's `pm2_5` and `pm10` look like they come from different corrections (pm10 < pm2.5) | Low–Med | Check the AirGradient settings; document in `docs/mqtt_payload.md` |
| R-8 | Channel utilisation or congestion suppresses sends on a busy mesh | Low | `SENSOR` role (§4.8): no congestion scaling, relaxed channel-util gate, `RELIABLE` priority |
| R-16 | Someone enables Power Saving on the `SENSOR` node → node deep-sleeps, radio included, stops relaying and reboots repeatedly | Medium | `docs/node_config.md` warning; verification step reads back `power.is_power_saving = false` |
| R-9 | Clients may not display `pm_temperature`/`pm_humidity` | Low | Check Android/iOS/web. If they don't show, temperature/humidity still reach anything that decodes the packet (MQTT gateways, HA, the CLI). |
| R-10 | RAM: WiFi + MQTT + TLS + OTA on a C3 with no PSRAM | Low | No SSH server or BLE stack; RAM gate in Phase 5 |
| R-11 | WiFi range from inside the closed enclosure on the roof | Medium | §4.7.3 antenna placement; lid-closed RSSI ≥ −75 dBm survey; fallback: bulkhead RP-SMA antenna |
| R-14 | C3 noise or 2.4 GHz harmonics degrade GNSS reception (L76K) in the shared enclosure | Low–Med | Distance ≥ 5 cm; compare satellite count / SNR with the C3 on vs. off |
| R-15 | A bad OTA image bricks a device nobody can reach | **High** | Rollback rule (§4.5); SHA-256 check; test a broken image in Phase 5 |

## 9. Project layout

```
solar-node-wifi/
├── pixi.toml, pixi.lock        # host tools and tasks
├── platformio.ini              # envs: xiao_esp32c3, native (tests)
├── CMakeLists.txt              # ESP-IDF project file
├── partitions.csv              # two OTA slots + core dump
├── sdkconfig.defaults
├── .github/workflows/ci.yml
├── docs/
│   ├── PLAN.md                 # this document
│   ├── mqtt_payload.md         # payload spec
│   ├── home-assistant-entities.md  # reference HA entity list
│   ├── ha_automation.yaml      # example publisher
│   ├── wiring.md               # Grove harness, battery-sense wire, in-enclosure mounting
│   ├── mosquitto_acl.example   # per-device ACL
│   └── node_config.md          # Solar Node Meshtastic settings
├── lib/                        # portable code, built for both target and host tests
│   ├── senxx/                  # crc, emulator, encode, sen66, sen55 (SEN66 target, SEN55 fallback)
│   ├── payload/                # JSON parse, unit conversion, freshness
│   └── modes/                  # mode machine, backoff, node watchdog logic
├── src/                        # ESP-IDF glue
│   ├── CMakeLists.txt
│   ├── main.cpp                # boot order per §4.1
│   ├── i2c_slave.{h,cpp}       # i2c_slave.h driver ↔ senxx emulator
│   ├── poller/{wifi,sntp,mqtt}.{h,cpp}
│   ├── node_watch.{h,cpp}      # light sleep + GPIO wake
│   ├── battery.{h,cpp}         # optional battery-sense interlock
│   ├── config.{h,cpp}          # NVS
│   ├── cli/{serial,commands}.{h,cpp}
│   └── mgmt/{mqtt_cmd,state,ota,ha_discovery}.{h,cpp}
├── tools/
│   ├── configure.py            # serial (bench)
│   ├── remote.py               # MQTT remote management
│   └── power_budget.py
└── test/
    ├── native/                 # T1 (PlatformIO Unity tests)
    ├── driver_harness/         # T2: real SENXXSensor vs fake TwoWire; fetches meshtastic/firmware
    │                           #     @ 727d8c3 into vendor/ at build time (gitignored)
    └── bench/                  # T3: mosquitto config, publisher.py, receiver.py
```

