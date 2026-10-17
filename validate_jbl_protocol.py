"""Verify the physical-button map against its recorded JBL ATT packets."""

import json
import hashlib
import os
import shutil
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parent
TSHARK = os.environ.get("TSHARK") or shutil.which("tshark") or "tshark"
MERGECAP = os.environ.get("MERGECAP") or shutil.which("mergecap") or "mergecap"


def read_jsonl(relative):
    return [json.loads(line) for line in (ROOT / relative).read_text(encoding="utf-8").splitlines()]


def verify_nordic_pairs(item, evidence):
    rows = read_jsonl(evidence["capture"])
    assert rows[0]["target"].upper() == os.environ["JBL_RESEARCH_REMOTE_MAC"].upper()
    assert len(evidence["press_lines"]) == evidence["verified_repetitions"]
    for line in evidence["press_lines"]:
        for row, value in zip(rows[line - 1:line + 1], [item["press"], item["release"]]):
            assert row["event"] == "value_changed" and row["subscribed"], (line, row)
            assert row["handle"] == int(item["att_handle"], 16) and row["hex"] == value, (line, row)
        assert rows[line - 1]["time"] < rows[line]["time"], line
    return len(evidence["press_lines"])


def verify_nordic_gatt(protocol):
    hid = protocol["gatt_hid"]
    rows = read_jsonl(hid["capture"])
    source = lambda entry: rows[entry["source_line"] - 1]
    declarations = [row for row in rows if row["event"] == "hid_characteristic"]
    assert len(hid["characteristics"]) == len(declarations) == 9
    services = rows[hid["services_source_line"] - 1]
    assert services["event"] == "services" and services["services"] == hid["services_discovery"]
    assert {"uuid": "1812", "start_handle": hid["service_start_handle"],
            "end_handle": hid["service_end_handle"]} in services["services"]
    for char in hid["characteristics"]:
        actual = source(char)
        assert actual["event"] == "hid_characteristic"
        for field in ["uuid", "declaration_handle", "value_handle", "properties"]:
            assert char[field] == actual[field], (char["value_handle"], field)
        if "read" in char:
            read = source(char["read"])
            assert read["event"] == "characteristic_read"
            assert (read["handle"], read["hex"]) == (char["value_handle"], char["read"]["hex"])
        if "read_error" in char:
            read = source(char["read_error"])
            assert read["event"] == "read_error"
            assert (read["handle"], read["error"]) == (char["value_handle"], char["read_error"]["error"])
        for desc in char["descriptors"]:
            actual = source(desc)
            assert actual["event"] == "hid_descriptor"
            assert (actual["characteristic_handle"], actual["handle"], actual["uuid"]) == (
                char["value_handle"], desc["handle"], desc["uuid"])
            if "read" in desc:
                actual = source(desc["read"])
                assert actual["event"] == "descriptor_read"
                assert (actual["handle"], actual["hex"]) == (desc["handle"], desc["read"]["hex"])
        if "notification_subscription_source_line" in char:
            actual = rows[char["notification_subscription_source_line"] - 1]
            assert actual["event"] == "subscribed" and actual["handle"] == char["value_handle"]
    references = {}
    for ref in hid["report_references"]:
        actual = source(ref)
        assert actual["event"] == "descriptor_read" and actual["uuid"] == "2908"
        for field in ["characteristic_handle", "handle", "hex", "report_id", "report_type"]:
            assert ref[field] == actual[field], field
        assert bytes.fromhex(ref["hex"]) == bytes([ref["report_id"], ref["report_type"]])
        references[ref["characteristic_handle"]] = (ref["report_id"], ref["report_type"])
    assert references == {47: (1, 1), 51: (1, 2), 54: (30, 1), 58: (30, 2), 61: (3, 1)}
    for item in protocol["buttons"].values():
        assert references[int(item["att_handle"], 16)] == (item["report_id"], item["report_type"])
    for uuid, field in [("2A4A", "hid_information_hex"), ("2A4E", "protocol_mode_hex")]:
        char = next(char for char in hid["characteristics"] if char["uuid"] == uuid)
        assert char["read"]["hex"] == hid[field]
    descriptor = protocol["hid_report_descriptor"]
    windows_bytes = (ROOT / descriptor["file"]).read_bytes()
    nordic_bytes = (ROOT / descriptor["gatt_file"]).read_bytes()
    actual = rows[descriptor["gatt_source_line"] - 1]
    assert actual["event"] == "characteristic_read" and actual["uuid"] == "2A4B"
    assert windows_bytes == nordic_bytes == bytes.fromhex(actual["hex"]) == bytes.fromhex(descriptor["hex"])
    assert len(nordic_bytes) == descriptor["length"] == 79
    assert hashlib.sha256(nordic_bytes).hexdigest() == descriptor["sha256"]
    security = protocol["security_observed"]
    assert security["capture"] == hid["capture"]
    assert any(r["event"] == "authentication" and r["status"] == 0 and r["bonded"] for r in rows)
    assert any(r["event"] == "security_changed" and r["securityMode"] == 1 and r["securityLevel"] == 2 for r in rows)
    return {"hid_characteristics": len(declarations), "report_references": len(references),
            "report_map_length": len(nordic_bytes), "report_map_sha256": descriptor["sha256"],
            "report_map_matches_windows": True, "input_report_ids": [1, 30, 3],
            "hid_information_hex": hid["hid_information_hex"], "protocol_mode_hex": hid["protocol_mode_hex"]}


def verify_advertising(protocol):
    adv = protocol["advertising"]
    args = [TSHARK, "-r", str(ROOT / adv["capture"]), "-Y", f"frame.number == {adv['frame']}"]
    actual = subprocess.check_output(args + ["-T", "fields", "-e", "nordic_ble.crcok",
        "-e", "btle.advertising_header.pdu_type", "-e", "btle.advertising_header.randomized_tx",
        "-e", "btle.advertising_address"], encoding="utf-8").strip().split("\t")
    assert actual == ["True", "0x00", "False", os.environ["JBL_RESEARCH_REMOTE_MAC"].lower()]
    packet = json.loads(subprocess.check_output(args + ["-T", "jsonraw"], encoding="utf-8"))[0]
    raw = packet["_source"]["layers"]["btle"]["btcommon.eir_ad.advertising_data_raw"][0]
    assert raw == adv["ad_data_hex"]
    data, fields, offset = bytes.fromhex(raw), {}, 0
    while offset < len(data):
        length = data[offset]
        assert length > 0 and offset + length < len(data)
        kind = data[offset + 1]
        assert kind not in fields
        fields[kind] = data[offset + 2:offset + length + 1]
        offset += length + 1
    assert set(fields) == {0x01, 0x03, 0x19, 0x09}
    assert fields[0x01].hex() == adv["flags_hex"] == "05"
    assert [f"{int.from_bytes(fields[0x03][i:i + 2], 'little'):04x}" for i in range(0, len(fields[0x03]), 2)] == adv["complete_16bit_service_uuids"]
    assert f"{int.from_bytes(fields[0x19], 'little'):04x}" == adv["appearance_hex"]
    assert fields[0x09].decode("utf-8") == adv["complete_local_name"]
    assert 0xff not in fields and adv["manufacturer_data_present"] is False
    return {"frame": adv["frame"], "crc_ok": True, "ad_data_hex": raw}


def main():
    if not os.environ.get("JBL_RESEARCH_REMOTE_MAC"):
        raise SystemExit("Private capture replay requires JBL_RESEARCH_REMOTE_MAC and the original captures/. For public source checks run firmware/esphome/check_jbl_config.py.")
    protocol = json.loads((ROOT / "jbl_protocol.json").read_text(encoding="utf-8"))
    captures = {}
    filtered = []
    pcap_buttons = {name: item for name, item in protocol["buttons"].items()
                    if item.get("capture_format", "pcapng") == "pcapng"}
    for relative in sorted({item["capture"] for item in pcap_buttons.values()}):
        source = ROOT / relative
        output = subprocess.check_output([
            TSHARK, "-r", str(source), "-Y",
            "bthci_acl.chandle == 0x0203 && btatt.opcode == 0x1b",
            "-T", "fields", "-e", "frame.number", "-e", "btatt.handle", "-e", "btatt.value",
        ], encoding="utf-8")
        captures[relative] = [line.split("\t") for line in output.splitlines() if line]
        target = source.with_name(source.stem + "_jbl_only.pcapng")
        subprocess.run([TSHARK, "-r", str(source), "-Y", "bthci_acl.chandle == 0x0203 && btatt",
                        "-w", str(target)], check=True)
        filtered.append(str(target))

    results = []
    for name, item in pcap_buttons.items():
        rows = captures[item["capture"]]
        by_frame = {int(row[0]): index for index, row in enumerate(rows)}
        assert len(item["press_frames"]) == item["verified_repetitions"], name
        for frame in item["press_frames"]:
            index = by_frame[frame]
            assert rows[index][1:] == [item["att_handle"], item["press"]], (name, frame)
            assert rows[index + 1][1:] == [item["att_handle"], item["release"]], (name, frame, "release")
        results.append({"button": name, "verified_press_release_pairs": len(item["press_frames"]),
                        "source_format": "pcapng"})

    pcap_pairs = sum(result["verified_press_release_pairs"] for result in results)
    assert len(pcap_buttons) == 12 and pcap_pairs == 25
    for name, item in protocol["buttons"].items():
        if item.get("capture_format") == "nordic_jsonl":
            results.append({"button": name, "verified_press_release_pairs": verify_nordic_pairs(item, item),
                            "source_format": "nordic_jsonl"})
    cross_checks = [{"button": evidence["button"], "verified_press_release_pairs":
                     verify_nordic_pairs(protocol["buttons"][evidence["button"]], evidence),
                     "source_format": "nordic_jsonl"} for evidence in protocol.get("cross_checks", [])]
    gatt = verify_nordic_gatt(protocol)
    advertising = verify_advertising(protocol)

    combined = ROOT / "captures" / "jbl_verified_buttons.pcapng"
    subprocess.run([MERGECAP, "-w", str(combined), *filtered], check=True)
    primary_pairs = sum(result["verified_press_release_pairs"] for result in results)
    report = {"validated_buttons": len(results), "results": results, "cross_checks": cross_checks,
              "verified_primary_press_release_pairs": primary_pairs,
              "verified_total_press_release_pairs": primary_pairs + sum(r["verified_press_release_pairs"] for r in cross_checks),
              "original_pcap_validation": {"buttons": len(pcap_buttons), "press_release_pairs": pcap_pairs},
              "nordic_gatt": gatt,
              "advertising": advertising,
              "target_only_capture": str(combined),
              "target_only_capture_scope": "The original 12 buttons captured via Windows HCI; Power and USB cross-check remain in Nordic JSONL.",
              "scope": "Capture consistency only. Hardware validation is tracked separately in evidence/validation-summary.json."}
    (ROOT / "captures" / "jbl_button_validation.json").write_text(
        json.dumps(report, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(report, ensure_ascii=False))


if __name__ == "__main__":
    main()
