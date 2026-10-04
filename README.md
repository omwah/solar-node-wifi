# solar-node-wifi

Firmware for a Seeed XIAO ESP32-C3 that takes weather and air-quality readings from
Home Assistant over MQTT and presents them to a SenseCAP Solar Node P1-Pro, running stock
Meshtastic, as an emulated Sensirion SEN66 on the Grove I²C port. The node broadcasts the
readings to the mesh as standard air-quality telemetry.

- Design, decisions and status: [docs/PLAN.md](docs/PLAN.md)
- MQTT payload and Home Assistant publisher: [docs/mqtt_payload.md](docs/mqtt_payload.md),
  [docs/ha_automation.yaml](docs/ha_automation.yaml)
- Wiring: [docs/wiring.md](docs/wiring.md)
- Solar Node settings: [docs/node_config.md](docs/node_config.md)
- Bench testing and pre-deployment checklist: [docs/bench_testing.md](docs/bench_testing.md)

## Build and test

Requires [Pixi](https://pixi.sh).

```
pixi run build          # firmware
pixi run test           # host unit tests
pixi run driver-test    # Meshtastic's SEN66 driver against the emulator
pixi run flash          # flash over USB
pixi run monitor        # serial console
```

## Configure

Over USB:

```
pixi run configure -- set wifi_ssid "My Network"
pixi run configure -- set wifi_pass "..."
pixi run configure -- set mqtt_host 192.168.1.10
pixi run configure -- set node_id site-01
pixi run configure -- commit
pixi run configure -- status
```

Over MQTT once deployed:

```
pixi run remote -- --host 192.168.1.10 --node site-01 status
pixi run remote -- --host 192.168.1.10 --node site-01 session 30
```
