#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace esphome::jbl_remote_relay {

struct ReceivedKey {
  uint8_t report_id;
  std::array<uint8_t, 8> payload;
  const char *protocol_key;
  const char *label;
};

// Exact captured bytes from jbl_protocol.json; ID3 contains two bytes, not four.
inline constexpr ReceivedKey RECEIVED_KEYS[] = {
    {1, {0x00, 0x00, 0x66, 0x00, 0x00, 0x00, 0x00, 0x00}, "power", "待机／唤醒"},
    {1, {0x00, 0x00, 0x52, 0x00, 0x00, 0x00, 0x00, 0x00}, "volume_up", "音量增加"},
    {1, {0x00, 0x00, 0x51, 0x00, 0x00, 0x00, 0x00, 0x00}, "volume_down", "音量降低"},
    {1, {0x00, 0x00, 0x82, 0x00, 0x00, 0x00, 0x00, 0x00}, "mute", "静音／取消静音"},
    {1, {0x00, 0x00, 0x50, 0x00, 0x00, 0x00, 0x00, 0x00}, "previous", "上一首"},
    {1, {0x00, 0x00, 0x58, 0x00, 0x00, 0x00, 0x00, 0x00}, "play_pause", "播放／暂停"},
    {1, {0x00, 0x00, 0x4F, 0x00, 0x00, 0x00, 0x00, 0x00}, "next", "下一首"},
    {3, {0xEA, 0x00}, "streaming", "网络播放"},
    {1, {0x00, 0x00, 0x75, 0x00, 0x00, 0x00, 0x00, 0x00}, "bluetooth", "蓝牙输入"},
    {3, {0xE9, 0x00}, "usb", "USB 输入"},
    {1, {0x00, 0x00, 0x29, 0x00, 0x00, 0x00, 0x00, 0x00}, "optical", "光纤输入"},
    {3, {0x23, 0x02}, "balanced", "平衡输入"},
    {1, {0x00, 0x00, 0x76, 0x00, 0x00, 0x00, 0x00, 0x00}, "aux", "AUX 辅助输入"},
};
inline constexpr ReceivedKey UNKNOWN_RECEIVED_KEY{0, {}, "unknown", "收到未识别报告"};

constexpr bool same_received_payload(const std::array<uint8_t, 8> &a, const std::array<uint8_t, 8> &b) {
  for (size_t i = 0; i < a.size(); i++)
    if (a[i] != b[i])
      return false;
  return true;
}

constexpr const ReceivedKey *find_received_key(uint8_t report_id, const uint8_t *data, size_t size) {
  if (!((report_id == 1 && size == 8) || (report_id == 3 && size == 2)))
    return nullptr;
  for (const auto &key : RECEIVED_KEYS) {
    if (key.report_id != report_id)
      continue;
    bool matches = true;
    for (size_t i = 0; i < size; i++)
      matches = matches && key.payload[i] == data[i];
    if (matches)
      return &key;
  }
  return nullptr;
}

constexpr const char *ha_send_block_reason(bool valid, bool physical_busy, bool ha_busy, bool speaker_ready) {
  if (!valid)
    return "命令格式或编码无效";
  if (physical_busy)
    return "原遥控器正在按住同类按键";
  if (ha_busy)
    return "上一条 HA 按键尚未释放";
  if (!speaker_ready)
    return "音箱连接或通知订阅未就绪";
  return nullptr;
}

// Per-report cache; nullptr suppresses observation only, never raw forwarding.
constexpr const ReceivedKey *observe_remote_key(uint8_t report_id, const uint8_t *data, size_t size,
                                                std::array<uint8_t, 8> &previous) {
  if (!((report_id == 1 && size == 8) || (report_id == 3 && size == 2)))
    return nullptr;
  std::array<uint8_t, 8> current{};
  bool pressed = false;
  for (size_t i = 0; i < size; i++) {
    current[i] = data[i];
    pressed = pressed || data[i] != 0;
  }
  const bool repeated = same_received_payload(current, previous);
  previous = current;  // A zero release clears the cache without replacing the last displayed key.
  if (!pressed || repeated)
    return nullptr;
  if (const auto *key = find_received_key(report_id, data, size))
    return key;
  return &UNKNOWN_RECEIVED_KEY;
}

constexpr bool received_keys_self_test() {
  const uint8_t zero[8]{};
  for (const auto &key : RECEIVED_KEYS) {
    std::array<uint8_t, 8> previous{};
    const size_t size = key.report_id == 1 ? 8 : 2;
    if (find_received_key(key.report_id, key.payload.data(), size) != &key ||
        observe_remote_key(key.report_id, key.payload.data(), size, previous) != &key ||
        observe_remote_key(key.report_id, key.payload.data(), size, previous) != nullptr ||
        observe_remote_key(key.report_id, zero, size, previous) != nullptr ||
        observe_remote_key(key.report_id, key.payload.data(), size, previous) != &key)
      return false;
  }
  std::array<uint8_t, 8> previous{};
  const uint8_t unknown[8]{0x01, 0x00, 0x66, 0x00, 0x00, 0x00, 0x00, 0x00};
  return find_received_key(1, unknown, 8) == nullptr && find_received_key(1, zero, 8) == nullptr &&
         find_received_key(3, unknown, 8) == nullptr &&
         observe_remote_key(1, unknown, 8, previous) == &UNKNOWN_RECEIVED_KEY &&
         observe_remote_key(1, unknown, 8, previous) == nullptr &&
         observe_remote_key(30, unknown, 8, previous) == nullptr &&
         observe_remote_key(1, unknown, 2, previous) == nullptr;
}
static_assert(received_keys_self_test(), "Remote key decoding/repeat/release checks failed");
static_assert(std::string_view(ha_send_block_reason(false, true, true, false)) == "命令格式或编码无效");
static_assert(std::string_view(ha_send_block_reason(true, true, true, false)) == "原遥控器正在按住同类按键");
static_assert(std::string_view(ha_send_block_reason(true, false, true, false)) == "上一条 HA 按键尚未释放");
static_assert(std::string_view(ha_send_block_reason(true, false, false, false)) == "音箱连接或通知订阅未就绪");
static_assert(ha_send_block_reason(true, false, false, true) == nullptr);

}  // namespace esphome::jbl_remote_relay
