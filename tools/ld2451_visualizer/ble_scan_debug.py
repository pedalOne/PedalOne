#!/usr/bin/env python3
"""Print every BLE advertisement seen by Bleak, without name/service filtering."""

from __future__ import annotations

import argparse
import asyncio
from datetime import datetime

from bleak import BleakScanner


async def scan(seconds: float) -> None:
    seen: set[str] = set()

    def detected(device, advertisement) -> None:
        name = advertisement.local_name or device.name or "<unnamed>"
        services = ",".join(advertisement.service_uuids) or "-"
        manufacturer = ",".join(
            f"0x{company:04X}:{data.hex()}"
            for company, data in advertisement.manufacturer_data.items()
        ) or "-"
        marker = "RADAR?" if "LD2451" in name.upper() or name.upper().startswith("HLK") else "      "
        print(
            f"{datetime.now().strftime('%H:%M:%S.%f')[:-3]} {marker} "
            f"{name!r} {device.address} RSSI={advertisement.rssi} "
            f"services={services} manufacturer={manufacturer}",
            flush=True,
        )
        seen.add(device.address)

    print(f"Unfiltered BLE scan for {seconds:g} seconds…", flush=True)
    async with BleakScanner(detection_callback=detected):
        await asyncio.sleep(seconds)
    print(f"Scan complete: {len(seen)} unique device(s).", flush=True)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--seconds", type=float, default=20)
    args = parser.parse_args()
    asyncio.run(scan(args.seconds))


if __name__ == "__main__":
    main()
