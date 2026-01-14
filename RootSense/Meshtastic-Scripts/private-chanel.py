#!/usr/bin/env python3
"""

Ensure a Meshtastic device has a private (encrypted) channel configured.
- If a matching private channel already exists, do nothing (idempotent).
- Otherwise, set the channel name and generate a new AES-256 PSK, then write it.

Usage examples:
  python ensure_private_channel.py --name "MyPrivateChannel"
  python ensure_private_channel.py --name "MyPrivateChannel" --port COM5
  python ensure_private_channel.py --name "MyPrivateChannel" --index 1
  python ensure_private_channel.py --name "MyPrivateChannel" --index 0 --force-regen
"""

from __future__ import annotations

import argparse
import base64
import binascii
import sys
from typing import Optional, Tuple

import meshtastic
import meshtastic.serial_interface
import meshtastic.util


def b64(psk_bytes: bytes) -> str:
    return base64.b64encode(psk_bytes).decode("ascii")


def hexstr(psk_bytes: bytes) -> str:
    return "0x" + binascii.hexlify(psk_bytes).decode("ascii")


def psk_kind(psk_bytes: bytes) -> str:
    """Human-readable (privacy-preserving) classification."""
    return meshtastic.util.pskToString(psk_bytes)


def is_private_psk(psk_bytes: bytes) -> bool:
    """
    Treat as "private" if it's a real AES key (16 or 32 bytes).
    Excludes:
      - unencrypted (0 bytes or 1 byte 0x00)
      - default (1 byte 0x01)
      - simpleN (1 byte > 0x01)
    """
    return len(psk_bytes) in (16, 32)


def find_matching_channel(local_node, name: str) -> Optional[int]:
    """
    Return channel index if any channel's name matches exactly.
    """
    for i, ch in enumerate(local_node.channels):
        try:
            ch_name = ch.settings.name
        except Exception:
            continue
        if ch_name == name:
            return i
    return None


def channel_summary(local_node, idx: int) -> str:
    ch = local_node.channels[idx]
    nm = getattr(ch.settings, "name", "")
    pk = getattr(ch.settings, "psk", b"")
    return f"index={idx} name={nm!r} psk={psk_kind(pk)} psk_len={len(pk)}"


def ensure_private_channel(
    iface,
    name: str,
    preferred_index: Optional[int],
    force_regen: bool,
) -> Tuple[int, bytes, bool]:
    """
    Returns: (channel_index_used, psk_bytes, changed?)
    """
    local = iface.localNode

    # Make sure we have channels loaded before reading/modifying.
    local.waitForConfig("channels")

    # If the channel name already exists anywhere, prefer that index.
    existing_idx = find_matching_channel(local, name)
    idx = existing_idx if existing_idx is not None else (preferred_index if preferred_index is not None else 0)

    # Basic bounds check (some firmwares expose a fixed number of channel slots)
    if idx < 0 or idx >= len(local.channels):
        raise IndexError(f"Channel index {idx} out of range. Device reports {len(local.channels)} channel slots.")

    ch = local.channels[idx]
    current_name = getattr(ch.settings, "name", "")
    current_psk = getattr(ch.settings, "psk", b"")

    # If we found an existing channel by name, check if it's already private.
    if existing_idx is not None and is_private_psk(current_psk) and not force_regen:
        return idx, current_psk, False

    # If name matches preferred slot but PSK isn't private, we will set/upgrade it.
    changed = False

    if current_name != name:
        ch.settings.name = name
        changed = True

    if force_regen or not is_private_psk(current_psk):
        new_psk = meshtastic.util.genPSK256()  # 32 random bytes (AES-256)
        ch.settings.psk = new_psk
        changed = True
    else:
        new_psk = current_psk

    if changed:
        local.writeChannel(idx)

    return idx, new_psk, changed


def main() -> int:
    ap = argparse.ArgumentParser(description="Ensure a Meshtastic private (encrypted) channel exists on a device.")
    ap.add_argument("--name", required=True, help="Channel name (must match across devices).")
    ap.add_argument("--port", default=None, help="Serial port/device path (e.g., COM5, /dev/ttyUSB0).")
    ap.add_argument("--index", type=int, default=None, help="Preferred channel index (default: 0).")
    ap.add_argument("--force-regen", action="store_true", help="Force-generate a new PSK even if already private.")
    args = ap.parse_args()

    # Connect
    try:
        if args.port:
            iface = meshtastic.serial_interface.SerialInterface(devPath=args.port)
        else:
            iface = meshtastic.serial_interface.SerialInterface()
    except Exception as e:
        print(f"ERROR: Failed to connect to Meshtastic device: {e}", file=sys.stderr)
        return 2

    try:
        # Print current state for visibility
        iface.localNode.waitForConfig("channels")
        print("Current channels:")
        for i in range(len(iface.localNode.channels)):
            try:
                print("  -", channel_summary(iface.localNode, i))
            except Exception:
                pass

        idx, psk_bytes, changed = ensure_private_channel(
            iface=iface,
            name=args.name,
            preferred_index=args.index,
            force_regen=args.force_regen,
        )

        print("\nResult:")
        print(" ", channel_summary(iface.localNode, idx))
        if changed:
            print("\nCHANGED: Wrote private channel configuration to device.")
        else:
            print("\nUNCHANGED: Matching private channel already existed; no write performed.")

        # Only print shareable PSK material if it is a real AES key.
        if is_private_psk(psk_bytes):
            print("\nShare this PSK to join other devices:")
            print("  base64:", b64(psk_bytes))
            print("  hex:   ", hexstr(psk_bytes))
        else:
            print("\nWARNING: PSK is not a private AES key; something is off.")

        return 0

    finally:
        try:
            iface.close()
        except Exception:
            pass


if __name__ == "__main__":
    raise SystemExit(main())
