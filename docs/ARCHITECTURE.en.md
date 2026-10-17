# Architecture and maintenance guide

[简体中文](ARCHITECTURE.md) | English

This document gives people and AI agents a shared map of the source. Follow [DEPLOYMENT.en.md](DEPLOYMENT.en.md) for deployment order and [AGENTS.md](../AGENTS.md) for execution rules. [jbl_protocol.json](../jbl_protocol.json) is the source of captured key bytes; [COMPATIBILITY.en.md](COMPATIBILITY.en.md) defines the scope of tested behavior.

## Data flow and roles

```text
Original remote (BLE peripheral / GATT server)
  | Encrypted connection and HID input notifications
  v
ESP32-C3: BLE client reads the original remote
  | jbl_remote_relay forwards raw reports
  v
ESP32-C3: BLE peripheral / HID GATT server
  | Input notifications
  v
Primary speaker (BLE central / GATT client)

HA native button -> encrypted ESPHome API -> send_key -> same HID server -> speaker
Remote reception / HA send outcome -> separate event + text_sensor -> HA activity cards
```

One ESP32 serves both BLE roles. The configuration allows two connections, with one speaker on the server side. Private parameters specify each target's public MAC. The speaker address must also be configured explicitly; it is not learned automatically from scanning. This is not a generic HID router or a gateway for multiple speakers.

Because the speaker is the central, ordinary advertising scans do not necessarily expose its identity on this connection. Do not derive its BLE MAC from its Wi-Fi MAC. An unknown address requires original connection-establishment captures or reliable endpoint records, with the public address type verified. Nordic can help with this initial identity research; normal deployment and daily operation do not depend on it once the addresses are known.

## File responsibilities

| File or component | Responsibility |
| --- | --- |
| `firmware/esphome/jbl-4305p-bridge.yaml` | Firmware entry point, HID services, 13 controls, 2 maintenance buttons, sensors, and event declarations |
| `components/esp32_ble` | BLE configuration/advertising and directed reconnection support based on the pinned ESPHome version |
| `components/esp32_ble_server` | HID GATT service, speaker identity and authentication checks, CCCD persistence/restoration, and speaker maintenance pairing |
| `components/jbl_remote_relay` | Remote authentication and report discovery, three-channel subscription, forwarding, synthetic HA keys, and separate receive/send observation |
| `components/jbl_remote_relay/received_keys.h` | Exact mappings and Chinese labels for 13 keys, repeat/release handling, and pre-send checks |
| `components/jbl_entity_labels` | Chinese display names while preserving the original firmware API entity keys; uses private ESPHome 2026.9.1 code-generation interfaces |
| `firmware/home-assistant/` | Native dashboard and static SVG connection diagram |

All `components/` paths above are relative to `firmware/esphome/`. Each component's `provenance.json` records its origin, local changes, and file hashes. Follow AGENTS.md when checking and updating these records after a change. Upgrading the underlying library alone does not establish that the local modifications remain compatible.

## HID reports and keys

The main service is HID `0x1812`. Report characteristics share UUID `0x2A4D`, so the UUID alone cannot distinguish them. The relay reads the actual `0x2908` Report References to discover input IDs **1, 3, and 30** and their CCCDs, then completes subscriptions sequentially. ATT handles in the captures are evidence for that session, not fixed addresses for another device.

- ID 1: 8 bytes, carrying most keys.
- ID 3: captured key notifications contain **2 bytes**. Although the original Report Map declares 4 bytes, reproduction must retain the observed 2-byte payloads.
- ID 30: preserves the vendor input channel. Large reports and two-link MTU behavior have not been physically tested.

The 13 key labels come from controlled physical key sequences matched to captures, not guesses based on standard HID usage names. The service also preserves ID 1/30 output Report References, but does not provide generic output-report forwarding.

## Original remote and HA coexistence

The remote becomes ready only after authentication and all three input subscriptions complete. Forwarding preserves raw press, repeat, and release reports. The observer emits a receive event only for a new observable press, so identical held-key reports do not flood the history. Releasing and pressing the same key again still produces a new event.

HA keys pass through the `jbl_send_key` queue, configured for at most 10 runs. After submitting a press, the relay schedules a synthetic release after about **150 ms**; the script uses a **160 ms** interval. When the physical remote occupies the corresponding report channel, conflicting HA requests are rejected. A new physical report can cancel a pending synthetic HA release so that it does not incorrectly release a key the person is still holding. Offline key commands are not queued for replay after reconnection.

## Event meanings

| Entity | Meaning | What it does not establish |
| --- | --- | --- |
| Original remote connection | The target remote is authenticated and all three input subscriptions are complete | Speaker receipt or execution |
| Speaker Bluetooth connection | A speaker connection exists on the ESP32 server | Speaker power, selected input, or volume |
| Original remote key received event | The ESP32 observed a remote key; the `event_type` is Chinese | Speaker execution acknowledgement |
| HA key sent event: 已提交 (Submitted) | The local BLE stack accepted the key-down report | Radio delivery, speaker execution, or successful key release |
| HA key sent event: 未发送 (Not sent) | A request that reached send processing was rejected | Complete accounting for network loss or queue overflow |

Remote reception and HA sends have separate `event` and latest-text entities. HA sends do not create remote receive events. A release failure is logged without creating another key-down event. Firmware startup resets the latest text to “本次启动尚未收到／发送” (Nothing received/sent since this boot) without emitting an event.

The state of a native HA event entity is its reception timestamp; the event type is in `event_type`. Repeated presses of the same key get new timestamps. Automations should use events rather than only comparing the latest text. History depends on actual HA connectivity and Recorder settings, with no offline replay. Existing English history is not rewritten when new events use Chinese values.

## Bonds and recovery

BLE bonds are stored on the device. The server additionally persists the target speaker's subscription state and restores it after target authentication. Normal firmware updates should preserve the existing partition layout, credentials, and bonds. The original hardware was tested for speaker reconnection and subscription recovery after an ESP32 restart. The remote's first press may only trigger wake/reconnection.

The maintenance actions handle targets separately: speaker maintenance handles the speaker bond and its subscriptions; remote maintenance handles the remote bond. These are not routine reconnect buttons, and must not be changed into operations that delete every bond or erase NVS. After changing entities, protocols, or target identities, recheck whether the persisted data still matches the new configuration.

## HA dashboard

“状态与控制” (Status and controls) contains the static diagram, live connection status, latest receive/send values, 13 controls, and two 24-hour activity cards. “配对说明” (Pairing instructions) keeps the two maintenance operations separate. All cards are native. Timestamps use `time_format: {type: time, style: long}` to display seconds.

Stable firmware API keys and Chinese display names do not guarantee that a new HA instance will assign the same `entity_id` values. Read the actual entity registry and map the references before importing the dashboard; do not blindly reuse the original deployment's prefix.
