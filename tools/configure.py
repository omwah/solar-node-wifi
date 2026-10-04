#!/usr/bin/env python3
"""Configure the bridge over the XIAO's USB serial console.

    pixi run configure -- --port /dev/ttyACM0 status
    pixi run configure -- --port /dev/ttyACM0 set wifi_ssid "My Network"
    pixi run configure -- --port /dev/ttyACM0 commit
    pixi run configure -- --port /dev/ttyACM0            # interactive shell
"""

import argparse
import json
import sys
import time

import serial
from serial.tools import list_ports

from commands import UsageError, parse_reply, split, to_line

# Espressif's USB-Serial-JTAG vendor/product id
ESP_USB_JTAG = (0x303A, 0x1001)


def find_port():
    for p in list_ports.comports():
        if (p.vid, p.pid) == ESP_USB_JTAG:
            return p.device
    return None


def run(conn, cmd, args, timeout, verbose):
    conn.reset_input_buffer()
    conn.write((to_line(cmd, args) + "\n").encode())
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        raw = conn.readline().decode(errors="replace")
        if not raw:
            continue
        reply = parse_reply(raw)
        if reply is not None:
            return reply
        if verbose:
            sys.stderr.write(raw)
    raise TimeoutError(f"no reply to {cmd!r} within {timeout}s")


def show(reply):
    print(json.dumps(reply, indent=2, ensure_ascii=False))
    return 0 if reply.get("ok") else 1


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--port", help="serial port (default: auto-detect the XIAO)")
    ap.add_argument("--timeout", type=float, default=40, help="seconds to wait for a reply (a poll can take ~30 s)")
    ap.add_argument("-v", "--verbose", action="store_true", help="also print device log lines")
    ap.add_argument("command", nargs="?")
    ap.add_argument("args", nargs="*")
    opts = ap.parse_args()

    port = opts.port or find_port()
    if not port:
        ap.error("no XIAO ESP32-C3 found; pass --port")

    with serial.Serial(port, 115200, timeout=0.5) as conn:
        if opts.command:
            try:
                return show(run(conn, opts.command, opts.args, opts.timeout, opts.verbose))
            except UsageError as e:
                ap.error(str(e))
        print(f"connected to {port}; type 'help', Ctrl-D to quit")
        while True:
            try:
                text = input("bridge> ")
            except EOFError:
                print()
                return 0
            if not text.strip():
                continue
            try:
                words = split(text)
                show(run(conn, words[0], words[1:], opts.timeout, opts.verbose))
            except (UsageError, ValueError, TimeoutError) as e:
                print(f"error: {e}")


if __name__ == "__main__":
    sys.exit(main())
