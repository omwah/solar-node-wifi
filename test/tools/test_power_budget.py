import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "tools"))

from power_budget import (  # noqa: E402
    Loads, autonomy_days, c3_floor_w, c3_wh_per_day, harvest_wh_per_day, report, SOCAL_ESTIMATE,
)


def test_linear_regulator_floor_matches_plan():
    # PLAN.md §6.1: each 1 mA at 3.3 V costs ~5.9 mW at the battery via boost + linear regulator.
    assert c3_floor_w(Loads(c3_floor_ma=1)) * 1000 == pytest.approx(5.88, abs=0.01)


def test_buck_is_cheaper_than_linear():
    assert c3_floor_w(Loads(linear_reg=False)) < c3_floor_w(Loads(linear_reg=True))


def test_polls_are_small_next_to_the_floor():
    loads = Loads(c3_floor_ma=15, poll_interval_s=600, poll_j=2)
    polls_wh = 144 * 2 / 3600
    assert polls_wh == pytest.approx(0.08)
    assert c3_wh_per_day(loads) == pytest.approx(c3_floor_w(loads) * 24 + polls_wh)


def test_harvest_and_autonomy():
    assert harvest_wh_per_day(4.2, 5, 0.7) == pytest.approx(14.7)
    assert autonomy_days(49, 0.8, 3.92) == pytest.approx(10.0)


def test_report_offline_estimate_is_sustainable(capsys):
    assert report(SOCAL_ESTIMATE, Loads(), 5, 0.7, 49, 0.8, "test")
    out = capsys.readouterr().out
    assert "Worst month: December" in out
