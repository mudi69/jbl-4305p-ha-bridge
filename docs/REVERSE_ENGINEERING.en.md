# How the protocol was obtained

[简体中文](REVERSE_ENGINEERING.md) | English

Once both public MAC addresses are known and the protocol matches, normal deployment and daily control do not require Nordic or new captures. The research path below may be needed when an address is initially unknown or a new remote/speaker has a different protocol.

The speaker is the BLE central in the original remote connection. Do not infer its identity from its Wi-Fi MAC or ordinary advertising scans. Identify it by capturing connection establishment and verifying the initiator and address type, or by obtaining reliable endpoint records. Identity research and key-protocol decoding are separate steps.

1. Normal computer scans mainly observe advertising. An already connected remote may no longer send connectable advertisements; not appearing in a scan does not mean it is not transmitting. The nRF52840 is used for BLE, not as a universal sniffer for every Bluetooth transport.
2. Nordic over-the-air capture established advertising and encrypted connections. This investigation did not obtain enough pairing/encryption-start material to recover all keys directly from those captures. Encrypted radio traffic must not be treated as a replayable control protocol.
3. The speaker was switched off, the remote entered pairing mode, and Windows became an authorized endpoint. Full Windows HCI logging plus isolated physical key sequences yielded the report bytes. Windows Raw Input mappings were supporting evidence only; the initial timing-based guess that USB equaled Esc was withdrawn.
4. Nordic connectivity firmware served as an independent GATT client. After authorized pairing, it read `0x2908` Report References to confirm input IDs 1/3/30 and their characteristics. This path also captured the power key without having the computer interpret it as a system action.
5. The 79-byte HID Report Map and GATT service structure were reproduced on the ESP32. ID 3 declares four bytes in the descriptor, but observed input notifications contain two bytes. The implementation preserves the measured length and does not reinterpret the physical key meanings from standard HID names.
6. Connecting the original and emulated remotes directly to the speaker together did not produce a stable solution. The implementation therefore uses an ESP32 BLE client/server relay. Authentication, CCCD subscriptions, bond persistence, and connection recovery each require their own handling.

## File guide

- `jbl_protocol.json`: protocol facts, key evidence, and original frame/line references.
- `evidence/hid-report-map.bin`: the credential-free 79-byte descriptor.
- `scan_ble.py`: advertising decoded by the operating system, not raw radio packet capture.
- `capture_remote.py`: research script for the Nordic legacy Sniffer API. It requires the official SDK to be installed separately and an explicit port and target. It is not required for deployment.
- `inspect_remote.py`: connects to an explicitly specified remote endpoint and reads GATT. It changes the active connection arrangement and is intended only for controlled research.
- `inspect_hid.py` / `inspect_hid_caps.py`: Windows HID endpoint research scripts.
- `validate_jbl_protocol.py`: cross-validates against private local raw captures. The complete historical validation cannot run without those files.

Only research devices you own. Temporary full HCI logging can record other Bluetooth connections and keys; limit the capture window and restore the previous logging settings afterward. The temporary full logging used in this project's original research was stopped and the previous settings restored.
