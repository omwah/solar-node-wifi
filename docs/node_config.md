# Solar Node configuration

Meshtastic settings for the Solar Node. Stock firmware; configuration only.

**Order matters.** Choosing a role applies that role's defaults at the moment the role
changes, so set the role first, let the node reboot, then apply the rest.

| Step | Setting | Value | Why |
|---|---|---|---|
| 1 | `lora.region` | `US` | 915 MHz band |
| 1 | channel 0 | default (LongFast, default key) | telemetry any Meshtastic client can decode |
| 2 | `device.role` | `SENSOR` | telemetry isn't stretched on busy meshes, gets a looser channel-busy check and higher priority; still relays like `CLIENT` |
| 3 | `power.is_power_saving` | `false` | **never enable**: with `SENSOR` it deep-sleeps the node, radio included |
| 3 | `telemetry.air_quality_enabled` | `true` | reads the bridge |
| 3 | `telemetry.air_quality_interval` | `1800` | shortest the default channel allows |
| 3 | `telemetry.environment_measurement_enabled` | `false` | the `SENSOR` role turns it on; no environment sensor is fitted |

## Meshtastic CLI

```
meshtastic --set lora.region US
meshtastic --set device.role SENSOR            # the node reboots
meshtastic --set power.is_power_saving false \
           --set telemetry.air_quality_enabled true \
           --set telemetry.air_quality_interval 1800 \
           --set telemetry.environment_measurement_enabled false
meshtastic --get device.role --get power.is_power_saving --get telemetry
```

## Phone app

Menu names vary slightly between app versions.

- Radio Configuration → LoRa → Region: `US`.
- Radio Configuration → Device → Role: `Sensor`. Save; wait for the reboot.
- Radio Configuration → Power → Power saving: off.
- Module Configuration → Telemetry → Air quality: enabled, interval 30 minutes.
  Environment metrics: disabled.

## Bridge setting to match

With a 1800 s air-quality interval, set the bridge's watchdog to three intervals:

```
pixi run configure -- set node_silence_s 5400
pixi run configure -- commit
```

## Verify

1. The node's serial log after a reboot shows `SEN6X found` and
   `SEN6X: found sensor model SEN66`.
2. Another Meshtastic client receives an air-quality packet from the node within about
   30 minutes (`pixi run -e bench receive` automates this on the bench).
3. `--get power.is_power_saving` reads `false`.

## Notes

- `SENSOR` marks the node unmessagable; it still relays other nodes' packets.
- Not `ROUTER`: router roles force a ≥ 12 h telemetry interval on the default channel.
