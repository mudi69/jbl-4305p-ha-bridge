"""Cross-check the compiled mapping against original captured protocol evidence."""
import json
from pathlib import Path
import re
import yaml

folder = Path(__file__).resolve().parent
protocol = json.loads((folder.parents[3] / "jbl_protocol.json").read_text(encoding="utf-8"))
source = (folder / "received_keys.h").read_text(encoding="utf-8")
entries = re.findall(r'\{([13]), \{([^}]+)\}, "([a-z_]+)", "([^"]+)"\}', source)
assert len(entries) == len(protocol["buttons"]) == 13
assert len({name for _, _, name, _ in entries}) == 13
for report_id, values, name, _ in entries:
    expected = protocol["buttons"][name]
    actual = bytes(int(value.strip(), 16) for value in values.split(","))
    assert int(report_id) == expected["report_id"], name
    assert actual.hex() == expected["press"], name
compile((folder / "__init__.py").read_bytes(), str(folder / "__init__.py"), "exec")

class Loader(yaml.SafeLoader):
    pass

for tag in ("!secret", "!lambda"):
    Loader.add_constructor(tag, lambda loader, node: loader.construct_scalar(node))
config = yaml.load((folder.parents[1] / "jbl-4305p-bridge.yaml").read_text(encoding="utf-8"), Loader=Loader)
events = {event["id"]: event["event_types"] for event in config["event"]}
labels = {label for _, _, _, label in entries}
assert len(labels) == 13
assert set(events["jbl_remote_key_received"]) == labels | {"收到未识别报告"}
assert len(events["jbl_remote_key_received"]) == 14
assert set(events["jbl_ha_key_sent"]) == {
    label + suffix for label in labels for suffix in ("（已提交）", "（未发送）")
} | {"未知命令（未发送）"}
assert len(events["jbl_ha_key_sent"]) == 27

relay = (folder / "jbl_remote_relay.cpp").read_text(encoding="utf-8")
send = relay.split("bool JBLRemoteRelay::send_key(", 1)[1].split("void JBLRemoteRelay::reset_()", 1)[0]
assert "find_received_key(id, payload.data(), payload.size())" in send
assert "ha_send_block_reason(" in send and "record_ha_send_(label, reason)" in send
assert 'record_ha_send_(label, "蓝牙协议栈未接受按下报告")' in send
assert send.index("if (!this->emit_") < send.index('this->set_timeout(') < send.index("record_ha_send_(label, nullptr)")
assert "report->ha_active = false;" in send
assert send.count("record_ha_send_") == 3
assert 'this->received_event_->trigger(key->label)' in relay
notify = (folder.parent / "esp32_ble_server/ble_characteristic.cpp").read_text(encoding="utf-8")
notify = notify.split("bool BLECharacteristic::notify()", 1)[1].split("void BLECharacteristic::add_descriptor", 1)[0]
assert "return false;" in notify and "return accepted;" in notify
assert notify.index("if (err != ESP_OK)") < notify.index("break;") < notify.index("accepted = true;")
print("OK: 13 captured mappings, 14 Chinese receive types, 27 HA result types, send gates and stack-acceptance wiring")
print("C++ static_assert covers actual decoding, repeats/releases and all pre-send decisions during firmware build")
