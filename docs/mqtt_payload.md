# MQTT Payload Specification

**Status:** defined by this project. The operator publishes it from Home Assistant using the provided
automation/blueprint (`docs/ha_automation.yaml`, Phase 3).

---

## 1. Design goals

1. **Generic publisher.** HA entity IDs depend on each user's device names
   (`sensor.st_00000000_temperature`, `sensor.roof_airgradient_open_air_pm2_5`). The publisher must not
   hard-code them. It strips the device prefix and publishes every numeric sensor of the chosen device
   under its **stable suffix** (`temperature`, `pm2_5`). The firmware knows the suffixes, not the prefixes.
2. **No unit assumptions.** HA reports values in the user's display units (°F in the reference install).
   Each value carries its unit, and **the firmware converts**. The publisher does no per-field logic.
3. **Freshness per value.** Each value carries the time HA last received it, so one dead sensor
   doesn't hide behind a live one.
4. **One retained message** so the C3 gets everything in a single short connection.

## 2. Transport

| Property | Value |
|---|---|
| Topic | `solarnode/<id>/telemetry` (`<id>` = node slug, e.g. `site-01`) |
| Retain | **true** |
| QoS | 0 |
| Payload | UTF-8 JSON, ≤ 4 KB |
| Publish cadence | every 60 s (time pattern). HA's load is negligible. |

The other topics under `solarnode/<id>/` (`cmd`, `resp`, `state`, `log`) are defined in `PLAN.md` §4.5.

## 3. Payload

```json
{
  "ts": 1791052648,
  "aq": {
    "device": "Roof AirGradient Open Air",
    "prefix": "roof_airgradient_open_air_",
    "values": {
      "pm1":            [0,      "µg/m³", 1791052610],
      "pm2_5":          [6.09,   "µg/m³", 1791052610],
      "pm10":           [2.17,   "µg/m³", 1791052610],
      "pm0_3":          [228,    "particles/dL", 1791052610],
      "carbon_dioxide": [482,    "ppm",   1791052610],
      "voc_index":      [71,     null,    1791052610],
      "nox_index":      [1,      null,    1791052610],
      "temperature":    [85.514, "°F",    1791052610],
      "humidity":       [49.96,  "%",     1791052610]
    }
  },
  "env": {
    "device": "ST-00000000",
    "prefix": "st_00000000_",
    "values": {
      "temperature":  [77.45,  "°F",   1791052595],
      "humidity":     [47.28,  "%",    1791052595],
      "irradiance":   [0,      "W/m²", 1791052595],
      "air_pressure": [982.39, "hPa",  1791052595],
      "wind_speed":   [0,      "mph",  1791052595]
    }
  },
  "debug": false,
  "debug_until": null
}
```
(`env.values` is shortened here. The publisher sends every numeric sensor of the device.)

### 3.1 Top level

| Field | Type | Meaning |
|---|---|---|
| `ts` | int, epoch s | When HA assembled the message. Diagnostic only: the C3's clock comes from SNTP. |
| `aq` | block \| null | Air-quality source (AirGradient Open Air) |
| `env` | block \| null | Weather source (Tempest / WeatherFlow) |
| `debug` | bool | `true` starts a live management session (`PLAN.md` §4.5) |
| `debug_until` | int \| null, epoch s | End of the session. The firmware also caps it at 2 h. |

### 3.2 Block

| Field | Type | Meaning |
|---|---|---|
| `device` | string | HA device name. Diagnostic only. |
| `prefix` | string | The prefix that was stripped. Diagnostic only. |
| `values` | object | `key → [value, unit, ts]` |

Value triple:

| Index | Type | Meaning |
|---|---|---|
| 0 | number \| null | The state as a number; `null` if `unavailable`, `unknown`, or not numeric |
| 1 | string \| null | `unit_of_measurement` exactly as HA reports it; `null` if unitless (indices) |
| 2 | int \| null, epoch s | HA `last_reported` of the entity: the last time the source reported, even if the value was unchanged |

## 4. Publisher: prefix stripping (generic)

The publisher uses HA's core **AirGradient** and **WeatherFlow** integrations; no add-ons are needed.

Inputs to the blueprint: the **AirGradient device**, the **WeatherFlow/Tempest device**, the node `<id>`,
and an optional **rename map**.

For each device:
1. Take the device's entities in the `sensor.` domain (`device_entities(device_id)`).
2. **Find the prefix:**
   - (a) try `slugify(device name) + "_"`. This works for default names: `ST-00000000` → `st_00000000_`,
     `Roof AirGradient Open Air` → `roof_airgradient_open_air_`.
   - (b) If fewer than half of the entities start with it (for example, the device was renamed after its
     entities were created), use the longest common prefix of their object IDs, cut at an `_` boundary.
3. **Key** = object ID with the prefix removed: `sensor.st_00000000_wet_bulb_temperature` →
   `wet_bulb_temperature`. Entities that don't start with the prefix keep their full object ID as the key.
4. Apply the **rename map** last (e.g. `{"temperatura": "temperature"}`). Use it for installs in other
   languages or entities the user renamed by hand.
5. Publish only entities whose state is numeric, or `unavailable`/`unknown` (published as `null`).
   Text states such as `precipitation_type: none` are skipped.

Entities belonging to other devices are excluded automatically. In the reference install,
`sensor.home_weather_station_battery` and `sensor.roof_home_weather_station_lightning_*` belong to a
different device than the station.

## 5. Firmware: keys it uses

Matching is on the **exact** key; suffix matching is never used (`wet_bulb_temperature` must not match
`temperature`). Unknown keys are ignored. Aliases are accepted where HA versions differ.

| Block.key (aliases) | Unit accepted → converted to | Emulated sensor field | Notes |
|---|---|---|---|
| `aq.pm1` | µg/m³ | PM1.0 | |
| `aq.pm2_5` (`pm25`) | µg/m³ | PM2.5 | |
| `aq.pm4` | µg/m³ | PM4.0 | Not reported by the Open Air → unknown |
| `aq.pm10` | µg/m³ | PM10 | |
| `aq.voc_index` | unitless | VOC index | |
| `aq.nox_index` | unitless | NOx index | |
| `aq.carbon_dioxide` (`co2`) | ppm | CO2 | The emulated SEN66 carries it (`PLAN.md` §3a) |
| `env.temperature` | °C, °F, K → °C | temperature | Tempest is the source |
| `env.humidity` | % | relative humidity | |
| `aq.temperature`, `aq.humidity` | as above | T/RH **fallback** | Used only if `th_fallback_aq = true` and the `env` value is missing or stale. Off by default: the Open Air reads high in sun (85.5 °F vs 77.5 °F in the reference capture). |
| `env.irradiance` (`solar_radiation`) | W/m² | — | Reported on the `state` topic only |
| `aq.pm0_3` | — | — | Ignored: Plantower "> 0.3 µm" counts don't map onto Sensirion bins |
| everything else | — | — | Ignored (pressure, wind, rain, lightning: not deliverable, `PLAN.md` §2.3) |

**Unit handling:**
- An accepted unit is converted.
- A **missing or unrecognised unit on a field that needs one** → the field is treated as unknown and the
  key is logged. It is never guessed.
- Recognised spellings: `°C` `°F` `K` `%` `µg/m³` (also `μg/m³` with Greek mu, `ug/m3`) `ppm` `W/m²` (also `W/m2`).

**Freshness:**
- A value is used only if `now_sntp − value_ts ≤ max_age` (default 1800 s).
- A value with `ts = null` is treated as stale.
- A stale or `null` value → the sensor field reads "unknown" (`0xFFFF`/`0x7FFF`). The last value is never
  carried forward.

## 6. `null` rules
- `null` means "not available". **Never publish `0` for a missing value.** On the mesh, a zero PM
  reading and a dead sensor would look the same.
- A whole block can be `null` (source device missing).
- A malformed message or JSON over 4 KB is treated as "no data received". It is never
  partially parsed.

## 7. Size

The reference install produces about 9 AirGradient + 25 Tempest values at ~40–45 bytes each ≈ 1.5 KB.
The firmware parses with a key filter (ArduinoJson `Filter`), so RAM use doesn't depend on how many
extra keys are published. The blueprint has an optional `include` list to send only the keys the
firmware uses.

## 8. Publisher checklist
- [ ] Blueprint imported; AirGradient and Tempest **devices** selected (not individual entities); `<id>` set.
- [ ] MQTT topic `solarnode/<id>/telemetry`, retained.
- [ ] Check the published keys once with `mosquitto_sub -v -t 'solarnode/+/telemetry'`. `aq` should contain
      `pm1`, `pm2_5`, `pm10`, `voc_index`, `nox_index`, `carbon_dioxide`; `env` should contain
      `temperature`, `humidity`. Add rename-map entries for any that are missing.
- [ ] Mosquitto ACL from `docs/mosquitto_acl.example` applied.
