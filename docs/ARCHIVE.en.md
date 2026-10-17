# Archive scope and confidence

[简体中文](ARCHIVE.md) | English

Archive date: 2026-10-17 (Asia/Shanghai). The main implementation is original remote → ESP32-C3 → JBL 4305P, with HA sending key commands to the same ESP32 over the encrypted ESPHome API.

## What is included

Source code, pinned build configuration, upstream component licenses and provenance, the 13-key protocol, sanitized validation summaries, the HA dashboard and connection diagram, human deployment instructions, AI execution rules, factory/OTA artifacts compiled with public example configuration, SHA-256 hashes, and a build manifest.

The Python prototype scripts document the research methods; they are not daily relay services. `docs/archive/` preserves implementation comparisons clearly marked as obsolete. Use the README and deployment guide for actual installation.

## Why live firmware and raw captures are not published

Firmware built for a working installation embeds its Wi-Fi settings, API key, and OTA password. Full-flash/NVS backups may also contain BLE bonding keys. Bluetooth/HCI captures may include data from other devices. These files remain with the project owner locally and are outside the public archive.

Original filenames and frame numbers in `jbl_protocol.json` are research references; they do not imply that the raw files are included in the repository. The public archive provides the original 79-byte HID descriptor and its hash, the complete key bytes, and GATT read results. Static checks can run independently. Replaying the original capture-validation checks requires the owner to provide the raw material privately.

## Keep the two versions distinct

- **Physically tested original-device version:** build 2026-10-17 19:52:45 +0800, with identities fixed to the original hardware set. Verified behavior is listed in `evidence/validation-summary.json`. The working firmware is retained locally and is not uploaded as a public artifact.
- **Configurable archive version:** both MAC addresses become deployment parameters, with the existing authentication and subscription gates preserved. The public version has source-review and build checks. It has not been flashed to the device currently in use, and has not been physically tested on a second set of the same model.

Public example `.bin` files contain fake network settings and public demonstration keys. They are for checking the build and file formats. We recommend having an AI fill in private configuration and compile from source for deployment. Do not use the example keys as real home credentials.

## Reproduction and rollback

Build with the pinned versions. The binaries contain timestamps and other build metadata, so this repository provides a repeatable build procedure and SHA-256 verification, without claiming byte-for-byte identical builds across machines.

If OTA fails, preserve the device's current state and investigate before erasing flash. For rollback, use the previous device-specific OTA image retained locally and matched to that device. For a new device, rebuild the corresponding source with its own parameters. The public factory image is not a substitute for a user's backup.

Archive each new version with its source commit, compiler/framework versions, configuration-template hash, artifact hashes, target hardware, actual tests, and untested items. Do not report “Bluetooth connected” as “the speaker executed the command.”
