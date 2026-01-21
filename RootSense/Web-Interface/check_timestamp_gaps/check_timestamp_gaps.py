#!/usr/bin/env python3
"""
check_timestamp_gaps.py

Checks that, for each sensor (field2), the time gap between consecutive rows'
created_at timestamps is no more than a given threshold (default: 70 minutes).

Usage:
  python3 check_timestamp_gaps.py input.csv
  python3 check_timestamp_gaps.py input.csv --max-minutes 70
  python3 check_timestamp_gaps.py input.csv --sensor-col field2 --time-col created_at
"""

from __future__ import annotations

import argparse
import csv
import sys
from collections import defaultdict
from dataclasses import dataclass
from datetime import datetime, timezone
from typing import Dict, List, Optional, Tuple


@dataclass(frozen=True)
class RowInfo:
    entry_id: Optional[str]
    created_at: datetime
    raw_created_at: str
    raw_sensor: str


def parse_created_at(value: str) -> datetime:
    """
    Parse timestamps like: 2026-01-16T06:23:08Z
    Returns an aware datetime in UTC.
    """
    s = (value or "").strip()
    if not s:
        raise ValueError("created_at is empty")

    # ThingSpeak often uses trailing 'Z' for UTC.
    if s.endswith("Z"):
        # Replace Z with +00:00 so datetime.fromisoformat can parse it reliably.
        s = s[:-1] + "+00:00"

    dt = datetime.fromisoformat(s)
    if dt.tzinfo is None:
        # If timezone missing, assume UTC (safer than local time).
        dt = dt.replace(tzinfo=timezone.utc)
    else:
        dt = dt.astimezone(timezone.utc)
    return dt


def read_csv_grouped(
    path: str,
    time_col: str,
    sensor_col: str,
    entry_col: str,
) -> Dict[str, List[RowInfo]]:
    grouped: Dict[str, List[RowInfo]] = defaultdict(list)

    with open(path, "r", newline="", encoding="utf-8") as f:
        reader = csv.DictReader(f)
        if reader.fieldnames is None:
            raise ValueError("CSV appears to have no header row.")

        # Basic header validation
        for col in (time_col, sensor_col):
            if col not in reader.fieldnames:
                raise ValueError(
                    f"Missing required column '{col}'. Found columns: {reader.fieldnames}"
                )

        for i, row in enumerate(reader, start=2):  # start=2 because header is line 1
            raw_time = (row.get(time_col) or "").strip()
            raw_sensor = (row.get(sensor_col) or "").strip()
            raw_entry = (row.get(entry_col) or "").strip() if entry_col in row else None

            if not raw_sensor:
                # Skip rows with no sensor id; alternatively, you could treat as an error.
                continue

            try:
                dt = parse_created_at(raw_time)
            except Exception as e:
                raise ValueError(f"Line {i}: invalid {time_col}={raw_time!r}: {e}") from e

            grouped[raw_sensor].append(
                RowInfo(
                    entry_id=raw_entry or None,
                    created_at=dt,
                    raw_created_at=raw_time,
                    raw_sensor=raw_sensor,
                )
            )

    return grouped


def find_violations(
    grouped: Dict[str, List[RowInfo]],
    max_gap_seconds: int,
) -> List[Tuple[str, RowInfo, RowInfo, int]]:
    """
    Returns list of (sensor_id, prev_row, curr_row, gap_seconds) for gaps > threshold.
    """
    violations: List[Tuple[str, RowInfo, RowInfo, int]] = []

    for sensor_id, rows in grouped.items():
        if len(rows) < 2:
            continue

        rows_sorted = sorted(rows, key=lambda r: r.created_at)
        prev = rows_sorted[0]
        for curr in rows_sorted[1:]:
            gap = int((curr.created_at - prev.created_at).total_seconds())
            if gap > max_gap_seconds:
                violations.append((sensor_id, prev, curr, gap))
            prev = curr

    return violations


def format_gap(seconds: int) -> str:
    mins = seconds // 60
    secs = seconds % 60
    return f"{mins}m {secs}s"


def main() -> int:
    ap = argparse.ArgumentParser(description="Check max time gap per sensor in a CSV export.")
    ap.add_argument("csv_path", help="Path to input CSV file")
    ap.add_argument("--max-minutes", type=int, default=70, help="Maximum allowed gap in minutes (default: 70)")
    ap.add_argument("--time-col", default="created_at", help="Timestamp column name (default: created_at)")
    ap.add_argument("--sensor-col", default="field2", help="Sensor ID column name (default: field2)")
    ap.add_argument("--entry-col", default="entry_id", help="Entry id column name (default: entry_id)")
    args = ap.parse_args()

    max_gap_seconds = args.max_minutes * 60

    try:
        grouped = read_csv_grouped(args.csv_path, args.time_col, args.sensor_col, args.entry_col)
    except Exception as e:
        print(f"[ERROR] {e}", file=sys.stderr)
        return 2

    violations = find_violations(grouped, max_gap_seconds)

    # Summary
    sensor_counts = {k: len(v) for k, v in grouped.items()}
    total_rows = sum(sensor_counts.values())
    print(f"Loaded {total_rows} rows across {len(sensor_counts)} sensors.")
    print(f"Max allowed gap: {args.max_minutes} minutes.\n")

    if not violations:
        print("OK: No gaps exceeded the threshold.")
        return 0

    print(f"FAIL: Found {len(violations)} gap(s) exceeding the threshold:\n")
    # Sort by largest gap first
    violations.sort(key=lambda x: x[3], reverse=True)

    for sensor_id, prev, curr, gap in violations:
        prev_id = prev.entry_id or "?"
        curr_id = curr.entry_id or "?"
        print(f"Sensor {sensor_id}: gap {format_gap(gap)}")
        print(f"  prev: entry_id={prev_id} created_at={prev.raw_created_at}")
        print(f"  curr: entry_id={curr_id} created_at={curr.raw_created_at}")
        print()

    # Non-zero exit so scripts can detect failure
    return 1


if __name__ == "__main__":
    raise SystemExit(main())
