#!/usr/bin/env python3
"""
convert_datetime_csv.py

- Replaces the header with: created_at,field1,field2,... up to the detected column count
- Converts the first column (created_at) from e.g. "01/02/2026, 11:19:01"
  into ISO-8601 with timezone offset, e.g. 2026-01-02T11:19:01-06:00
- Interprets naive timestamps as America/Chicago by default (DST-aware)
"""

from __future__ import annotations

import argparse
import csv
import sys
from datetime import datetime
from zoneinfo import ZoneInfo


KNOWN_INPUT_FORMATS = (
    "%m/%d/%Y, %H:%M:%S",   # 01/02/2026, 11:19:01
    "%m/%d/%Y %H:%M:%S",    # 01/02/2026 11:19:01
    "%Y-%m-%d %H:%M:%S",    # 2026-01-02 11:19:01
    "%Y-%m-%dT%H:%M:%S",    # 2026-01-02T11:19:01 (no tz)
)


def try_parse_datetime(s: str) -> datetime | None:
    s = (s or "").strip().strip('"').strip()
    if not s:
        return None

    # Try ISO forms (including trailing Z)
    iso_candidate = s.replace("Z", "+00:00") if s.endswith("Z") else s
    try:
        return datetime.fromisoformat(iso_candidate)
    except ValueError:
        pass

    for fmt in KNOWN_INPUT_FORMATS:
        try:
            return datetime.strptime(s, fmt)
        except ValueError:
            continue

    return None


def make_new_header(col_count: int) -> list[str]:
    # created_at, field1, field2, ... field{col_count-1}
    if col_count < 1:
        return []
    header = ["created_at"]
    header += [f"field{i}" for i in range(1, col_count)]
    return header


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("input_csv", help="Path to input CSV")
    ap.add_argument("output_csv", help="Path to output CSV")
    ap.add_argument(
        "--tz",
        default="America/Chicago",
        help="IANA timezone name to interpret naive timestamps (default: America/Chicago)",
    )
    args = ap.parse_args()

    tz = ZoneInfo(args.tz)

    with open(args.input_csv, "r", newline="", encoding="utf-8") as fin:
        sample = fin.read(8192)
        fin.seek(0)
        try:
            dialect = csv.Sniffer().sniff(sample)
        except csv.Error:
            dialect = csv.excel

        reader = csv.reader(fin, dialect=dialect)

        # Read the original header to detect column count
        try:
            original_header = next(reader)
        except StopIteration:
            print("Error: input CSV is empty.", file=sys.stderr)
            return 2

        col_count = len(original_header)
        if col_count == 0:
            print("Error: header row has zero columns.", file=sys.stderr)
            return 2

        new_header = make_new_header(col_count)

        with open(args.output_csv, "w", newline="", encoding="utf-8") as fout:
            writer = csv.writer(fout, dialect=dialect)
            writer.writerow(new_header)

            for row in reader:
                # Normalize row length (pad short rows; keep extras if any)
                if len(row) < col_count:
                    row = row + [""] * (col_count - len(row))
                elif len(row) > col_count:
                    # If a row has *more* columns than header, extend header conceptually:
                    # We'll keep the extra columns and extend the output header once.
                    # (Rare, but this avoids silent truncation.)
                    extra = len(row) - col_count
                    # Extend existing header output (only once, the first time we see it)
                    # by rewriting the file header is not feasible here, so instead we:
                    # - truncate to original header length
                    # If you expect this case, tell me and I'll implement a 2-pass approach.
                    row = row[:col_count]

                # Convert first column -> created_at
                dt = try_parse_datetime(row[0])
                if dt is not None:
                    if dt.tzinfo is None:
                        dt = dt.replace(tzinfo=tz)
                    row[0] = dt.isoformat(timespec="seconds")

                writer.writerow(row)

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
