#pragma once

#include "esphome/components/ble_client/ble_client.h"
#include "esphome/components/esp32_ble_server/ble_characteristic.h"
#include "esphome/components/event/event.h"
#include "esphome/components/text_sensor/text_sensor.h"
#include "esphome/core/component.h"
#include <array>
#include <vector>

namespace esphome::jbl_remote_relay {

class JBLRemoteRelay : public Component, public ble_client::BLEClientNode {
 public:
  void setup() override;
  void loop() override;
  void dump_config() override;
  void set_output(uint8_t id, esp32_ble_server::BLECharacteristic *output);
  bool is_remote_ready() const { return this->ready_; }
  bool is_speaker_ready() const;
  bool send_key(uint8_t report_id, const std::vector<uint8_t> &payload);
  void start_remote_pairing();
  void set_received_event(event::Event *received_event) { this->received_event_ = received_event; }
  void set_last_key_text_sensor(text_sensor::TextSensor *sensor) { this->last_key_text_sensor_ = sensor; }
  void set_sent_event(event::Event *sent_event) { this->sent_event_ = sent_event; }
  void set_last_send_text_sensor(text_sensor::TextSensor *sensor) { this->last_send_text_sensor_ = sensor; }
  void gattc_event_handler(esp_gattc_cb_event_t event, esp_gatt_if_t gattc_if,
                           esp_ble_gattc_cb_param_t *param) override;
  void gap_event_handler(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t *param) override;

 protected:
  struct Report {
    uint8_t id;
    esp32_ble_server::BLECharacteristic *output{nullptr};
    uint16_t handle{0};
    uint16_t cccd{0};
    bool physical_down{false};
    bool ha_active{false};
    std::array<uint8_t, 8> last_received{};
  };
  struct Reference {
    uint16_t handle;
    uint16_t ref;
    uint16_t cccd;
    bool notify;
  };
  Report *report_(uint8_t id);
  void begin_discovery_();
  void read_next_reference_();
  void subscribe_next_();
  void reset_();
  void fail_(const char *reason);
  bool emit_(Report &report, const uint8_t *data, size_t size);
  bool record_ha_send_(const char *label, const char *failure_reason);
  void release_keys_();
  void continue_remote_pairing_();
  void finish_remote_pairing_(bool success);
  std::array<Report, 3> reports_{{{1}, {3}, {30}}};
  std::vector<Reference> references_;
  size_t reference_index_{0};
  size_t subscription_index_{0};
  bool authenticated_{false};
  uint64_t remote_address_{0};
  bool security_started_{false};
  bool discovered_{false};
  bool setup_started_{false};
  bool ready_{false};
  bool speaker_was_ready_{false};
  bool remote_pairing_pending_{false};
  bool remote_bond_remove_pending_{false};
  bool remote_link_closed_{false};
  bool remote_client_was_enabled_{true};
  event::Event *received_event_{nullptr};
  text_sensor::TextSensor *last_key_text_sensor_{nullptr};
  event::Event *sent_event_{nullptr};
  text_sensor::TextSensor *last_send_text_sensor_{nullptr};
};

}  // namespace esphome::jbl_remote_relay
