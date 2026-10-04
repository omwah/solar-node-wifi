"""Renders docs/ha_automation.yaml's payload template with mocked HA template functions.

Covers the prefix stripping and value encoding against the reference entity list
(docs/home-assistant-entities.md). HA-specific behaviour is mocked, so this checks the
template logic, not HA itself.
"""

import json
import re
from datetime import datetime, timezone
from pathlib import Path
from types import SimpleNamespace

import jinja2
import pytest
import yaml

ROOT = Path(__file__).resolve().parents[2]
NOW = datetime(2026, 10, 3, 18, 40, tzinfo=timezone.utc)
REPORTED = datetime(2026, 10, 3, 18, 39, 30, tzinfo=timezone.utc)

UNITS = {
    "temperature": "°F", "dew_point": "°F", "feels_like": "°F", "wet_bulb_temperature": "°F",
    "humidity": "%", "air_pressure": "hPa", "irradiance": "W/m²", "illuminance": "lx",
    "wind_speed": "mph", "wind_gust": "mph", "wind_lull": "mph", "wind_speed_average": "mph",
    "pm1": "µg/m³", "pm2_5": "µg/m³", "pm10": "µg/m³", "pm0_3": "particles/dL",
    "carbon_dioxide": "ppm", "battery_voltage": "V", "air_density": "kg/m³",
}


class InputTag(str):
    pass


def load_blueprint():
    class Loader(yaml.SafeLoader):
        pass

    Loader.add_constructor("!input", lambda loader, node: InputTag(loader.construct_scalar(node)))
    return yaml.load((ROOT / "docs" / "ha_automation.yaml").read_text(), Loader=Loader)


def reference_states():
    """entity_id -> state, from the reference entity table."""
    states = {}
    for line in (ROOT / "docs" / "home-assistant-entities.md").read_text().splitlines():
        m = re.match(r"\|\s*`(sensor\.[^`]+)`\s*\|\s*([^|]+?)\s*\|", line)
        if m:
            states[m.group(1)] = m.group(2)
    return states


class FakeHass:
    def __init__(self, devices, states):
        self.devices = devices  # id -> (name, [entity ids])
        self.state_values = states

    def unit(self, entity_id):
        obj = entity_id.split(".", 1)[1]
        for suffix, unit in UNITS.items():
            if obj.endswith("_" + suffix):
                return unit
        return None

    def env(self):
        env = jinja2.Environment()

        def is_number(v):
            try:
                float(v)
                return True
            except (TypeError, ValueError):
                return False

        def states_fn(entity_id):
            return self.state_values.get(entity_id, "unknown")

        # HA's `states` is both callable (state string) and indexable (state object).
        class StatesProxy:
            def __call__(_, entity_id):
                return states_fn(entity_id)

            def __getitem__(_, entity_id):
                return SimpleNamespace(last_reported=REPORTED, last_updated=REPORTED, last_changed=REPORTED)

        env.globals.update(
            device_attr=lambda dev, attr: self.devices[dev][0] if attr == "name" else None,
            device_entities=lambda dev: list(self.devices[dev][1]),
            states=StatesProxy(),
            state_attr=lambda e, attr: self.unit(e) if attr == "unit_of_measurement" else None,
            is_state=lambda e, s: states_fn(e) == s,
            as_timestamp=lambda dt: dt.timestamp(),
            now=lambda: NOW,
        )
        env.filters.update(
            slugify=lambda s: re.sub(r"[^a-z0-9]+", "_", s.lower()).strip("_"),
            is_number=is_number,
            to_json=lambda v: json.dumps(v, ensure_ascii=False),
            as_timestamp=lambda dt: dt.timestamp(),
        )
        env.tests["match"] = lambda value, pattern: re.match(pattern, value) is not None
        return env


def render(devices, states, rename_map=None, debug_switch=""):
    bp = load_blueprint()
    template = bp["actions"][0]["data"]["payload"]
    hass = FakeHass(devices, states)
    text = hass.env().from_string(template).render(
        node_id="site-01", aq_device="aq", env_device="env",
        rename_map=rename_map or {}, debug_switch=debug_switch, debug_minutes=30,
    )
    return json.loads(text)


@pytest.fixture
def reference():
    states = reference_states()
    aq = [e for e in states if e.startswith("sensor.roof_airgradient_open_air_")]
    env = [e for e in states if e.startswith("sensor.st_00000000_")]
    devices = {"aq": ("Roof AirGradient Open Air", aq), "env": ("ST-00000000", env)}
    return devices, states


def test_blueprint_inputs_and_topic():
    bp = load_blueprint()
    assert set(bp["blueprint"]["input"]) >= {"node_id", "aq_device", "env_device", "rename_map"}
    data = bp["actions"][0]["data"]
    assert data["topic"] == "solarnode/{{ node_id }}/telemetry"
    assert data["retain"] is True


def test_reference_install(reference):
    msg = render(*reference)
    ts = int(REPORTED.timestamp())
    assert msg["ts"] == int(NOW.timestamp())
    assert msg["aq"]["prefix"] == "roof_airgradient_open_air_"
    assert msg["env"]["prefix"] == "st_00000000_"
    aq, env = msg["aq"]["values"], msg["env"]["values"]
    assert aq["pm2_5"] == [6.09, "µg/m³", ts]
    assert aq["carbon_dioxide"] == [482.0, "ppm", ts]
    assert aq["voc_index"] == [71.0, None, ts]
    assert env["temperature"] == [77.45, "°F", ts]
    assert env["wet_bulb_temperature"] == [63.61, "°F", ts]
    assert env["irradiance"] == [0.0, "W/m²", ts]
    assert "precipitation_type" not in env  # text state, skipped
    assert msg["debug"] is False and msg["debug_until"] is None


def test_unavailable_is_null(reference):
    devices, states = reference
    states = dict(states, **{"sensor.st_00000000_humidity": "unavailable"})
    assert render(devices, states)["env"]["values"]["humidity"][0] is None


def test_renamed_device_falls_back_to_common_prefix(reference):
    devices, states = reference
    devices = dict(devices, env=("Roof Weather", devices["env"][1]))
    msg = render(devices, states)
    assert msg["env"]["prefix"] == "st_00000000_"
    assert "temperature" in msg["env"]["values"]


def test_rename_map(reference):
    devices, states = reference
    msg = render(devices, states, rename_map={"carbon_dioxide": "co2"})
    assert "co2" in msg["aq"]["values"] and "carbon_dioxide" not in msg["aq"]["values"]


def test_debug_switch(reference):
    devices, states = reference
    states = dict(states, **{"input_boolean.bridge_session": "on"})
    msg = render(devices, states, debug_switch="input_boolean.bridge_session")
    assert msg["debug"] is True
    assert msg["debug_until"] == int(REPORTED.timestamp()) + 30 * 60
