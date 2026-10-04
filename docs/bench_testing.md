# Bench testing

A step-by-step guide to the bench tests that complete **Phase 0** (the hardware spike in
`PLAN.md` §7), followed by the remaining bench checks and a **pre-deployment checklist** to
finish before the Solar Node goes on the roof.

Work through the steps in order. Each step says what to do, what you should see, and what
to write in the [results sheet](#results-sheet) at the end. Phase 0 decides the
harness's final wiring: the level shifter, the power source and the battery-sense wire. Until it
is done, treat `docs/wiring.md` as a draft.

---

## 0. What you need

**Hardware**

| Item | Used for |
|---|---|
| SenseCAP Solar Node P1-Pro | the device under test |
| Seeed XIAO ESP32-C3 with its WiFi antenna | the bridge |
| A second Meshtastic device (any board on `US`, default channel) | receives the node's packets on the mesh |
| Adjustable bench power supply, 0–5 V / 2 A, with current readout | stands in for the node's cells: cold boots, low-battery shutdown |
| Multimeter with a µA range | voltages, continuity, idle current |
| Oscilloscope | Grove 5 V sag test |
| Logic analyzer (optional) | boot-scan timing |
| 15 Ω ≥ 5 W resistor and a push switch (or an electronic load) | 350 mA pulse load |
| Harness parts from `docs/wiring.md` §1, **including the BSS138 level shifter** | bench harness |
| Two 1 MΩ resistors, a 100 nF capacitor | battery-sense wire, only if step 11 calls for it |
| Three USB cables | XIAO, Solar Node and the receiver on the PC |

**Host and network**

- A Linux PC with [Pixi](https://pixi.sh). The bench broker (`pixi run -e bench broker`) is
  Linux-only; on macOS, point the bridge at any other Mosquitto broker on the LAN.
- A 2.4 GHz WiFi network the PC and the XIAO can both reach.

**Safety**

- Remove the 18650 cells and disconnect the solar panel before connecting the bench supply
  to the battery-holder terminals. Never connect both at once.
- Set the bench supply's current limit to 1 A.
- Don't connect the XIAO to the Grove port until step 3's harness checklist passes.

---

## 1. Prepare the host

```
pixi install
pixi install -e bench
pixi run test && pixi run driver-test && pixi run build
```

All three must pass before you touch the hardware. They prove the emulator works against
Meshtastic's own driver on the PC, so any failure on the bench comes from the hardware.

Find the serial ports. Plug in one device at a time and run `ls /dev/ttyACM*` after each.
Note which port is the XIAO, which is the Solar Node and which is the receiver. The
examples below use `/dev/ttyACM0` (XIAO), `/dev/ttyACM1` (Solar Node) and
`/dev/ttyACM2` (receiver).

## 2. Prepare the Solar Node

1. **Power it from the bench supply.** Take out the cells, disconnect the panel, and connect
   the supply to the battery-holder + and − terminals at **3.90 V**.
2. **Find the battery + point** for a possible battery-sense wire: the battery-holder +
   terminal or a BAT test pad, using Seeed's schematic or the board itself. Photograph it.
   → *Results: battery + point.*
3. **Configure the node** as in `docs/node_config.md`: role first, wait for the reboot, then
   the rest. Run the read-back command and check that `power.is_power_saving` is `false`.
4. **Note the node id** (`!xxxxxxxx`):
   ```
   pixi run -e bench meshtastic --port /dev/ttyACM1 --info | grep -i '"id"'
   ```
5. **Open the node's log:**
   ```
   pixi run pio device monitor -p /dev/ttyACM1 -b 115200
   ```
   Reboot the node once (`pixi run -e bench meshtastic --port /dev/ttyACM1 --reboot`) and
   check that the boot log, including the I²C scan, scrolls past. You'll check this log for
   detection throughout. The Meshtastic CLI and the monitor can't share the port, so close
   the monitor before each CLI command.

## 3. Build the bench harness

1. Build the Grove harness from `docs/wiring.md` §1 **with the BSS138 level shifter
   fitted**. Don't leave it out until step 3.3 shows it's safe. Keep it long enough to
   reach the bench (30 cm is fine); you'll cut the final harness to 10–15 cm later.
2. With the node on and the XIAO **not** connected, run the harness checklist in
   `docs/wiring.md` §1: continuity, the voltage at the free end, and the diode direction.
3. **SDA/SCL idle voltage.** Measure Grove pins 1 and 2 to GND with the node on.
   → *Results: SDA/SCL idle voltage.*
   - **About 3.3 V:** the level shifter isn't needed. Rebuild without it, or bypass it.
   - **About 5 V:** the level shifter stays. The C3's pins are not 5 V tolerant.
   - **About 0 V:** the node has no pull-ups on Grove. Stop here; the design assumes them.

## 4. First detection with the Phase 0 build

The Phase 0 build serves fixed values (PM1 1, PM2.5 2, PM10 3 µg/m³, CO2 450 ppm, VOC 100,
NOx 1, 21.5 °C, 45 %RH). It has no WiFi and no console, so the I²C slave is the only
thing under test.

1. With the XIAO on USB only, flash it:
   ```
   pixi run flash-phase0
   ```
2. Unplug the XIAO from USB and plug the harness into the node's Grove port.
3. Power-cycle the node: bench supply off, wait 5 s, on.
4. In the node log, look for both lines:
   ```
   SEN6X found ...
   SEN6X: found sensor model SEN66
   ```
5. **Check the mesh.** With the receiver on USB:
   ```
   pixi run -e bench receive -- --port /dev/ttyACM2 --from '!xxxxxxxx' --expect phase0
   ```
   It waits up to 45 minutes for the node's next air-quality packet and prints `PASS` if
   the values match.

If the lines don't appear, check the wiring against the pin map (SDA and SCL swapped is
the usual mistake), then see R-1 in `PLAN.md` §8. The fallback is the SEN55 at `0x69`.

## 5. Detection reliability: 20 cold boots and 20 warm reboots

Keep the XIAO **off USB** so it's powered from Grove. It then starts at the same moment as
the node, which is the boot race this step tests.

1. **Cold boots, 20 times:** bench supply off, wait 5 s, on. Check the node log for both
   detection lines. Tally the passes.
2. **Warm reboots, 20 times:** `pixi run -e bench meshtastic --port /dev/ttyACM1 --reboot`,
   then check the log. The XIAO stays powered through these.

→ *Results: cold boots __/20, warm reboots __/20.* The gate is 20/20 for each. A single
miss means the C3 isn't ready before the node scans: go to step 6 and measure the timing.

## 6. Boot timing (logic analyzer, optional unless step 5 failed)

Connect channel 0 to Grove 5 V (through a divider if your analyzer is 3.3 V only),
channel 1 to SCL and channel 2 to SDA, with an I²C decoder on 1 and 2. Trigger on the
5 V rising edge and power the node on.

Measure:
- **5 V rise → first I²C transaction to `0x6B`**: how long the node takes to reach its scan.
- **5 V rise → the C3's first ACK at `0x6B`**: the target is under 150 ms.

→ *Results: node scan time, C3 ready time.* The C3's ready time must be well below the
node's scan time.

## 7. Grove power

### 7.1 Sag under a 350 mA pulse

This stands in for the C3's WiFi start-up current. It decides whether the C3 can run
from Grove 5 V.

1. XIAO off USB, on the harness, with the 470 µF capacitor fitted.
2. Scope on the XIAO's `5V` pad. Connect the 15 Ω resistor and switch across `5V` and `GND`
   at the XIAO.
3. Make the node transmit:
   `pixi run -e bench meshtastic --port /dev/ttyACM1 --sendtext test`.
   Close the switch for about 1 s while the packet goes out. Repeat 10 times.
4. Check the node didn't reset: no new boot banner in its log.

→ *Results: minimum 5 V during the pulse, node resets (should be 0).*
- **No resets:** Grove power is confirmed.
- **Resets:** try a 1000 µF capacitor. If it still resets, the C3 gets its own cell and
  regulator, and the harness carries SDA, SCL and GND only (`docs/wiring.md` §1).

### 7.2 C3 idle current

Measure this with the full firmware (step 8) in `NORMAL` mode between polls, WiFi off,
and the XIAO off USB. Put the multimeter in series with the Grove 5 V (red) wire.

→ *Results: idle current.* The Phase 4 target is ≤ 12 mA (stretch goal 8 mA). The
XIAO's regulator is linear, so the current at 5 V is about the same as at 3.3 V.

## 8. Full firmware on the bench broker

1. Start the broker and the synthetic Home Assistant publisher, each in its own terminal:
   ```
   pixi run -e bench broker
   pixi run -e bench publish -- --host <PC's IP> --node site-01
   ```
2. XIAO on USB, unplugged from Grove. Flash the full firmware and configure it:
   ```
   pixi run flash
   pixi run configure -- set wifi_ssid "<bench network>"
   pixi run configure -- set wifi_pass "<password>"
   pixi run configure -- set mqtt_host <PC's IP>
   pixi run configure -- set node_id site-01
   pixi run configure -- set node_silence_s 600
   pixi run configure -- commit
   pixi run configure -- poll-now
   pixi run configure -- status
   ```
   `node_silence_s 600` is the shortest allowed. It lets the light-sleep tests (steps 9 and 10)
   start after 10 minutes instead of 90. `status` should show `clock_valid: true`, one poll,
   no failures, and a `payload` block.
3. Unplug USB, plug the harness into the node, and power-cycle the node. Both detection
   lines should appear in the node log.
4. **Check the mesh with real payload values:**
   ```
   pixi run -e bench receive -- --port /dev/ttyACM2 --from '!xxxxxxxx'
   ```
   `PASS` means the whole path works: MQTT → C3 → I²C → node → mesh.

From here on, read the C3's state over MQTT. Opening the USB port can reset the XIAO,
which would clear its log.

```
pixi run remote -- --host <PC's IP> --node site-01 --state      # last published state
pixi run remote -- --host <PC's IP> --node site-01 log 40       # waits for the next poll
```

## 9. Detection from a light-sleeping C3: 20 trials

When the node stops reading for `node_silence_s`, the C3 enters `NODE_DOWN` and light
sleeps. When the node reboots, its boot scan must wake the C3, and the C3 must answer in time.

Each trial:
1. Power-cycle the node. Both detection lines appear, and the node reads the sensor once.
2. Wait **12 minutes**. The C3 goes to `NODE_DOWN` after 10 minutes of silence; the state
   topic shows `mode: NODE_DOWN`, or `log` shows `node down: light sleep up to ...`.
3. **Reboot the node before its next 30-minute read**:
   `pixi run -e bench meshtastic --port /dev/ttyACM1 --reboot`. Check for both detection lines.

Reboot before the scheduled read because a periodic read that lands on a sleeping C3
isn't part of this test. In service, `node_silence_s` is three read intervals, so the C3
only sleeps when the node has stopped reading.

→ *Results: light-sleep detections __/20.* The gate is 20/20. 20 trials take about 4 hours.

## 10. Low-battery shutdown

This tells you what the C3 sees when Meshtastic shuts the node down on low battery. Keep
the XIAO **off USB**. Otherwise USB keeps it powered and hides what the rail does.

1. Queue a log request so it's answered when the node comes back:
   ```
   pixi run remote -- --host <PC's IP> --node site-01 --wait 0 log 40
   ```
2. Lower the bench supply in 0.05 V steps, waiting 1 minute at each, until the node shuts
   down (its log stops and the radio goes quiet). → *Results: shutdown voltage.*
3. With the node shut down, measure:
   - **Grove 5 V** at the harness. → *Results: does the rail stay on?*
   - **SDA and SCL** to GND. → *Results: line levels during shutdown.* They must stay
     high, or the C3's wake-on-edge fires all the time. The firmware detects lines held low,
     logs `I2C bus held low while the node is down; not sleeping` and stays awake.
   - **Supply current** with the node down, C3 included. → *Results: shutdown current.*
4. Leave it like this for **1 hour**. The C3 should enter `NODE_DOWN` and stay asleep.
5. Raise the supply back to 3.90 V. The node boots, the C3 wakes, its next poll answers the
   queued `log 40`, and the reply prints in the terminal from step 1. (If that terminal
   has closed, run the command again without `--wait 0`.)
6. Count the `node down: light sleep up to ...` lines in the log. Each line is one wake.
   Expect one when the C3 entered `NODE_DOWN` and at most one more from the hourly
   heartbeat (`heartbeat_s` can't go lower than 3600). → *Results: wakes in 1 h.*

## 11. Decide on the battery-sense wire

| What steps 9 and 10 showed | Decision |
|---|---|
| The Grove 5 V rail **turns off** when the node shuts down | The C3 can't drain the battery. Neither the watchdog nor the sense wire is needed; leave `batt_enabled` off. |
| The rail stays on, 20/20 light-sleep detections, no false wakes | The watchdog is enough. Don't fit the sense wire. |
| The rail stays on and detection < 20/20, false wakes, or SDA/SCL low during shutdown | Build and fit the battery-sense wire (below). |

**If the wire is needed:**
1. Build it from `docs/wiring.md` §2 at the battery + point from step 2.2, and run its
   checklist.
2. Enable and calibrate it: measure the supply voltage with the multimeter and compare
   it with `battery_v` in `status`.
   ```
   pixi run configure -- set batt_enabled true
   pixi run configure -- status
   pixi run configure -- set batt_cal <measured / battery_v>
   pixi run configure -- commit
   ```
3. Set the thresholds above the step 10 shutdown voltage: `batt_low_v` at least 0.1 V
   above it, `batt_resume_v` at least 0.2 V above `batt_low_v`.
4. **Test `LOW_BATT`:** lower the supply below `batt_low_v`. The mode becomes `LOW_BATT`
   and polls stop. Raise it above `batt_resume_v`. The mode returns to `NORMAL` once the
   node reads the sensor again.

## 12. Record the results

1. Fill in the [results sheet](#results-sheet).
2. Update `docs/wiring.md`: remove the level shifter if it isn't needed, set the
   capacitor value, set the power source, fill in the battery + point, and mark the
   sense wire fitted or not.
3. If `PLAN.md` §6 or §8 assumed something that turned out wrong, correct it there.

### Phase 0 exit gate

| Gate | Step | Pass |
|---|---|---|
| Detection on 20/20 cold boots | 5 | ☐ |
| Detection on 20/20 warm reboots | 5 | ☐ |
| Detection on 20/20 boots from a light-sleeping C3 | 9 | ☐ |
| No false wakes over 1 h of node shutdown, or the battery-sense wire fitted and its `LOW_BATT` test passed | 10, 11 | ☐ |
| Level shifter decision made and recorded | 3 | ☐ |
| Grove power confirmed, or the C3 moved to its own cell | 7.1 | ☐ |
| Capacitor value confirmed | 7.1 | ☐ |

---

## 13. Remaining bench checks

These close the hardware gates of Phases 3–5. Use the same setup as step 8.

### 13.1 Fault handling (Phase 3)

Run each publisher scenario and check the C3 (`--state` or `status`) and the mesh:

| Run | Expected |
|---|---|
| `publish -- --scenario stale` | values older than `max_age_s`; the node reports no PM/CO2 values |
| `publish -- --scenario nulls` | both blocks null; same as above |
| `publish -- --scenario celsius` | temperature still 25.25 °C on the mesh |
| `publish -- --scenario bad-units` | CO2 and humidity unknown; the other fields normal |
| `publish -- --scenario malformed` | `malformed_payloads` goes up; the last good values expire after `max_age_s` |
| `publish -- --scenario oversize` | same as malformed |
| Stop the broker | `poll_failures` goes up and `backoff_s` doubles each time |
| Wrong `mqtt_pass` on a broker with authentication | backoff jumps straight to 6 h |
| `publish -- --scenario debug` | `live session started`; `--shell` works until `debug_until` |

Run `publish -- --scenario normal` again afterwards, and `--scenario clear` when you're done.

### 13.2 Remote management (Phase 5)

1. **Queued command:** with the C3 between polls, queue a setting change:
   `remote ... --wait 0 set poll_interval_s 300`, then `remote ... --wait 0 commit`.
   At the next poll, `get poll_interval_s` returns 300.
2. **Live session:** `remote ... session 10`, then `remote ... --shell`. Commands answer in
   about a second. `session end` closes it.
3. **OTA, good image:** serve the build from the PC:
   ```
   cd .pio/build/xiao_esp32c3 && python -m http.server 8000
   sha256sum firmware.bin
   ```
   Send `remote ... ota http://<PC's IP>:8000/firmware.bin <sha256>`, then `reboot`.
   `status` shows `ota: pending verification`, then `new image verified` appears in `log`
   after the node has been seen and one poll has succeeded.
4. **OTA, rollback:** install the image again and reboot, then stop the broker straight
   away so no poll can succeed. After 30 minutes, `log` shows
   `new image not verified in time; rolling back`, and the C3 reboots into the previous slot.
   Restart the broker; the C3 recovers by itself.
5. **RAM with TLS:** enable TLS on a broker set up per `docs/mosquitto_acl.example`
   (`set mqtt_tls true`, `set mqtt_port 8883`). During a live session, `min_free_heap`
   in `status` stays ≥ 40 KB.

### 13.3 Power (Phase 4)

1. Measure the idle current as in step 7.2 with the final settings.
2. Run the budget with your measured values and your coordinates (never commit them):
   ```
   pixi run power-budget -- --c3-floor-ma <idle mA> --lat <lat> --lon <lon>
   ```
   December harvest must exceed the daily load with margin.

### 13.4 Enclosure fit

1. Build the final harness, cut to 10–15 cm, and run its checklist again.
2. Mount the XIAO and antenna as in `docs/wiring.md` §3.
3. Close the lid at the install spot (or the same distance from the access point) and
   check that `rssi` in `status` is ≥ −75 dBm.
4. Compare the GNSS satellite count with the XIAO running and with it unplugged.
5. Check that the gasket is seated.

### 13.5 Soak (T4)

Put the cells and the panel back, with the final harness and the enclosure closed. Set
`node_silence_s` back to `5400`. Log for **72 hours**:
- `node_transactions` keeps climbing.
- The receiver sees every 30-minute packet.
- `reset_reason` and the node's uptime show no unexplained resets.

---

## Pre-deployment checklist

Everything here must be true before the node goes on the roof. After that, the only way
in is MQTT.

**Hardware**
- [ ] Phase 0 exit gate passed (step 12)
- [ ] Final harness built from `docs/wiring.md` with Phase 0's decisions applied; checklist passed
- [ ] Battery-sense wire fitted, calibrated and `LOW_BATT` tested, **or** step 11 decided it isn't needed
- [ ] XIAO mounted on Kapton, antenna placed as in `docs/wiring.md` §3, lid-closed RSSI ≥ −75 dBm
- [ ] GNSS unaffected with the XIAO running
- [ ] Cells back in, panel connected, bench supply removed
- [ ] Gasket seated; no new holes

**Bridge firmware and settings**
- [ ] Production firmware flashed (`pixi run flash`, **not** `flash-phase0`); `fw` in `status` recorded
- [ ] WiFi set for the roof location (static IP if used: `ip`, `gateway`, `netmask`, `dns`)
- [ ] Broker set to the real one: per-device user and password, TLS on port 8883 if used
- [ ] `node_id` matches the blueprint's node id
- [ ] `node_silence_s` back to **5400** (step 8 set it to 600)
- [ ] `batt_enabled`, `batt_low_v` and `batt_resume_v` set as step 11 decided
- [ ] `status`: `clock_valid` true, `poll_failures` 0, `payload` fresh, `node_seen` true

**Solar Node**
- [ ] `docs/node_config.md` applied and read back: role `SENSOR`, `is_power_saving` false,
      air quality on, interval 1800
- [ ] Node id recorded

**Home Assistant and broker**
- [ ] Blueprint running in the real Home Assistant; `mosquitto_sub -v -t 'solarnode/+/telemetry'`
      shows the keys listed in `docs/mqtt_payload.md` §8
- [ ] PM2.5 and PM10 come from the same correction (R-17 in `PLAN.md` §8)
- [ ] Mosquitto ACL from `docs/mosquitto_acl.example` applied; only the operator account can write `cmd`
- [ ] HA Discovery entities appear (if `ha_discovery` is on)

**Proven on the bench**
- [ ] Mesh receiver `PASS` with real Home Assistant data, not only the publisher
- [ ] Fault scenarios behave as in 13.1
- [ ] Queued command, live session, OTA and rollback all work (13.2)
- [ ] `min_free_heap` ≥ 40 KB during a TLS session
- [ ] Idle current measured and the power budget positive for December (13.3)
- [ ] 72 h soak with no missed detections and no resets caused by the C3 (13.5)

**Recovery ready**
- [ ] `remote.py` works from where you'll manage it (home LAN or VPN), with the operator credentials
- [ ] A place to serve OTA images on the LAN (HA's `/config/www` works)
- [ ] Bench notes and the results sheet committed (no coordinates, no serials)

---

## Results sheet

| Measurement | Step | Value | Decides |
|---|---|---|---|
| Battery + point | 2.2 | | where the sense wire attaches |
| SDA/SCL idle voltage | 3.3 | | level shifter |
| First detection (Phase 0 build) | 4 | ☐ node log ☐ receiver `PASS` | |
| Cold boot detections | 5 | __/20 | gate |
| Warm reboot detections | 5 | __/20 | gate |
| Node: power-on → scan of `0x6B` | 6 | ms | boot race margin |
| C3: power-on → first ACK | 6 | ms (target < 150) | boot race margin |
| Min Grove 5 V during the 350 mA pulse | 7.1 | V | Grove power, capacitor |
| Node resets during the pulse test | 7.1 | __/10 | Grove power |
| Capacitor fitted | 7.1 | µF | |
| C3 idle current (`NORMAL`, WiFi off) | 7.2 | mA | power budget |
| Full path receiver `PASS` | 8 | ☐ | |
| Light-sleep detections | 9 | __/20 | gate |
| Meshtastic shutdown voltage | 10 | V | `batt_low_v` |
| Grove 5 V during shutdown | 10 | on / off | whether battery protection is needed |
| SDA/SCL during shutdown | 10 | high / low | wake-on-edge works |
| Supply current, node shut down | 10 | mA | storm survival |
| C3 wakes in 1 h of shutdown | 10 | | gate |
| Battery-sense wire | 11 | fitted / not needed | |
| `batt_cal` | 11 | | |
