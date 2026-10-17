"""Check the YAML against captured protocol data without loading secrets or hardware."""

import json
from pathlib import Path

import yaml


class Loader(yaml.SafeLoader):
    pass


class Secret(str):
    pass


Loader.add_constructor("!secret", lambda loader, node: Secret(loader.construct_scalar(node)))
Loader.add_constructor("!lambda", lambda loader, node: loader.construct_scalar(node))

folder = Path(__file__).resolve().parent
root = folder.parent.parent
config = yaml.load((folder / "jbl-4305p-bridge.yaml").read_text(encoding="utf-8"), Loader=Loader)
protocol = json.loads((root / "jbl_protocol.json").read_text(encoding="utf-8-sig"))
assert "build_path" not in config["esphome"]
assert config["esphome"]["min_version"] == "2026.9.1"
assert config["esp32"]["board"] == "esp32-c3-devkitm-1"
assert config["esp32"]["framework"]["version"] == "5.5.5"
for address, key in (
    (config["ble_client"][0]["mac_address"], "jbl4305p_remote_mac"),
    (config["esp32_ble_server"]["speaker_mac"], "jbl4305p_speaker_mac"),
):
    assert isinstance(address, Secret) and address == key
services = {service["uuid"]: service for service in config["esp32_ble_server"]["services"]}
hid = services["1812"]["characteristics"]
report_map = next(char["value"] for char in hid if char["uuid"] == "2A4B")
assert bytes(report_map).hex() == protocol["hid_report_descriptor"]["hex"]
reports = [char for char in hid if char["uuid"] == "2A4D"]
assert [next(desc["value"] for desc in char["descriptors"] if desc["uuid"] == "2908") for char in reports] == [[1, 1], [1, 2], [30, 1], [30, 2], [3, 1]]
assert [char.get("notify", False) for char in reports] == [True, False, True, False, True]
for char in reports:
    if char.get("notify"):
        assert [desc["uuid"] for desc in char["descriptors"]] == ["2902", "2908"]
        assert char["descriptors"][0]["value"] == [0, 0]
remote_buttons = [button for button in config["button"] if button.get("entity_category") != "config"]
assert len(remote_buttons) == 13
for button in remote_buttons:
    key = button["id"].removeprefix("jbl_button_")
    action = button["on_press"][0]["script.execute"]
    evidence = protocol["buttons"].get(key)
    expected_id = {"0x002f": 1, "0x003d": 3}[evidence["att_handle"].lower()]
    assert action["report_id"] == expected_id, key
    assert bytes(action["payload"]).hex() == evidence["press"], key
    assert len(action["payload"]) == len(bytes.fromhex(evidence["release"])) == (8 if expected_id == 1 else 2), key
assert config["script"][0]["mode"] == "queued"
assert config["api"]["reboot_timeout"] == config["wifi"]["reboot_timeout"] == "0s"
print("OK: Portable configuration, Report Map, 5 Report References, 13 key payloads and release lengths match public protocol evidence.")
