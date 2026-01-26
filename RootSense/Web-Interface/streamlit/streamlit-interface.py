#!/usr/bin/env python3

from datetime import datetime, timezone
import requests


def ensure_utc(dt: datetime) -> datetime:
    """
    Ensure dt is timezone-aware and in UTC.
    - Naive datetime => assumed UTC
    - Aware datetime => converted to UTC
    """
    if dt.tzinfo is None:
        return dt.replace(tzinfo=timezone.utc)
    return dt.astimezone(timezone.utc)


def format_for_thingspeak_utc(dt: datetime) -> str:
    """
    Format datetime for ThingSpeak start/end parameters:
    'YYYY-MM-DD HH:MM:SS' (UTC)
    """
    dt_utc = ensure_utc(dt)
    return dt_utc.strftime("%Y-%m-%d %H:%M:%S")


def fetch_feeds_between_utc(
    channel_id: int,
    start: datetime,
    end: datetime,
    read_api_key: str | None = None,
    fmt: str = "json",   # json | csv | xml
    timeout_s: int = 15,
):
    """
    Fetch ThingSpeak feeds between UTC start/end datetimes.
    """
    start_utc = ensure_utc(start)
    end_utc = ensure_utc(end)

    if start_utc >= end_utc:
        raise ValueError("start must be earlier than end")

    if fmt not in {"json", "csv", "xml"}:
        raise ValueError("fmt must be json, csv, or xml")

    url = f"https://api.thingspeak.com/channels/{channel_id}/feeds.{fmt}"

    params = {
        "start": format_for_thingspeak_utc(start_utc),
        "end": format_for_thingspeak_utc(end_utc),
    }

    if read_api_key:
        params["api_key"] = read_api_key

    response = requests.get(url, params=params, timeout=timeout_s)
    response.raise_for_status()

    return response.json() if fmt == "json" else response.text


def main():
    CHANNEL_ID = 3002040
    READ_API_KEY = "25U4UME7L2A571GY"  # set if channel is private

    start_dt = datetime(2026, 1, 16, 0, 0, 0, tzinfo=timezone.utc)
    end_dt = datetime(2026, 1, 21, 23, 59, 59, tzinfo=timezone.utc)

    data = fetch_feeds_between_utc(
        channel_id=CHANNEL_ID,
        start=start_dt,
        end=end_dt,
        read_api_key=READ_API_KEY,
        fmt="json",
    )

    feeds = data.get("feeds", [])
    print(f"Fetched {len(feeds)} rows")

    for row in feeds[:5]:
        print(row)


if __name__ == "__main__":
    main()
