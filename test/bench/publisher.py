#!/usr/bin/env python3
"""Synthetic Home Assistant publisher for the T3 bench (docs/mqtt_payload.md).

Publishes the retained telemetry message the HA blueprint would, using the reference
install's values (°F from the Tempest), plus fault scenarios for the firmware:

    pixi run -e bench publish -- --host 127.0.0.1 --node site-01
    pixi run -e bench publish -- --host 127.0.0.1 --node site-01 --scenario stale
"""

import argparse
import json
import sys
import time

import paho.mqtt.client as mqtt

SCENARIOS = {
    "normal": "reference values",
    "celsius": "Tempest reports °C instead of °F",
    "stale": "every value 2 h old (the C3 must serve unknown)",
    "nulls": "aq and env blocks null (the C3 must serve unknown)",
    "bad-units": "CO2 in ppb and humidity without a unit (those fields unknown)",
    "malformed": "truncated JSON (the C3 must reject it)",
    "oversize": "more than 4 KB (the C3 must reject it)",
    "debug": "debug true with debug_until (the C3 starts a live session)",
    "clear": "remove the retained message",
}

# Values the receiver expects to see on the mesh for the normal scenario.
EXPECTED_MESH = {
    "pm10_standard": 0,     # PM1.0 (0 µg/m³)
    "pm25_standard": 6,     # 6.09 µg/m³; the driver truncates to whole µg/m³
    "pm100_standard": 2,    # 2.17 µg/m³
    "co2": 482,
    "pm_voc_idx": 71.0,
    "pm_nox_idx": 1.0,
    "pm_temperature": 25.25,  # 77.45 °F
    "pm_humidity": 47.28,
}


def triple(value, unit, ts):
    return [value, unit, ts]


def build(scenario, now, debug_minutes):
    age = 7200 if scenario == "stale" else 30
    ts = now - age
    temp = triple(25.25, "°C", ts) if scenario == "celsius" else triple(77.45, "°F", ts)
    aq = {
        "pm1": triple(0, "µg/m³", ts),
        "pm2_5": triple(6.09, "µg/m³", ts),
        "pm10": triple(2.17, "µg/m³", ts),
        "pm0_3": triple(228, "particles/dL", ts),
        "carbon_dioxide": triple(482, "ppb" if scenario == "bad-units" else "ppm", ts),
        "voc_index": triple(71, None, ts),
        "nox_index": triple(1, None, ts),
        "temperature": triple(85.514, "°F", ts),
        "humidity": triple(49.96, "%", ts),
    }
    env = {
        "temperature": temp,
        "wet_bulb_temperature": triple(63.61, "°F", ts),
        "humidity": triple(47.28, None if scenario == "bad-units" else "%", ts),
        "irradiance": triple(812, "W/m²", ts),
        "air_pressure": triple(982.39, "hPa", ts),
        "wind_speed": triple(0, "mph", ts),
    }
    message = {
        "ts": now,
        "aq": None if scenario == "nulls" else
        {"device": "Roof AirGradient Open Air", "prefix": "roof_airgradient_open_air_", "values": aq},
        "env": None if scenario == "nulls" else {"device": "ST-00000000", "prefix": "st_00000000_", "values": env},
        "debug": scenario == "debug",
        "debug_until": now + debug_minutes * 60 if scenario == "debug" else None,
    }
    text = json.dumps(message, ensure_ascii=False)
    if scenario == "malformed":
        return text[: len(text) // 2]
    if scenario == "oversize":
        message["padding"] = "x" * 5000
        return json.dumps(message, ensure_ascii=False)
    return text


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--host", default="127.0.0.1")
    ap.add_argument("--port", type=int, default=1883)
    ap.add_argument("--node", default="site-01")
    ap.add_argument("--scenario", choices=SCENARIOS, default="normal")
    ap.add_argument("--interval", type=float, default=60, help="seconds between publishes")
    ap.add_argument("--debug-minutes", type=int, default=10)
    ap.add_argument("--once", action="store_true")
    ap.add_argument("--list", action="store_true", help="list scenarios")
    opts = ap.parse_args()
    if opts.list:
        for name, text in SCENARIOS.items():
            print(f"{name:10} {text}")
        return 0

    topic = f"solarnode/{opts.node}/telemetry"
    client = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2)
    client.connect(opts.host, opts.port)
    client.loop_start()
    try:
        while True:
            payload = b"" if opts.scenario == "clear" else build(opts.scenario, int(time.time()), opts.debug_minutes).encode()
            client.publish(topic, payload, qos=0, retain=True).wait_for_publish()
            print(f"{time.strftime('%H:%M:%S')} {opts.scenario}: {len(payload)} bytes -> {topic}", flush=True)
            if opts.once or opts.scenario == "clear":
                break
            time.sleep(opts.interval)
    except KeyboardInterrupt:
        pass
    client.loop_stop()
    client.disconnect()
    return 0


if __name__ == "__main__":
    sys.exit(main())
