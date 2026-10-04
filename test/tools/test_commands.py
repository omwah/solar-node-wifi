import json
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "tools"))

from commands import UsageError, parse_reply, to_json, to_line  # noqa: E402


def test_line_quotes_spaces_and_quotes():
    assert to_line("set", ["wifi_ssid", 'My "Home" Net']) == 'set wifi_ssid "My \\"Home\\" Net"'
    assert to_line("set", ["wifi_pass", ""]) == 'set wifi_pass ""'
    assert to_line("status", []) == "status"


def test_json_named_args():
    msg = json.loads(to_json("set", ["poll_interval_s", "900"], "abc"))
    assert msg == {"id": "abc", "cmd": "set", "args": {"key": "poll_interval_s", "value": "900"}}
    assert json.loads(to_json("session", ["30"], "x"))["args"] == {"minutes": 30}
    assert json.loads(to_json("session", ["end"], "x"))["args"] == {"end": True}
    assert "args" not in json.loads(to_json("status", [], "x"))


def test_rejects_unknown_and_extra_args():
    with pytest.raises(UsageError):
        to_line("frobnicate", [])
    with pytest.raises(UsageError):
        to_json("reboot", ["now"], "x")


def test_parse_reply_skips_log_lines():
    assert parse_reply("I (123) app: mode NORMAL\n") is None
    assert parse_reply('{"not": "a reply"}') is None
    assert parse_reply('{"ok": true, "result": "saved"}') == {"ok": True, "result": "saved"}
