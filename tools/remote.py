#!/usr/bin/env python3
"""Manage a deployed bridge over MQTT.

Commands queue at the broker (QoS 1, persistent session) and run at the device's next
poll, so a reply can take up to the poll interval (10 min by default). Start a live
session for an interactive round trip of about a second:

    pixi run remote -- --host 10.0.0.2 --node site-01 status
    pixi run remote -- --host 10.0.0.2 --node site-01 session 30
    pixi run remote -- --host 10.0.0.2 --node site-01 --shell     # needs a live session
    pixi run remote -- --host 10.0.0.2 --node site-01 --state     # print the retained state
"""

import argparse
import getpass
import json
import os
import queue
import sys
import uuid

import paho.mqtt.client as mqtt

from commands import UsageError, split, to_json


class Remote:
    def __init__(self, opts):
        self.prefix = f"solarnode/{opts.node}/"
        self.replies = queue.Queue()
        self.states = queue.Queue()
        self.client = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2, client_id=f"solarnode-remote-{uuid.uuid4().hex[:8]}")
        if opts.user:
            password = opts.password or os.environ.get("SOLARNODE_MQTT_PASS") or getpass.getpass("MQTT password: ")
            self.client.username_pw_set(opts.user, password)
        if opts.tls:
            self.client.tls_set(ca_certs=opts.ca)
        self.client.on_message = self._on_message
        self.client.connect(opts.host, opts.port)
        self.client.subscribe([(self.prefix + "resp", 1), (self.prefix + "state", 0)])
        self.client.loop_start()

    def _on_message(self, client, userdata, msg):
        try:
            obj = json.loads(msg.payload)
        except json.JSONDecodeError:
            return
        (self.states if msg.topic.endswith("/state") else self.replies).put(obj)

    def send(self, cmd, args, wait):
        request_id = uuid.uuid4().hex[:12]
        self.client.publish(self.prefix + "cmd", to_json(cmd, args, request_id), qos=1).wait_for_publish()
        if wait <= 0:
            return {"ok": True, "result": "queued", "id": request_id}
        while True:
            try:
                reply = self.replies.get(timeout=wait)
            except queue.Empty:
                return {"ok": False, "error": f"no reply within {wait:.0f}s (queued; it runs at the next poll)",
                        "id": request_id}
            if reply.get("id") == request_id:
                return reply

    def state(self, wait):
        try:
            return self.states.get(timeout=wait)
        except queue.Empty:
            return None


def show(obj):
    print(json.dumps(obj, indent=2, ensure_ascii=False))
    return 0 if obj.get("ok", True) else 1


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--host", required=True)
    ap.add_argument("--port", type=int, default=1883)
    ap.add_argument("--user")
    ap.add_argument("--password", help="or set SOLARNODE_MQTT_PASS")
    ap.add_argument("--tls", action="store_true")
    ap.add_argument("--ca", help="CA certificate for --tls")
    ap.add_argument("--node", required=True, help="node_id, e.g. site-01")
    ap.add_argument("--wait", type=float, default=900, help="seconds to wait for a reply (0 = just queue it)")
    ap.add_argument("--state", action="store_true", help="print the retained state and exit")
    ap.add_argument("--shell", action="store_true", help="interactive prompt (use during a live session)")
    ap.add_argument("command", nargs="?")
    ap.add_argument("args", nargs="*")
    opts = ap.parse_args()

    remote = Remote(opts)
    if opts.state:
        state = remote.state(5)
        if state is None:
            print("no retained state yet")
            return 1
        return show(state)
    if opts.shell:
        print("type commands; Ctrl-D to quit")
        while True:
            try:
                text = input(f"{opts.node}> ")
            except EOFError:
                print()
                return 0
            if text.strip():
                try:
                    words = split(text)
                    show(remote.send(words[0], words[1:], 15))
                except (UsageError, ValueError) as e:
                    print(f"error: {e}")
    if not opts.command:
        ap.error("give a command, --state or --shell")
    try:
        return show(remote.send(opts.command, opts.args, opts.wait))
    except UsageError as e:
        ap.error(str(e))


if __name__ == "__main__":
    sys.exit(main())
