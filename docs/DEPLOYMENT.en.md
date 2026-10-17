# Deployment checklist

[简体中文](DEPLOYMENT.md) | English

For the AI performing the deployment and the person handling wiring, pairing, and speaker observation. Read the root [AGENTS.md](../AGENTS.md) first. Run all commands below from the repository root. This guide does not authorize flashing unspecified devices or resetting their bonds.

## 1. Confirm the target

**We recommend having an AI perform the deployment directly by following AGENTS.md and this checklist.** The AI handles configuration, compilation, target verification, upload, and HA setup. A person prepares the hardware, supplies private parameters locally, and performs the physical pairing steps.

- [ ] Confirm that the specified target is an ESP32-C3 with 4 MB flash. The configured board is `esp32-c3-devkitm-1`.
- [ ] Identify **one** serial port using USB device information, physical unplug/replug correspondence, and board identity. Record the final target. Do not try flashing every enumerated port.
- [ ] For OTA, verify one exact IP/hostname, the existing ESPHome node identity, and device information. Do not attempt uploads across a subnet.
- [ ] Confirm the remote and speaker's individual **public BLE MAC addresses**. Do not reuse addresses from another installation or substitute the speaker's network MAC for its BLE MAC.
- [ ] Determine whether this is a first installation or an update preserving existing bonds. Save the current source, private parameters, toolchain version, and usable rollback firmware first.
- [ ] Arrange a controlled listening volume and a clear view of the input indicators before testing physical actions.

The ESP32-C3 with 4 MB flash is the tested configuration. Other ESP32 variants, capacities, or another set of the same speaker model require validation. Generic BLE support is not a substitute for testing this project.

### Identify both MAC addresses

The original remote is a BLE peripheral; the primary speaker is the **BLE central** that connects to it. Normal BLE scans mainly show advertising devices and do not establish the speaker's central-role identity. A matching name, Wi-Fi MAC, Ethernet MAC, or arithmetic guess based on a nearby address is not sufficient evidence.

If the speaker address is unknown, first investigate this specific set. For example, capture the original remote-to-speaker connection establishment and verify the initiating role and address type, or obtain reliable controller/endpoint connection records. Also confirm that the remote address belongs to the physical remote. If identity or address type is uncertain, finish source preparation and wait for the missing evidence before generating a deployment configuration. Do not bypass this step by accepting arbitrary peers.

A Nordic capture device may be needed for initial address identification or protocol research. **Once both public MAC addresses are confirmed, normal deployment, pairing, and daily control do not require Nordic.** After the initial USB upload, the development computer is also outside the daily relay path.

## 2. Create an isolated build environment

Use Python 3.13. The original development environment was Windows with Python 3.13.14. ESPHome 2026.9.1 package metadata requires Python `>=3.12,<3.15`. Create a new virtual environment after cloning; do not depend on the author's local `.venv-esphome`. The root `requirements.txt` is for capture tools; firmware build dependencies are in `requirements-build.txt`.

Windows PowerShell:

```powershell
py -3.13 -m venv .venv-build
.\.venv-build\Scripts\python.exe -m pip install -r requirements-build.txt
.\.venv-build\Scripts\python.exe -m esphome version
```

Linux:

```sh
python3.13 -m venv .venv-build
.venv-build/bin/python -m pip install -r requirements-build.txt
.venv-build/bin/python -m esphome version
```

The version must be ESPHome **2026.9.1**. ESP-IDF is pinned to **5.5.5** in the YAML. The first build downloads the toolchain and needs network access. Do not upgrade ESPHome or IDF to work around an installation problem. On Windows, use a short ASCII checkout path to avoid toolchain path-length issues; do not restore the author's absolute `build_path`.

## 3. Prepare private parameters

For a first installation, run `scripts/prepare_config.py`. Replace the two placeholders below with the confirmed public BLE MAC addresses. The script prompts for the SSID, reads the Wi-Fi password using hidden `getpass` input, generates a unique API key and OTA password, and writes them directly to the Git-ignored `firmware/esphome/secrets.yaml`. It does not print credentials and refuses to overwrite an existing file.

Windows PowerShell:

```powershell
.\.venv-build\Scripts\python.exe scripts/prepare_config.py --remote-mac "REMOTE_PUBLIC_MAC" --speaker-mac "SPEAKER_PUBLIC_MAC"
```

Linux:

```sh
.venv-build/bin/python scripts/prepare_config.py --remote-mac "REMOTE_PUBLIC_MAC" --speaker-mac "SPEAKER_PUBLIC_MAC"
```

The script validates address syntax, not whether the address type is actually public; confirm that from device evidence. For an existing installation, preserve its secrets file and credentials so HA and OTA access continue to work. Do not delete it merely to rerun the script, or overwrite it with `secrets.example.yaml`. The example file documents these six keys:

| Key | Value |
| --- | --- |
| `wifi_ssid` | Local Wi-Fi SSID |
| `wifi_password` | Wi-Fi password |
| `jbl4305p_api_key` | Base64 encoding of 32 random bytes, used for ESPHome API encryption |
| `jbl4305p_ota_password` | Device-specific OTA password |
| `jbl4305p_remote_mac` | This original remote's public BLE MAC |
| `jbl4305p_speaker_mac` | This primary speaker's public BLE MAC |

Both MAC addresses are required. The implementation does not automatically learn the speaker address. Do not publish the secrets file, private values in logs, or binaries compiled with real parameters. An AI may use these values locally, but should not repeat credentials in its final response or commit message. Generate unique credentials for a new installation; retain existing credentials for updates.

## 4. Check and compile

Complete the static and provenance checks required by AGENTS.md, then compile. These commands do not upload firmware:

Windows PowerShell:

```powershell
.\.venv-build\Scripts\python.exe firmware/esphome/check_jbl_config.py
.\.venv-build\Scripts\python.exe firmware/esphome/components/jbl_remote_relay/verify_received_keys.py
.\.venv-build\Scripts\python.exe -m esphome config firmware/esphome/jbl-4305p-bridge.yaml
.\.venv-build\Scripts\python.exe -m esphome compile firmware/esphome/jbl-4305p-bridge.yaml
```

Linux:

```sh
.venv-build/bin/python firmware/esphome/check_jbl_config.py
.venv-build/bin/python firmware/esphome/components/jbl_remote_relay/verify_received_keys.py
.venv-build/bin/python -m esphome config firmware/esphome/jbl-4305p-bridge.yaml
.venv-build/bin/python -m esphome compile firmware/esphome/jbl-4305p-bridge.yaml
```

- [ ] The configuration references all six local secrets and uses the correct ESPHome/IDF versions.
- [ ] Report Map, Report References, all 13 key payloads, and Chinese event lists pass their checks.
- [ ] C++ compilation succeeds. Save the build time, versions, board target, and output SHA-256 hashes.
- [ ] Keep this private build separate from the examples in `releases/`; verify the artifact's origin again before upload.

A valid configuration or successful build does not establish that the speaker was physically tested.

## 5. Upload only to the verified board

Use USB for a first installation. `COM_TARGET` and `/dev/serial/by-id/TARGET` are placeholders: replace them with the real port confirmed in step 1. These commands upload the firmware you just compiled, not a public example artifact.

Windows PowerShell:

```powershell
.\.venv-build\Scripts\python.exe -m esphome upload firmware/esphome/jbl-4305p-bridge.yaml --device COM_TARGET
```

Linux:

```sh
.venv-build/bin/python -m esphome upload firmware/esphome/jbl-4305p-bridge.yaml --device /dev/serial/by-id/TARGET
```

For OTA to an existing, verified node, replace the `--device` value with that node's exact IP or hostname and use the OTA credentials it currently accepts. Do not rename the node, repartition flash, erase the whole device, clear NVS, or automatically press maintenance pairing buttons just to complete an upload. Normal updates should preserve bonds; toolchain and partition migrations have not been validated by this project.

`releases/2026.10.17/firmware.factory.bin` and `firmware.ota.bin` contain a fake SSID, example credentials, and documentation MAC addresses. They are not backups of your installation. **Do not OTA them over a working device.** There is no provisioning portal to fill in all parameters afterward; real deployment requires private configuration and a rebuild.

## 6. Add Home Assistant and map its entities

1. Once the ESP32 is online, add it through HA's ESPHome integration using its private API encryption key.
2. Verify the 21 actual entities: 15 buttons (13 controls plus 2 maintenance actions), 2 connection sensors, 2 text sensors, and 2 event entities.
3. Create a mapping from the actual HA entity registry. Template IDs come from the original instance. Chinese transliteration, device prefixes, and suffixes may differ on a new instance. Firmware entity keys are also distinct from HA `entity_id` values.
4. Create a separate JBL dashboard and import `firmware/home-assistant/jbl-4305p-dashboard.yaml`. Replace every entity reference with its actual match. Do not take over or overwrite the user's existing dashboard.
5. Copy `firmware/home-assistant/jbl-connection.svg` to `/config/www/jbl-4305p/connection.svg` on HA. The template uses `/local/jbl-4305p/connection.svg`. Creating `www` for the first time requires an HA restart; schedule it before proceeding.
6. Check that there are two views: `control` for status, controls, and send/receive history, and `pairing` for pairing instructions. There is no separate `history` view. The dashboard URL prefix depends on the path chosen when it is created.

All cards are native HA cards; HACS and custom cards are unnecessary. The device page shows the registered entities; free-form instructions and the diagram belong on the dedicated dashboard. The diagram describes the structure, while the two connection entities show live status. The supplied firmware and dashboard labels are Chinese.

## 7. Initial and maintenance pairing

Skip this section when existing bonds work. For a new board or a link that needs repair, follow the corresponding procedure. Each maintenance button resets only its target side; do not repeatedly press both.

**For a first installation, turn off the primary speaker's rear POWER switch before pairing the original remote with the ESP32.** This prevents the original speaker from reconnecting to the remote first. Once the remote is connected, turn on the speaker and follow the speaker-pairing procedure. Remote maintenance pairing on an already deployed bridge was tested with the speaker left powered on; that maintenance result is not the first-install procedure.

### Pair the original remote with the ESP32

1. Place the remote near the ESP32 and keep the ESP32 powered. For a first installation, leave the primary speaker's rear POWER switch off. For maintenance on an already deployed bridge, the speaker may remain powered on.
2. Press “重新配对原装遥控器（维护）” (Re-pair original remote — maintenance).
3. A person holds **Streaming + Play/Pause/OK** on the remote for about **3 seconds**, then releases them.
4. Wait for “原装遥控器 → ESP32” (Original remote → ESP32) to connect and inspect receive events. After the speaker side is also paired, press Optical then USB and observe the actual input change.

This action only handles the remote bond and preserves the speaker bond. It does not require touching the speaker's knob. Do not pair the original remote directly back to the speaker for normal use. When the remote has been asleep, the first press may only wake and reconnect it; wait about 5 seconds before pressing again.

### Pair the speaker with the ESP32

1. Keep the ESP32 powered. Turn on the primary speaker's rear POWER switch and wait for startup, the white power light, and an input indicator.
2. In the pairing view, press “重新配对音箱（维护）” (Re-pair speaker — maintenance).
3. A person holds the **left front VOL/MUTE knob** for about **4 seconds**, then releases it.
4. Wait for “ESP32 → 音箱” (ESP32 → speaker) to connect. Select Optical then USB from HA and observe the input indicator.

This handles the speaker bond and subscriptions while preserving the remote bond. If it does not connect, turn the speaker's rear POWER off for about 10 seconds, turn it on, wait for startup, and retry. Keep the ESP32 powered throughout. A flashing light or tone alone is not a reliable success/failure criterion.

## 8. Acceptance and handoff

- [ ] Both links are connected. Remote readiness means authentication and all three input report subscriptions have completed.
- [ ] A person confirms physical Optical → USB switching from both HA and the original remote.
- [ ] Pressing, releasing, and pressing the same remote key produces two distinct receive events.
- [ ] Sending the same HA key twice produces two send events without adding remote receive events.
- [ ] HA shows Chinese event types and timestamps with seconds; both activity cards are visible, with no missing entities.
- [ ] Test volume changes and standby/wake as appropriate, and record which keys were not physically tested.
- [ ] After an explicitly arranged restart test, verify bond preservation, speaker recovery, and remote control after wake/reconnection.
- [ ] Deliver versions, build hashes, target identity, actual HA entity mapping, and test results without publishing credentials.

“已提交” (Submitted) only establishes local stack acceptance of the key-down report. It does not confirm execution or subsequent key-release delivery. There is no complete history guarantee for HA downtime, requests that never reach the ESP32, or queue overflow. If long-press filtering, congestion, or release-failure paths only received source/build checks, state that explicitly in the handoff.
