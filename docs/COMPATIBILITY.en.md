# Compatibility and validation limits

[简体中文](COMPATIBILITY.md) | English

This document helps deployers and AI agents distinguish captured protocol evidence, source/build checks, and successful physical speaker tests. Validation date: **2026-10-17**. Read [AGENTS.md](../AGENTS.md) and the [deployment checklist](DEPLOYMENT.en.md) before acting. A matching product model alone does not establish plug-and-play compatibility.

## Original tested environment

| Item | Original environment | Requirements for reuse |
| --- | --- | --- |
| Speaker | JBL 4305P primary speaker; reported firmware `0.101.1581.0x3bf911c` | Retest another unit or firmware version and supply its public BLE MAC. |
| Remote | Original `JBL 4305P REMOTE`; GATT device information `Realtek BT / HBG BLE RC` | Each remote has its own MAC. Supply its public MAC and pair it with the ESP32. |
| MCU | ESP32-C3, 4 MB flash; board configuration `esp32-c3-devkitm-1` | Only the original board was tested. Other variants and capacities are unverified. |
| Framework | ESPHome 2026.9.1, ESP-IDF 5.5.5 | Keep these versions. `jbl_entity_labels` explicitly checks the ESPHome version. |
| Build | Windows, Python 3.13.14; ESPHome supports Python `>=3.12,<3.15` | Create a fresh venv. Linux commands describe the standard build procedure, not a completed Linux end-to-end test. |
| HA | Home Assistant Core 2026.9.4, native ESPHome integration | Verify the version, actual entity IDs, and card rendering on a new instance. |

The latest feature tests on the original hardware used the **2026-10-17 19:52:45** build, when both addresses were fixed to that hardware. The archived source parameterizes both addresses and is compiled with public example parameters. Source/build validation does not establish that the new firmware was OTA-installed or tested on another hardware set. Check `releases/2026.10.17/manifest.json` for the exact release build status, toolchain, and hashes.

## Available evidence

| Item | Evidence level | Confirmed behavior |
| --- | --- | --- |
| 13 key payloads | Real, controlled captures | Standby/wake, volume up/down, mute, previous, play/pause, next, and six input selections. |
| HID Report Map/References | Windows HID/HCI and Nordic GATT cross-checks | A 79-byte descriptor; input IDs 1, 3, and 30; actual ID 3 notifications contain 2 bytes. |
| Speaker control | Physical tests on the original hardware | Optical, USB, volume changes, standby, and wake from the original remote. |
| HA/remote coexistence | Physical tests on the original hardware | Both BLE links work together, and both command sources can control the speaker. The original remote is paired to the ESP32. |
| ESP32 restart recovery | Physical tests on the original hardware | Speaker reconnection, authentication, and three subscription restorations; the remote works after wake and another press. |
| Remote maintenance pairing | Logs, live state, and human confirmation | Only the remote bond was removed; the speaker remained connected. Optical → USB worked after re-pairing. |
| Repeated remote key | HA history and human confirmation | Optical, USB, USB were recorded at distinct times. Identical latest-key text does not suppress new events. |
| Chinese send/receive history | Encrypted API, HA UI, and human confirmation | Chinese receive events; two HA USB submissions recorded separately without contaminating receive history. |
| Dashboard | Native HA UI verification | Controls and history are combined, pairing is separate, and no entities are missing. |

In one USB-input volume test, a remote volume-down press changed a read-only network value from 29 to 26, and HA volume-up restored it to 29. This describes that observation only; it does not establish a fixed three-step change at every volume setting.

See [evidence/validation-summary.json](../evidence/validation-summary.json) for the public summary an AI can read, [JBL-4305P-使用说明.txt](JBL-4305P-使用说明.txt) for detailed human test notes in Chinese, and [jbl_protocol.json](../jbl_protocol.json) for the protocol index. Raw captures, private logs, and working-device firmware remain with the owner and are not published. Older records have a time boundary: an early “not tested” statement or a screenshot of the previous three-view dashboard describes that earlier stage and must be read alongside later dated evidence.

## Unverified or unsupported guarantees

- Mute, other input selections, and media controls have captured payloads but were not all individually confirmed on the speaker.
- Another set of the same model, other remote hardware revisions, random BLE addresses, other JBL products, other ESP32 boards, or different flash capacities.
- Complete pairing, physical control, and restart recovery of the parameterized release firmware on a new set. Archiving the project does not automatically flash the original live device.
- Whole-system power cycling, long-term stability, extreme range/interference, and concurrency stress.
- Large vendor ID 30 reports, two-link MTU behavior, general output-report forwarding, or multiple-speaker/remote routing.
- Full hardware fault injection for rejected sends, held-key filtering, queue congestion, and rejected release reports. Source/build checks do not establish those physical tests.
- Bluetooth readback of speaker power, selected input, volume, or mute state. A separate read-only network volume observation does not establish network write control.
- Offline event replay, a complete send audit, permanent history retention, or direct operation of public example binaries on a real home network.

## Required checks for another set of the same model

1. Confirm **both public BLE MAC addresses**, for the original remote and primary speaker, and put them in private configuration. The speaker is a BLE central: do not guess its identity from its Wi-Fi MAC or a normal scan. If unknown, confirm the identity and public address type from connection-establishment captures or reliable endpoint records. Changing Wi-Fi alone is insufficient. The speaker address is not learned automatically.
2. Confirm the actual ESP32 model, flash capacity, uniquely identified upload target, and ESPHome 2026.9.1/IDF 5.5.5 versions.
3. Check whether the new remote's HID Report References and actual report lengths match the captured protocol. Report Reference discovery does not imply compatibility with arbitrary HID devices.
4. Pair both endpoints with the ESP32 and physically verify Optical → USB, volume, and standby/wake.
5. Map the actual 21 HA entities: 15 buttons, 2 connection sensors, 2 text sensors, and 2 event entities. Repository button labels, firmware entity keys, and existing HA entity IDs are distinct and must not be substituted for one another.
6. Verify the release manifest and SHA256SUMS. Rebuild with private parameters for real deployment. Example factory/OTA binaries do not contain reusable home-network or device-identity configuration.

Nordic or similar tools may be needed for initial address identification, but are unnecessary for normal deployment and daily control once both MAC addresses are known. Knowing the addresses does not establish that the new set's protocol and physical behavior have been validated.

## Upgrade rules

Do not upgrade ESPHome or ESP-IDF automatically, or use an upgrade as a reason to flash live devices without arranging that work. Recheck the local BLE components and code-generation interfaces, run protocol/provenance checks, compile, and repeat pairing and recovery tests on a clearly identified target. Normal updates should preserve existing bonds. Clearing NVS, repartitioning flash, and deleting all bonds are not routine deployment steps.
