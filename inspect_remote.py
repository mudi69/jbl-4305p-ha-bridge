"""Pair this computer with the identified JBL remote and record its GATT data."""

import argparse
import asyncio
from datetime import datetime, timezone
import json
from pathlib import Path

from bleak import BleakClient, BleakScanner
from bleak.backends.device import BLEDevice

async def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--remote-mac", required=True, help="Confirmed remote public BLE MAC; never the speaker Wi-Fi MAC")
    parser.add_argument("--paired", action="store_true", help="Use the known Windows paired device without scanning")
    parser.add_argument("--seconds", type=int, default=300)
    args = parser.parse_args()
    target = args.remote_mac
    if args.seconds <= 0:
        parser.error("--seconds must be positive")
    path = Path(__file__).resolve().parent / "captures" / f"jbl_endpoint_{datetime.now():%Y%m%d_%H%M%S_%f}.jsonl"
    path.parent.mkdir(exist_ok=True)
    with path.open("x", encoding="utf-8", buffering=1) as log:
        def record(kind, **fields):
            event = {"type": kind, "utc": datetime.now(timezone.utc).isoformat(), **fields}
            log.write(json.dumps(event, ensure_ascii=False) + "\n")
            print(json.dumps(event, ensure_ascii=False), flush=True)

        record("waiting_for_remote", address=target, file=str(path), timeout_seconds=300, paired=args.paired)
        device = (BLEDevice(target, "JBL 4305P REMOTE", None) if args.paired else
                  await BleakScanner.find_device_by_address(target, timeout=300))
        if device is None:
            record("not_found")
            return
        record("found", address=device.address, name=device.name)
        try:
            async with BleakClient(device, pair=True, timeout=45,
                                   winrt={"use_cached_services": False}) as client:
                record("connected", address=target)
                for service in client.services:
                    record("service", uuid=service.uuid, handle=service.handle,
                           description=service.description)
                    for characteristic in service.characteristics:
                        details = {"uuid": characteristic.uuid, "handle": characteristic.handle}
                        record("characteristic", **details, service=service.uuid,
                               properties=characteristic.properties,
                               description=characteristic.description)
                        if "read" in characteristic.properties:
                            try:
                                value = await asyncio.wait_for(client.read_gatt_char(characteristic), 5)
                                record("read", **details, value=bytes(value).hex())
                            except Exception as exc:
                                record("read_error", **details, error=str(exc))
                        for descriptor in characteristic.descriptors:
                            record("descriptor", characteristic=characteristic.handle,
                                   uuid=descriptor.uuid, handle=descriptor.handle)
                        if {"notify", "indicate"}.intersection(characteristic.properties):
                            try:
                                await asyncio.wait_for(client.start_notify(
                                    characteristic,
                                    lambda sender, data: record("notification", uuid=sender.uuid,
                                                                handle=sender.handle, value=bytes(data).hex())), 5)
                                record("subscribed", **details)
                            except Exception as exc:
                                record("subscribe_error", **details, error=str(exc))
                record("ready_for_buttons", listen_seconds=args.seconds)
                await asyncio.sleep(args.seconds)
        except Exception as exc:
            record("error", error=repr(exc))
        finally:
            record("session_end")


if __name__ == "__main__":
    try:
        asyncio.run(main())
    except KeyboardInterrupt:
        pass
