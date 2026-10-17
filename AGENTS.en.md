# AI deployment contract

Release checks cover the index and all local branches, tags, and remote-tracking branch history. Desktop private recovery refs such as `refs/codex/` stay local and are not release content. Push only the explicit intended branch/tag, never `push --mirror`. Archives require a separate expanded-content review. Without a private-credential baseline, do not claim actual private-value scanning was completed.

[简体中文](AGENTS.md) | English

This repository recommends having an AI carry out preparation, compilation, deployment, and verification directly. The human is responsible for confirming device ownership, supplying power, performing physical pairing actions, and observing the speaker's indicators. Read README.md, docs/DEPLOYMENT.md, and docs/COMPATIBILITY.md before proceeding.

## Authoritative files

- `firmware/esphome/jbl-4305p-bridge.yaml`: deployment configuration; components are pinned to ESPHome 2026.9.1 and ESP-IDF 5.5.5.
- `firmware/esphome/components/`: the four local components that must be deployed together.
- `jbl_protocol.json`: 13 captured buttons, the HID Report Map, and the GATT structure; raw captures remain local to the project owner.
- `evidence/validation-summary.json`: the scope of hardware validation; do not treat protocol inference as validation on a second hardware set.
- `firmware/home-assistant/`: the combined control and receive/send history page, separate pairing instructions, and connection diagram.
- `releases/2026.10.17/manifest.json`: exact build information and limitations for the public example artifacts.

## Required deployment inputs

Confirm the target ESP32 model, flash size, and unique USB identity or verified OTA IP address. Confirm the primary speaker's BLE **public** MAC and the original remote's BLE **public** MAC. Obtain the Wi-Fi and HA access information for this deployment. The speaker's Wi-Fi MAC or IP address cannot substitute for its BLE MAC. Do not guess the target from a historical COM number or the first serial port.

When inputs are missing, first complete source checks and documentation that do not depend on them, then ask only for the missing items. Once explicit authorization and sufficient device evidence are available, carry out deployment without repeatedly requesting the same authorization. Flashing other devices, erasing flash, clearing pairings, or expanding network access is outside the default scope.

## Execution checklist

1. Read the current state and back up the existing configuration and current firmware. Preserve the user's uncommitted changes. Do not replace another ESPHome device's configuration with this project.
2. Use an isolated Python environment and `requirements-build.txt`. On Windows, an ASCII path for the working copy/build directory is recommended to avoid toolchain path issues.
3. Create a Git-ignored `secrets.yaml` using `secrets.example.yaml`; generate a unique API key and OTA password for each device. Do not print credentials, upload them to Git, or copy the original developer's bonding information.
4. Set both MAC addresses. Both sides must continue to enforce strict identity filtering. Do not resolve connection failures by disabling authentication or accepting arbitrary peers.
5. Run the configuration mapping check, component provenance checks, event mapping check, entity-name code generation check, and a full build. YAML validation alone does not establish that the firmware is usable.
6. Flash only the verified ESP32. Use USB for the first installation; for an existing working device, prefer OTA with matching credentials. Do not treat `.factory.bin` as an image to write at the application offset, and do not treat public example artifacts as usable firmware for a real home installation.
7. Pair the original remote and the speaker in the order specified by the deployment guide. Each maintenance button must affect only its corresponding side. The first reconnection may require one press to wake the remote, a wait of about 5 seconds, and then a press of the intended control button.
8. Adapt the dashboard YAML after discovering the actual entity IDs in HA. The example entity IDs are not guaranteed across HA instances. Back up the dashboard, then save through a supported HA interface or API; do not directly edit a running instance's `.storage` files.
9. Verify optical-to-USB switching, volume, standby/wake, and recovery after an ESP32 restart using both the original remote and HA. Logs cannot replace the user's physical button presses and observations.
10. Verify that Chinese receive and send events are recorded separately and that repeated presses of the same key produce separate events. `已提交` (“submitted”) means only that the local Bluetooth stack accepted the press report; it is not an acknowledgment that the speaker executed the command.
11. Stop temporary listeners while leaving HA and the ESP32 operating normally. Deliver the device addresses, versions, checksums, verification results, unverified items, and rollback steps. Update the archive only after removing private information.

## Release boundaries

- State the validation scope of the hardware-tested production version separately from that of the newly parameterized MAC version. Do not automatically flash the production device merely to complete an upload or release.
- `captures/`, `tools/`, `.local/`, `secrets.yaml`, production full-flash/NVS backups, logs, Wi-Fi/API/OTA credentials, and private `.bin` files must not enter a public release.
- Build public artifacts afresh only from an explicitly public example configuration. Before committing, inspect the staging area, binaries, archives, and all Git history. Scan for both textual credentials and decoded API keys, reporting locations rather than credential values.
- Preserve upstream licenses, source commits, and modified-file hashes. Update the corresponding provenance records after changing components.
- The design scope is one speaker/remote set. Do not claim support for new models, different GATT layouts, random addresses, multiple sets operating in parallel, preservation of the first wake-up keypress, or long-term stability without validation.
