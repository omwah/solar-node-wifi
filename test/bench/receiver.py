#!/usr/bin/env python3
"""T3 bench check: wait for the Solar Node's AirQualityMetrics on the mesh.

Run on a second Meshtastic device connected over USB, while the Solar Node (with the C3
on Grove) and either the publisher's normal scenario or the Phase 0 build is running.
Exits 0 when a packet from the Solar Node carries the expected values, 1 on timeout or a
mismatch.

    pixi run -e bench receive -- --port /dev/ttyACM1 --from '!a1b2c3d4'
    pixi run -e bench receive -- --port /dev/ttyACM1 --from '!a1b2c3d4' --expect phase0
"""

import argparse
import queue
import sys

from pubsub import pub

import meshtastic.serial_interface
from meshtastic.protobuf import telemetry_pb2
from publisher import EXPECTED_MESH

TOLERANCE = 0.05

# The fixed values served by the Phase 0 build (src/main.cpp, BRIDGE_FIXED_TEST_VALUES).
EXPECTED_PHASE0 = {
    "pm10_standard": 1,
    "pm25_standard": 2,
    "pm100_standard": 3,
    "co2": 450,
    "pm_voc_idx": 100.0,
    "pm_nox_idx": 1.0,
    "pm_temperature": 21.5,
    "pm_humidity": 45.0,
}

EXPECTED = {"publisher": EXPECTED_MESH, "phase0": EXPECTED_PHASE0}


def matches(aq, expected):
    problems = []
    for field, want in expected.items():
        if not aq.HasField(field):
            problems.append(f"{field} missing")
            continue
        got = getattr(aq, field)
        if abs(got - want) > TOLERANCE:
            problems.append(f"{field}={got} (want {want})")
    return problems


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--port", help="serial port of the receiving Meshtastic device")
    ap.add_argument("--from", dest="sender", required=True, help="Solar Node id, e.g. !a1b2c3d4")
    ap.add_argument("--expect", choices=EXPECTED, default="publisher",
                    help="values to expect: the publisher's normal scenario or the Phase 0 build")
    ap.add_argument("--timeout", type=float, default=45 * 60, help="seconds (default covers a 30 min interval)")
    opts = ap.parse_args()

    packets = queue.Queue()

    def on_telemetry(packet, interface):
        if packet.get("fromId") == opts.sender:
            packets.put(packet)

    pub.subscribe(on_telemetry, "meshtastic.receive.telemetry")
    iface = meshtastic.serial_interface.SerialInterface(devPath=opts.port)
    print(f"listening for AirQualityMetrics from {opts.sender} (up to {opts.timeout:.0f}s)", flush=True)
    try:
        while True:
            try:
                packet = packets.get(timeout=opts.timeout)
            except queue.Empty:
                print("FAIL: no AirQualityMetrics received")
                return 1
            telemetry = packet["decoded"].get("telemetry")
            if telemetry is None:
                continue
            msg = telemetry_pb2.Telemetry()
            msg.ParseFromString(packet["decoded"]["payload"])
            if msg.WhichOneof("variant") != "air_quality_metrics":
                continue
            problems = matches(msg.air_quality_metrics, EXPECTED[opts.expect])
            print(msg.air_quality_metrics)
            if problems:
                print("FAIL: " + "; ".join(problems))
                return 1
            print(f"PASS: AirQualityMetrics match the {opts.expect} values")
            return 0
    finally:
        iface.close()


if __name__ == "__main__":
    sys.exit(main())
