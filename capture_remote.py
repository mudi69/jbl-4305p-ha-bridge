"""Capture the identified JBL remote using Nordic's unmodified Python API."""

import argparse
from datetime import datetime
from pathlib import Path
import sys
import time

ROOT = Path(__file__).resolve().parent

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--seconds", type=int, default=600)
parser.add_argument("--scan", action="store_true", help="Scan adverts for a setup check")
parser.add_argument("--port", required=True, help="Confirmed Nordic sniffer serial port")
parser.add_argument("--sdk-path", type=Path, required=True, help="Official Nordic legacy SDK extcap folder containing SnifferAPI")
parser.add_argument("--remote-mac", help="Confirmed public BLE MAC; required for follow mode")
args = parser.parse_args()
if args.seconds <= 0:
    parser.error("--seconds must be positive")
if not args.scan:
    try:
        remote_bytes = bytes.fromhex((args.remote_mac or "").replace(":", ""))
        assert len(remote_bytes) == 6
    except (ValueError, AssertionError):
        parser.error("--remote-mac must be a six-byte MAC in follow mode")
if not (args.sdk_path / "SnifferAPI").is_dir():
    parser.error("--sdk-path must contain the official SnifferAPI directory")
sys.path.insert(0, str(args.sdk_path.resolve()))

mode = "scan" if args.scan else "follow"
output = ROOT / "captures" / f"jbl_legacy_{mode}_{datetime.now():%Y%m%d_%H%M%S_%f}.pcap"
output.parent.mkdir(exist_ok=True)
from SnifferAPI import Logger
Logger.setLogFileName(str(output.with_suffix(".log")))
from SnifferAPI import Devices, Sniffer

sniffer = Sniffer.Sniffer(portnum=args.port, baudrate=1000000, capture_file_path=str(output))
count = 0
try:
    sniffer.start()
    sniffer.getFirmwareVersion()
    sniffer.getTimestamp()
    sniffer.setAdvHopSequence([37, 38, 39])
    sniffer.scan(findScanRsp=True, findAux=True)
    time.sleep(0.5)
    if not args.scan:
        # Nordic API expects display-order bytes, then 0 for a public address.
        device = Devices.Device(list(remote_bytes) + [0], "JBL candidate", 0)
        sniffer.addDevice(device)
        sniffer.follow(device)
    print(f"READY mode={mode} file={output}", flush=True)
    deadline = time.monotonic() + args.seconds
    while time.monotonic() < deadline:
        if not sniffer.is_alive():
            raise RuntimeError("Nordic capture thread stopped; inspect its log")
        count += len(sniffer.getPackets())
        time.sleep(0.1)
except KeyboardInterrupt:
    pass
finally:
    sniffer.doExit(join=True)
    # Finish the SDK's flush thread before its shutdown closes the log file.
    Logger.logFlusher.stop()
    Logger.logFlusher.join()
    Logger.shutdownLogger()
    print(f"STOPPED packets_read={count} uart_missed={sniffer.missedPackets} file={output}", flush=True)
