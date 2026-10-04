#!/usr/bin/env python3
"""T3 bench check: wait for the Solar Node's AirQualityMetrics on the mesh.

Run on a second Meshtastic device connected over USB, while the Solar Node (with the C3
on Grove) and the publisher's normal scenario are running. Exits 0 when a packet from
the Solar Node carries the expected values, 1 on timeout or a mismatch.

    pixi run -e bench receive -- --port /dev/ttyACM1 --from '!a1b2c3d4'
"""

import argparse
import queue
import sys

from pubsub import pub

import meshtastic.serial_interface
from meshtastic.protobuf import telemetry_pb2
from publisher import EXPECTED_MESH

TOLERANCE = 0.05


def matches(aq):
    problems = []
    for field, want in EXPECTED_MESH.items():
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
            problems = matches(msg.air_quality_metrics)
            print(msg.air_quality_metrics)
            if problems:
                print("FAIL: " + "; ".join(problems))
                return 1
            print("PASS: AirQualityMetrics match the publisher's values")
            return 0
    finally:
        iface.close()


if __name__ == "__main__":
    sys.exit(main())
