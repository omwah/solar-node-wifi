#!/usr/bin/env python3
"""Solar Node + bridge power budget (docs/PLAN.md §6).

Monthly harvest vs. load and storm autonomy. Site coordinates are runtime inputs only;
never commit them.

    pixi run power-budget                                   # offline Southern California estimate
    pixi run power-budget -- --lat 34.0 --lon -118.0        # NREL PVWatts irradiance (DEMO_KEY)
    pixi run power-budget -- --c3-floor-ma 11.5 --node-wh 2.4   # with Phase 0 measurements
"""

import argparse
import calendar
import json
import os
import sys
import urllib.parse
import urllib.request
from dataclasses import dataclass

# Plane-of-array peak sun hours (kWh/m²/day), Southern California, south 45°, unshaded.
# Rough estimate from PLAN.md §6.2; replace with PVWatts figures for the real site.
SOCAL_ESTIMATE = [4.3, 5.0, 5.8, 6.3, 6.3, 6.3, 6.6, 6.7, 6.4, 5.7, 4.8, 4.2]

PVWATTS_URL = "https://developer.nrel.gov/api/pvwatts/v8.json"


@dataclass
class Loads:
    c3_floor_ma: float = 15.0   # always-on C3 current at 3.3 V
    rail_v: float = 3.3
    supply_v: float = 5.0       # Grove rail
    boost_eff: float = 0.85     # battery -> 5 V
    linear_reg: bool = True     # XIAO's 5 -> 3.3 V regulator is linear
    buck_eff: float = 0.85      # used when linear_reg is False
    poll_interval_s: float = 600
    poll_j: float = 2.0         # energy per poll at the battery
    node_wh: float = 3.0        # Solar Node's own consumption per day


def c3_floor_w(loads: Loads) -> float:
    """Battery-side power of the C3's always-on floor."""
    if loads.linear_reg:
        rail_w = loads.supply_v * loads.c3_floor_ma / 1000  # linear: input current = output current
    else:
        rail_w = loads.rail_v * loads.c3_floor_ma / 1000 / loads.buck_eff
    return rail_w / loads.boost_eff


def c3_wh_per_day(loads: Loads) -> float:
    polls = 86400 / loads.poll_interval_s
    return c3_floor_w(loads) * 24 + polls * loads.poll_j / 3600


def load_wh_per_day(loads: Loads) -> float:
    return c3_wh_per_day(loads) + loads.node_wh


def harvest_wh_per_day(psh: float, panel_w: float, derate: float) -> float:
    return psh * panel_w * derate


def autonomy_days(battery_wh: float, usable: float, load_wh: float) -> float:
    return battery_wh * usable / load_wh


def fetch_pvwatts(lat, lon, tilt, azimuth, key):
    query = urllib.parse.urlencode({
        "api_key": key, "lat": lat, "lon": lon, "system_capacity": 0.005, "azimuth": azimuth,
        "tilt": tilt, "array_type": 0, "module_type": 0, "losses": 14,
    })
    with urllib.request.urlopen(f"{PVWATTS_URL}?{query}", timeout=30) as resp:
        data = json.load(resp)
    if data.get("errors"):
        raise RuntimeError("; ".join(data["errors"]))
    return data["outputs"]["solrad_monthly"]


def report(psh, loads, panel_w, derate, battery_wh, usable, source):
    load = load_wh_per_day(loads)
    c3 = c3_wh_per_day(loads)
    print(f"Irradiance: {source}")
    print(f"C3: floor {c3_floor_w(loads) * 1000:.0f} mW at the battery, {c3:.2f} Wh/day "
          f"(polls every {loads.poll_interval_s:.0f} s); Solar Node {loads.node_wh:.2f} Wh/day")
    print(f"Battery {battery_wh:.0f} Wh, {usable:.0%} usable; panel {panel_w:g} W x {derate:g} derate\n")
    print(f"{'Month':<6}{'PSH':>6}{'Harvest':>10}{'Load':>8}{'Margin':>9}")
    worst = None
    for month, h in enumerate(psh, start=1):
        harvest = harvest_wh_per_day(h, panel_w, derate)
        margin = harvest / load
        worst = min(worst, (margin, month)) if worst else (margin, month)
        print(f"{calendar.month_abbr[month]:<6}{h:>6.2f}{harvest:>9.1f} {load:>7.2f}{margin:>8.1f}x")
    days = autonomy_days(battery_wh, usable, load)
    print(f"\nWorst month: {calendar.month_name[worst[1]]}, harvest {worst[0]:.1f}x the load "
          f"({'sustainable' if worst[0] >= 1 else 'NOT sustainable'})")
    print(f"Storm autonomy with no sun: {days:.1f} days")
    return worst[0] >= 1


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--lat", type=float)
    ap.add_argument("--lon", type=float)
    ap.add_argument("--tilt", type=float, default=45)
    ap.add_argument("--azimuth", type=float, default=180)
    ap.add_argument("--nrel-key", default=os.environ.get("NREL_API_KEY", "DEMO_KEY"))
    ap.add_argument("--panel-w", type=float, default=5)
    ap.add_argument("--derate", type=float, default=0.7)
    ap.add_argument("--battery-wh", type=float, default=49)
    ap.add_argument("--usable", type=float, default=0.8)
    defaults = Loads()
    ap.add_argument("--c3-floor-ma", type=float, default=defaults.c3_floor_ma)
    ap.add_argument("--buck", action="store_true", help="3.3 V buck instead of the XIAO's linear regulator")
    ap.add_argument("--poll-interval", type=float, default=defaults.poll_interval_s)
    ap.add_argument("--poll-j", type=float, default=defaults.poll_j)
    ap.add_argument("--node-wh", type=float, default=defaults.node_wh)
    opts = ap.parse_args()

    loads = Loads(c3_floor_ma=opts.c3_floor_ma, linear_reg=not opts.buck, poll_interval_s=opts.poll_interval,
                  poll_j=opts.poll_j, node_wh=opts.node_wh)
    psh = None
    if opts.lat is not None and opts.lon is not None:
        try:
            psh = fetch_pvwatts(opts.lat, opts.lon, opts.tilt, opts.azimuth, opts.nrel_key)
            source = f"NREL PVWatts v8, tilt {opts.tilt:g}°, azimuth {opts.azimuth:g}°"
        except (OSError, RuntimeError, KeyError, ValueError) as e:
            print(f"PVWatts unavailable ({e}); using the built-in estimate\n", file=sys.stderr)
    if psh is None:
        psh = SOCAL_ESTIMATE
        source = "built-in Southern California estimate (pass --lat/--lon for PVWatts)"
    ok = report(psh, loads, opts.panel_w, opts.derate, opts.battery_wh, opts.usable, source)
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
