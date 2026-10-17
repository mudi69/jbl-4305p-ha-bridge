# JBL 4305P Bluetooth Remote Bridge

[简体中文](README.md) | English

One ESP32-C3 receives commands from both the original JBL remote and Home Assistant, then sends them to the JBL 4305P over Bluetooth. Keep the ESP32 powered for everyday use. The Nordic capture device and development computer are not part of the runtime path.

```text
Original remote ── BLE HID ──→ ESP32-C3 ── BLE HID ──→ JBL 4305P primary speaker
Home Assistant ── Wi-Fi / encrypted ESPHome API ──┘
```

**We recommend having an AI perform the deployment by following [AGENTS.md](AGENTS.md) and the [deployment checklist](docs/DEPLOYMENT.en.md).** A person prepares the hardware, confirms both Bluetooth addresses, performs physical pairing, and observes whether the speaker actually responds. You can give the AI this instruction:

> Read AGENTS.md, docs/DEPLOYMENT.en.md, and docs/COMPATIBILITY.en.md first. Identify the ESP32 I specify, confirm the speaker and remote public MAC addresses, and compile with my private parameters. Preserve existing bonds and the verified toolchain. Do not scan serial ports and flash every device, and do not OTA the example firmware onto my existing device. Match the actual HA entities, import the dashboard, complete the deployment checklist, and distinguish build checks from physical tests.

## Features

- 13 speaker controls: standby/wake, volume up/down, mute, previous, play/pause, next, and streaming, Bluetooth, USB, optical, balanced, and AUX inputs.
- The original remote and HA work together, with separate status indicators for the two Bluetooth links.
- Chinese original-remote receive events, latest key, independent HA send results, and native 24-hour activity cards. The firmware and dashboard labels remain Chinese; these documents provide English deployment guidance.
- The HA “状态与控制” (Status and controls) view includes send/receive history. “配对说明” (Pairing instructions) remains a separate view.
- Separate maintenance actions for the remote and speaker bonds. Normal operation does not require re-pairing.

“收到” (Received) means the ESP32 received a remote report. “已提交” (Submitted) means the Bluetooth stack accepted an HA key-down report. Neither confirms speaker execution. Connection status does not report the speaker's power, volume, or selected input.

## Supported scope

| Item | Evidence and limitations |
| --- | --- |
| Hardware | One JBL 4305P set, its original remote, and an ESP32-C3 with 4 MB flash were physically tested. This does not establish plug-and-play support for other boards or another set of the same model. |
| Toolchain | ESPHome **2026.9.1**, ESP-IDF **5.5.5**. The local components depend on these versions; do not upgrade automatically. |
| Keys | All 13 payloads have real capture evidence. Physical speaker tests covered optical, USB, volume changes, standby, and wake. The remaining actions were not individually tested. |
| Restart | ESP32 restart and recovery with preserved bonds were tested on the original hardware. The first remote press may only wake/reconnect it; wait about 5 seconds and press again. |
| Portable configuration | Both the remote and speaker public MAC addresses must be supplied. Neither address is universal, and the speaker address is not learned automatically. |
| Published version | Check the release manifest for the parameterized source and sanitized example build. Tests of the original hardware do not establish that the parameterized version was physically tested on a new set. |

See [compatibility and validation limits](docs/COMPATIBILITY.en.md) for details.

## What to prepare

1. A JBL 4305P primary speaker and its original remote.
2. An identified ESP32-C3 with 4 MB flash, a USB data cable, and a stable power supply.
3. Home Assistant, working Wi-Fi, and the public BLE MAC addresses of this specific speaker and remote.
4. Private Wi-Fi SSID/password, an ESPHome API encryption key, and an OTA password. Never commit them to Git. The first-install configuration script can generate the API key and OTA password directly into the ignored secrets file.

The speaker is the **central** on this BLE link. Do not infer its BLE identity from its Wi-Fi MAC or a normal device scan. If its address is unknown, obtain connection-establishment captures or reliable endpoint evidence and confirm that the address type is public. Nordic or similar research tools may be needed for initial address identification. Once both MAC addresses are known, normal deployment and control do not require Nordic. See the address-identification section in the deployment guide.

For a first installation, follow the [complete deployment guide](docs/DEPLOYMENT.en.md), compile from source, and upload only to the identified board. Updating a working device requires preserving its credentials, partition layout, and bonds.

## Documentation and source

| Entry | Purpose |
| --- | --- |
| [AGENTS.md](AGENTS.md) | AI execution rules, required checks, and release boundaries |
| [docs/DEPLOYMENT.en.md](docs/DEPLOYMENT.en.md) | Human/AI responsibilities, build commands, installation, pairing, HA setup, and acceptance checklist |
| [docs/ARCHITECTURE.en.md](docs/ARCHITECTURE.en.md) | Dual BLE roles, component responsibilities, event meanings, and bond recovery |
| [docs/COMPATIBILITY.en.md](docs/COMPATIBILITY.en.md) | Tested environment, protocol evidence, and unverified cases |
| [docs/ARCHIVE.en.md](docs/ARCHIVE.en.md) | Public archive scope, the two firmware versions, reproduction, and rollback limits |
| [docs/REVERSE_ENGINEERING.en.md](docs/REVERSE_ENGINEERING.en.md) | Protocol and identity research, evidence sources, and research-script purposes |
| [NOTICE](NOTICE.md) | License and third-party notices |
| [scripts/build_release.py](scripts/build_release.py) | Builds public example artifacts in isolation; never uploads to a device |
| [Release artifact notes](releases/2026.10.17/README.md) | This release's factory/OTA examples, checksums, and usage restrictions |
| [evidence/validation-summary.json](evidence/validation-summary.json) | Sanitized validation summary that an AI can read |
| [docs/JBL-4305P-使用说明.txt](docs/JBL-4305P-使用说明.txt) | Daily operation and original deployment test notes; Chinese |
| [firmware/esphome/jbl-4305p-bridge.yaml](firmware/esphome/jbl-4305p-bridge.yaml) | Firmware entry point; local components are in the adjacent `components/` directory |
| [firmware/home-assistant/jbl-4305p-dashboard.yaml](firmware/home-assistant/jbl-4305p-dashboard.yaml) | Native HA dashboard template; match all references to the actual entity IDs in your instance |
| [jbl_protocol.json](jbl_protocol.json) | Report descriptor, key bytes, and capture evidence index |

The public repository contains the protocol and sanitized validation summaries. Raw captures, working-device firmware backups, and private logs remain with the project owner and are not distributed.

## Release artifacts

`releases/2026.10.17/` contains `firmware.factory.bin`, `firmware.ota.bin`, `manifest.json`, and `SHA256SUMS`. The binaries are compiled with **public, sanitized example parameters**: a fake SSID, example API key, and documentation MAC addresses. They do not contain usable settings for your home or devices.

Use the example artifacts only for initial USB experiments or build reproduction. For real use, fill in your device-specific parameters and rebuild. The project does not provide a runtime provisioning portal for the example firmware. **Do not OTA an example binary over working firmware containing your credentials**, and do not publicly upload binaries built with real credentials. The `ota` filename describes the format, not suitability for updating an existing device.
