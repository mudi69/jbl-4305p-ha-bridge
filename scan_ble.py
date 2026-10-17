"""Record Windows-decoded BLE advertisements; does not pair or capture connections."""

import argparse
import asyncio
import json
from datetime import datetime, timezone
from pathlib import Path

from bleak import BleakScanner


async def capture(seconds, label, mode):
    folder = Path(__file__).resolve().parent / "captures"
    folder.mkdir(exist_ok=True)
    path = folder / (datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%S_%fZ") + ".jsonl")
    devices = {}
    events = 0
    with path.open("x", encoding="utf-8", buffering=1) as output:
        def record(kind, **fields):
            output.write(json.dumps({"type": kind, "utc": datetime.now(timezone.utc).isoformat(),
                                     **fields}, ensure_ascii=False) + "\n")

        def received(device, advertisement):
            nonlocal events
            events += 1
            data = {
                "address": device.address,
                "name": advertisement.local_name,
                "rssi": advertisement.rssi,
                "service_uuids": advertisement.service_uuids,
                "manufacturer_data": {str(k): v.hex() for k, v in advertisement.manufacturer_data.items()},
                "service_data": {k: v.hex() for k, v in advertisement.service_data.items()},
            }
            record("advertisement", **data)
            devices[device.address] = data

        record("start", label=label, seconds=seconds, mode=mode,
               scope="OS-decoded advertisement events, not raw radio packets")
        print(f"LOG: {path}", flush=True)
        try:
            async with BleakScanner(received, scanning_mode=mode):
                print(f"SCANNING: {seconds} seconds ({label})", flush=True)
                await asyncio.sleep(seconds)
        finally:
            record("end", events=events, addresses=len(devices))
            print(json.dumps({"events": events, "addresses": len(devices), "file": str(path)}), flush=True)
    for data in sorted(devices.values(), key=lambda item: item["rssi"], reverse=True):
        print(json.dumps(data, ensure_ascii=False), flush=True)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--seconds", type=int, default=20)
    parser.add_argument("--label", default="baseline")
    parser.add_argument("--mode", choices=["active", "passive"], default="active")
    args = parser.parse_args()
    if not 1 <= args.seconds <= 3600:
        parser.error("--seconds must be between 1 and 3600")
    asyncio.run(capture(args.seconds, args.label, args.mode))
