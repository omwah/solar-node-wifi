"""Broker-backed checks of the host tools and the bench publisher (needs mosquitto)."""

import json
import os
import shutil
import socket
import subprocess
import sys
import tempfile
import threading
import time
from pathlib import Path
from types import SimpleNamespace

import paho.mqtt.client as mqtt
import pytest

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))
sys.path.insert(0, str(ROOT / "test" / "bench"))

import publisher  # noqa: E402
from remote import Remote  # noqa: E402

MOSQUITTO = shutil.which("mosquitto") or str(Path(os.environ.get("CONDA_PREFIX", "")) / "sbin" / "mosquitto")


def free_port():
    with socket.socket() as s:
        s.bind(("127.0.0.1", 0))
        return s.getsockname()[1]


@pytest.fixture(scope="module")
def broker():
    if not Path(MOSQUITTO).exists():
        pytest.skip("mosquitto not available")
    port = free_port()
    with tempfile.NamedTemporaryFile("w", suffix=".conf", delete=False) as conf:
        conf.write(f"listener {port} 127.0.0.1\nallow_anonymous true\npersistence false\n")
    proc = subprocess.Popen([MOSQUITTO, "-c", conf.name], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    for _ in range(50):
        try:
            socket.create_connection(("127.0.0.1", port), timeout=0.1).close()
            break
        except OSError:
            time.sleep(0.1)
    yield port
    proc.terminate()
    proc.wait()
    os.unlink(conf.name)


def client(port):
    c = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2)
    c.connect("127.0.0.1", port)
    c.loop_start()
    return c


def test_remote_round_trip_with_fake_device(broker):
    received = []
    device = client(broker)

    def on_cmd(c, userdata, msg):
        req = json.loads(msg.payload)
        received.append(req)
        c.publish("solarnode/t1/resp", json.dumps({"id": req["id"], "ok": True, "result": {"mode": "NORMAL"}}), qos=1)

    device.on_message = on_cmd
    device.subscribe("solarnode/t1/cmd", qos=1)
    time.sleep(0.2)

    opts = SimpleNamespace(node="t1", user=None, password=None, tls=False, ca=None, host="127.0.0.1", port=broker)
    reply = Remote(opts).send("set", ["poll_interval_s", "900"], wait=5)
    assert reply["ok"] is True
    assert received[0]["cmd"] == "set"
    assert received[0]["args"] == {"key": "poll_interval_s", "value": "900"}
    device.loop_stop()


def test_remote_times_out_without_device(broker):
    opts = SimpleNamespace(node="nobody", user=None, password=None, tls=False, ca=None, host="127.0.0.1", port=broker)
    reply = Remote(opts).send("status", [], wait=0.5)
    assert reply["ok"] is False
    assert "queued" in reply["error"]


def test_publisher_retains_spec_payload(broker):
    payload = publisher.build("normal", 1791052648, 10)
    pub_client = client(broker)
    pub_client.publish("solarnode/t2/telemetry", payload.encode(), retain=True).wait_for_publish()

    got = threading.Event()
    result = {}
    sub = client(broker)

    def on_msg(c, userdata, msg):
        result["retain"] = msg.retain
        result["payload"] = json.loads(msg.payload)
        got.set()

    sub.on_message = on_msg
    sub.subscribe("solarnode/t2/telemetry")
    assert got.wait(5)
    assert result["retain"] is True
    message = result["payload"]
    assert message["env"]["values"]["temperature"] == [77.45, "°F", 1791052648 - 30]
    assert message["aq"]["values"]["carbon_dioxide"][1] == "ppm"
    assert message["debug"] is False


def test_publisher_fault_scenarios():
    now = 1791052648
    assert publisher.build("stale", now, 10).count(str(now - 7200)) > 5
    assert json.loads(publisher.build("nulls", now, 10))["aq"] is None
    with pytest.raises(json.JSONDecodeError):
        json.loads(publisher.build("malformed", now, 10))
    assert len(publisher.build("oversize", now, 10).encode()) > 4096
    debug = json.loads(publisher.build("debug", now, 10))
    assert debug["debug"] is True and debug["debug_until"] == now + 600
