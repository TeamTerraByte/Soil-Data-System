#!/usr/bin/env python3
"""
nsrdb_panel_sunlight.py

Fetch NSRDB (NREL) PSM3 time-series solar data at a coordinate and estimate
how much sunlight a solar panel receives over a user-specified time range.

Outputs:
- NSRDB irradiance: GHI, DNI, DHI (W/m^2)
- Horizontal-plane energy over the range: kWh/m^2
- Optional POA (Plane-Of-Array) irradiance/energy if pvlib is installed

Examples:
  # Basic (no extra libs), UTC timestamps:
  python3 nsrdb_panel_sunlight.py \
    --api-key YOUR_KEY --email you@example.com \
    --lat 27.8006 --lon -97.3964 \
    --start 2026-01-24T00:00:00Z --end 2026-01-26T00:00:00Z \
    --out nsrdb_range.csv

  # With panel geometry (tilt/azimuth). If pvlib is installed, adds POA columns:
  python3 nsrdb_panel_sunlight.py \
    --api-key YOUR_KEY --email you@example.com \
    --lat 27.8006 --lon -97.3964 \
    --start 2026-01-24 --end 2026-01-26 \
    --tilt 25 --azimuth 180 \
    --tz America/Chicago \
    --out nsrdb_panel.csv
"""

from __future__ import annotations

import argparse
import csv
import io
import sys
from dataclasses import dataclass
from datetime import datetime, timezone
from typing import Dict, List, Optional, Tuple

import requests


NSRDB_PSM3_CSV_URL = "https://developer.nrel.gov/api/nsrdb/v2/solar/psm3-download.csv"


def parse_dt(s: str, default_tz: timezone) -> datetime:
    """
    Parse:
      - YYYY-MM-DD
      - YYYY-MM-DDTHH:MM:SSZ
      - YYYY-MM-DDTHH:MM:SS±HH:MM
    Returns an aware datetime.
    """
    s = s.strip()
    if len(s) == 10 and s[4] == "-" and s[7] == "-":
        # Date only -> midnight in default tz
        dt = datetime.fromisoformat(s)
        return dt.replace(tzinfo=default_tz)

    # Handle trailing Z
    if s.endswith("Z"):
        s2 = s[:-1] + "+00:00"
        return datetime.fromisoformat(s2)

    dt = datetime.fromisoformat(s)
    if dt.tzinfo is None:
        return dt.replace(tzinfo=default_tz)
    return dt


@dataclass
class NsrdbRow:
    ts_utc: datetime
    ghi: float
    dni: float
    dhi: float
    raw: Dict[str, str]


def nsrdb_download_csv(
    api_key: str,
    email: str,
    lat: float,
    lon: float,
    year: int,
    interval_minutes: int,
    attributes: List[str],
    timeout_s: int = 60,
) -> bytes:
    """
    Download a full-year PSM3 CSV for the given coordinate, then we'll filter locally.
    """
    params = {
        "api_key": api_key,
        "email": email,
        "wkt": f"POINT({lon} {lat})",
        "names": str(year),                 # returns that year
        "interval": str(interval_minutes),  # 60 for hourly, 30 for half-hour
        "utc": "true",
        "leap_day": "false",
        "attributes": ",".join(attributes),
        "full_name": "nsrdb_panel_sunlight.py",
        "affiliation": "personal",
        "reason": "estimate_sunlight",
    }

    r = requests.get(NSRDB_PSM3_CSV_URL, params=params, timeout=timeout_s)
    r.raise_for_status()
    return r.content


def find_header_and_rows(csv_bytes: bytes) -> Tuple[List[str], List[List[str]]]:
    """
    NSRDB CSV includes metadata lines before the actual CSV header.
    We'll locate the header line by finding a line that starts with "Year,Month,Day".
    """
    text = csv_bytes.decode("utf-8", errors="replace")
    lines = text.splitlines()

    header_idx = None
    for i, line in enumerate(lines):
        if line.startswith("Year,Month,Day"):
            header_idx = i
            break

    if header_idx is None:
        # As a fallback, try to find a line containing the core fields
        for i, line in enumerate(lines):
            if "Year" in line and "Month" in line and "Day" in line and "GHI" in line:
                header_idx = i
                break

    if header_idx is None:
        raise ValueError("Could not find NSRDB CSV header (Year,Month,Day...).")

    reader = csv.reader(lines[header_idx:])
    rows = list(reader)
    header = rows[0]
    data_rows = rows[1:]
    return header, data_rows


def parse_nsrdb_rows(header: List[str], data_rows: List[List[str]]) -> List[NsrdbRow]:
    """
    Convert NSRDB row arrays into typed rows with UTC timestamps.
    """
    col = {name: idx for idx, name in enumerate(header)}

    required = ["Year", "Month", "Day", "Hour", "Minute", "GHI", "DNI", "DHI"]
    missing = [k for k in required if k not in col]
    if missing:
        raise ValueError(f"Missing required columns: {missing}. Got columns: {header}")

    out: List[NsrdbRow] = []
    for r in data_rows:
        if len(r) < len(header):
            # Skip malformed
            continue

        year = int(r[col["Year"]])
        month = int(r[col["Month"]])
        day = int(r[col["Day"]])
        hour = int(r[col["Hour"]])
        minute = int(r[col["Minute"]])

        ts = datetime(year, month, day, hour, minute, tzinfo=timezone.utc)

        def f(name: str) -> float:
            v = r[col[name]].strip()
            if v == "":
                return float("nan")
            return float(v)

        raw_map = {h: r[i] for h, i in col.items() if i < len(r)}

        out.append(
            NsrdbRow(
                ts_utc=ts,
                ghi=f("GHI"),
                dni=f("DNI"),
                dhi=f("DHI"),
                raw=raw_map,
            )
        )

    return out


def filter_by_range(rows: List[NsrdbRow], start_utc: datetime, end_utc: datetime) -> List[NsrdbRow]:
    """
    Keep rows with start_utc <= ts < end_utc.
    """
    return [x for x in rows if start_utc <= x.ts_utc < end_utc]


def energy_kwh_per_m2(rows: List[NsrdbRow], interval_minutes: int) -> float:
    """
    Integrate GHI over time range to get kWh/m^2 on a horizontal plane.
    GHI is W/m^2. Energy = sum(GHI * dt_hours)/1000.
    """
    dt_hours = interval_minutes / 60.0
    total_wh_m2 = 0.0
    for x in rows:
        if x.ghi == x.ghi:  # not NaN
            total_wh_m2 += x.ghi * dt_hours
    return total_wh_m2 / 1000.0


def maybe_add_poa_with_pvlib(
    rows: List[NsrdbRow],
    lat: float,
    lon: float,
    tilt_deg: float,
    azimuth_deg: float,
    tz_name: str,
) -> Optional[Tuple[List[float], List[float]]]:
    """
    Optional: compute POA (Plane-Of-Array) irradiance using pvlib if available.
    Returns (poa_wm2_list, poa_energy_kwh_m2_daily_placeholder_list).
    We'll just compute POA W/m^2 per row; energy can be integrated like GHI.
    """
    try:
        import pandas as pd  # type: ignore
        import pvlib  # type: ignore
    except Exception:
        return None

    # Build a time index in UTC
    times = [x.ts_utc for x in rows]
    idx = pd.DatetimeIndex(times).tz_convert("UTC")

    # Solar position
    solpos = pvlib.solarposition.get_solarposition(idx, lat, lon)

    # Use a transposition model via get_total_irradiance
    # (uses DNI/DHI/GHI + solar position + surface geometry)
    ghi = pd.Series([x.ghi for x in rows], index=idx)
    dni = pd.Series([x.dni for x in rows], index=idx)
    dhi = pd.Series([x.dhi for x in rows], index=idx)

    poa = pvlib.irradiance.get_total_irradiance(
        surface_tilt=tilt_deg,
        surface_azimuth=azimuth_deg,
        solar_zenith=solpos["zenith"],
        solar_azimuth=solpos["azimuth"],
        dni=dni,
        ghi=ghi,
        dhi=dhi,
        model="perez",
    )

    poa_global = poa["poa_global"].astype(float).tolist()
    return (poa_global, [])


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--api-key", required=True, help="NREL developer API key")
    ap.add_argument("--email", required=True, help="Email used with NREL API key")
    ap.add_argument("--lat", type=float, required=True)
    ap.add_argument("--lon", type=float, required=True)
    ap.add_argument("--start", required=True, help="Start datetime/date (ISO). Ex: 2026-01-24 or 2026-01-24T00:00:00Z")
    ap.add_argument("--end", required=True, help="End datetime/date (ISO). Ex: 2026-01-26 or 2026-01-26T00:00:00Z")
    ap.add_argument("--interval", type=int, default=60, choices=[30, 60], help="Minutes per sample (30 or 60)")
    ap.add_argument("--tz", default="UTC", help="Only used for interpreting date-only inputs. Ex: America/Chicago")
    ap.add_argument("--tilt", type=float, default=None, help="Panel tilt degrees (0=flat). If set, tries to compute POA with pvlib.")
    ap.add_argument("--azimuth", type=float, default=None, help="Panel azimuth degrees (180=south in N hemisphere).")
    ap.add_argument("--out", default="nsrdb_filtered.csv", help="Output CSV filename")

    args = ap.parse_args()

    # Interpret start/end
    # If date-only is provided, we treat it as midnight in the chosen tz, then convert to UTC.
    # We'll use zoneinfo (stdlib) when available.
    try:
        from zoneinfo import ZoneInfo  # py3.9+
        tz = ZoneInfo(args.tz)
        default_tz = tz
    except Exception:
        default_tz = timezone.utc

    start_dt = parse_dt(args.start, default_tz)
    end_dt = parse_dt(args.end, default_tz)

    # Normalize to UTC
    if start_dt.tzinfo is None:
        start_dt = start_dt.replace(tzinfo=timezone.utc)
    if end_dt.tzinfo is None:
        end_dt = end_dt.replace(tzinfo=timezone.utc)

    start_utc = start_dt.astimezone(timezone.utc)
    end_utc = end_dt.astimezone(timezone.utc)

    if end_utc <= start_utc:
        print("ERROR: --end must be after --start", file=sys.stderr)
        return 2

    # Download years spanned by [start, end)
    years = sorted({start_utc.year, end_utc.year})
    all_rows: List[NsrdbRow] = []

    # Minimal set of attributes we need
    attrs = ["ghi", "dni", "dhi"]

    for y in years:
        blob = nsrdb_download_csv(
            api_key=args.api_key,
            email=args.email,
            lat=args.lat,
            lon=args.lon,
            year=y,
            interval_minutes=args.interval,
            attributes=attrs,
        )
        header, data_rows = find_header_and_rows(blob)
        parsed = parse_nsrdb_rows(header, data_rows)
        all_rows.extend(parsed)

    # Filter
    kept = filter_by_range(all_rows, start_utc, end_utc)

    if not kept:
        print("No rows in the requested range after filtering. Check your dates/timezone.", file=sys.stderr)
        return 1

    # Horizontal energy (kWh/m^2) over the range
    horiz_kwh_m2 = energy_kwh_per_m2(kept, args.interval)

    # Optional POA
    poa_global: Optional[List[float]] = None
    if args.tilt is not None and args.azimuth is not None:
        poa = maybe_add_poa_with_pvlib(
            rows=kept,
            lat=args.lat,
            lon=args.lon,
            tilt_deg=args.tilt,
            azimuth_deg=args.azimuth,
            tz_name=args.tz,
        )
        if poa is None:
            print("NOTE: pvlib not installed; skipping POA estimate. Install with: pip install pvlib", file=sys.stderr)
        else:
            poa_global = poa[0]

    # Write output CSV
    with open(args.out, "w", newline="", encoding="utf-8") as f:
        w = csv.writer(f)
        w.writerow(["ts_utc", "GHI_Wm2", "DNI_Wm2", "DHI_Wm2"] + (["POA_Wm2"] if poa_global else []))
        for i, x in enumerate(kept):
            row = [x.ts_utc.isoformat().replace("+00:00", "Z"), x.ghi, x.dni, x.dhi]
            if poa_global:
                row.append(poa_global[i])
            w.writerow(row)

    # Print summary
    print(f"Wrote: {args.out}")
    print(f"Range (UTC): {start_utc.isoformat()} → {end_utc.isoformat()}")
    print(f"Horizontal-plane sunlight energy (integrated GHI): {horiz_kwh_m2:.3f} kWh/m^2 over range")
    if poa_global:
        # integrate POA too
        dt_hours = args.interval / 60.0
        poa_wh_m2 = sum(v * dt_hours for v in poa_global if v == v)
        print(f"Panel-plane sunlight energy (integrated POA): {(poa_wh_m2/1000.0):.3f} kWh/m^2 over range")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
