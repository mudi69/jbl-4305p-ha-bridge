#include "jbl_remote_relay.h"
#include "received_keys.h"
#include "esphome/core/log.h"
#include <algorithm>
#include <cstring>

namespace esphome::jbl_remote_relay {

static const char *const TAG = "jbl_remote_relay";

void JBLRemoteRelay::setup() {
  this->remote_address_ = this->parent()->get_address();
  if (this->remote_address_ == 0 || this->remote_address_ == 0xFFFFFFFFFFFFULL) {
    ESP_LOGE(TAG, "BLE client must target an explicitly configured nonzero JBL remote address");
    this->mark_failed();
  }
  if (this->last_key_text_sensor_ != nullptr)
    this->last_key_text_sensor_->publish_state("本次启动尚未收到");
  if (this->last_send_text_sensor_ != nullptr)
    this->last_send_text_sensor_->publish_state("本次启动尚未发送");
}

void JBLRemoteRelay::dump_config() { ESP_LOGCONFIG(TAG, "JBL original remote relay (Input reports 1, 3, 30)"); }

JBLRemoteRelay::Report *JBLRemoteRelay::report_(uint8_t id) {
  for (auto &report : this->reports_)
    if (report.id == id)
      return &report;
  return nullptr;
}

void JBLRemoteRelay::set_output(uint8_t id, esp32_ble_server::BLECharacteristic *output) {
  if (auto *report = this->report_(id))
    report->output = output;
}

bool JBLRemoteRelay::is_speaker_ready() const {
  for (const auto &report : this->reports_)
    if (report.output == nullptr || !report.output->can_notify())
      return false;
  return true;
}

bool JBLRemoteRelay::emit_(Report &report, const uint8_t *data, size_t size) {
  if (report.output == nullptr || !report.output->can_notify())
    return false;  // No stale key queue across speaker reconnects.
  report.output->set_value(std::vector<uint8_t>(data, data + size));
  return report.output->notify();
}

void JBLRemoteRelay::release_keys_() {
  const uint8_t zero[8]{};
  for (auto &report : this->reports_) {
    if (report.id == 30)
      continue;
    this->cancel_timeout(static_cast<uint32_t>(report.id));
    if (report.physical_down || report.ha_active)
      this->emit_(report, zero, report.id == 1 ? 8 : 2);
    report.physical_down = false;
    report.ha_active = false;
  }
}

void JBLRemoteRelay::loop() {
  this->continue_remote_pairing_();
  const bool speaker_ready = this->is_speaker_ready();
  if (speaker_ready && !this->speaker_was_ready_) {
    const uint8_t zero[8]{};
    for (auto &report : this->reports_)
      if (report.id != 30 && !report.physical_down && !report.ha_active)
        this->emit_(report, zero, report.id == 1 ? 8 : 2);
  }
  this->speaker_was_ready_ = speaker_ready;
}

void JBLRemoteRelay::start_remote_pairing() {
  if (this->is_failed() || this->parent()->get_address() != this->remote_address_ ||
      this->parent()->get_gattc_if() == ESP_GATT_IF_NONE) {
    ESP_LOGW(TAG, "Remote pairing unavailable: BLE client not ready");
    return;
  }
  if (this->remote_pairing_pending_) {
    ESP_LOGW(TAG, "Remote pairing reset already pending");
    return;
  }
  this->remote_pairing_pending_ = true;
  this->remote_bond_remove_pending_ = false;
  this->remote_link_closed_ = this->parent()->state() == esp32_ble_tracker::ClientState::IDLE &&
                              this->parent()->get_conn_id() == esp32_ble_client::UNSET_CONN_ID;
  this->remote_client_was_enabled_ = this->parent()->enabled;
  this->reset_();
  this->parent()->set_enabled(false);  // Native client disconnects and stops accepting discoveries.
  if (!this->remote_client_was_enabled_)
    this->parent()->disconnect();
  ESP_LOGI(TAG, "Preparing original remote pairing; speaker connection unchanged");
  this->set_timeout("remote_pairing", 30000, [this]() {
    // An asynchronous remove may still complete. Do not reconnect and risk deleting a fresh bond.
    ESP_LOGW(TAG, "Remote pairing reset timed out; remote client stays paused until completion. Restart ESP32 if stuck");
  });
  this->continue_remote_pairing_();
}

void JBLRemoteRelay::continue_remote_pairing_() {
  if (!this->remote_pairing_pending_ || this->remote_bond_remove_pending_ || !this->remote_link_closed_ ||
      this->parent()->state() != esp32_ble_tracker::ClientState::IDLE ||
      this->parent()->get_conn_id() != esp32_ble_client::UNSET_CONN_ID)
    return;
  // Native may force IDLE on timeout; require real close evidence before removing the old bond.
  int count = esp_ble_get_bond_device_num();
  if (count < 0) {
    ESP_LOGW(TAG, "Could not query original remote bond");
    this->finish_remote_pairing_(false);
    return;
  }
  std::vector<esp_ble_bond_dev_t> bonds(count);
  if (count > 0 && esp_ble_get_bond_device_list(&count, bonds.data()) != ESP_OK) {
    ESP_LOGW(TAG, "Could not read original remote bond");
    this->finish_remote_pairing_(false);
    return;
  }
  for (auto &bond : bonds) {
    if (!this->parent()->check_addr(bond.bd_addr))
      continue;
    this->remote_bond_remove_pending_ = true;
    const esp_err_t err = esp_ble_remove_bond_device(bond.bd_addr);
    if (err != ESP_OK) {
      ESP_LOGW(TAG, "Could not request original remote bond removal: %s", esp_err_to_name(err));
      this->finish_remote_pairing_(false);
    } else {
      ESP_LOGI(TAG, "Removing only the original JBL remote bond");
    }
    return;
  }
  ESP_LOGI(TAG, "Original remote has no stored bond");
  this->finish_remote_pairing_(true);
}

void JBLRemoteRelay::finish_remote_pairing_(bool success) {
  this->cancel_timeout("remote_pairing");
  this->remote_pairing_pending_ = false;
  this->remote_bond_remove_pending_ = false;
  if (success)
    this->parent()->set_unpaired();
  this->parent()->set_enabled(this->remote_client_was_enabled_);
  if (success) {
    ESP_LOGI(TAG, "Original remote pairing reset complete; put the remote into pairing mode");
  } else {
    ESP_LOGW(TAG, "Original remote pairing reset failed; previous client enable state restored");
  }
}

bool JBLRemoteRelay::record_ha_send_(const char *label, const char *failure_reason) {
  const std::string event_type = std::string(label) + (failure_reason == nullptr ? "（已提交）" : "（未发送）");
  if (this->last_send_text_sensor_ != nullptr)
    this->last_send_text_sensor_->publish_state(event_type + (failure_reason == nullptr ? "" : std::string("：") + failure_reason));
  if (this->sent_event_ != nullptr)
    this->sent_event_->trigger(event_type);
  return failure_reason == nullptr;
}

bool JBLRemoteRelay::send_key(uint8_t id, const std::vector<uint8_t> &payload) {
  auto *report = this->report_(id);
  const auto *key = find_received_key(id, payload.data(), payload.size());
  const char *label = key == nullptr ? "未知命令" : key->label;
  const char *reason = ha_send_block_reason(key != nullptr && report != nullptr,
                                          report != nullptr && report->physical_down,
                                          report != nullptr && report->ha_active, this->is_speaker_ready());
  if (reason != nullptr) {
    ESP_LOGW(TAG, "HA key not sent: %s", reason);
    return this->record_ha_send_(label, reason);
  }
  report->ha_active = true;
  if (!this->emit_(*report, payload.data(), payload.size())) {
    report->ha_active = false;
    return this->record_ha_send_(label, "蓝牙协议栈未接受按下报告");
  }
  // Physical reports cancel this release, so it cannot clear a newly held physical key.
  this->set_timeout(static_cast<uint32_t>(id), 150, [this, id]() {
    auto *current = this->report_(id);
    if (!current->ha_active)
      return;
    current->ha_active = false;
    const uint8_t zero[8]{};
    if (!this->emit_(*current, zero, id == 1 ? 8 : 2))
      ESP_LOGW(TAG, "HA key release was not accepted; speaker may be unavailable");
  });
  // ESP_OK means the stack accepted the press, not that the speaker executed it.
  return this->record_ha_send_(label, nullptr);
}

void JBLRemoteRelay::reset_() {
  this->cancel_timeout("remote_setup");
  this->release_keys_();
  this->authenticated_ = false;
  this->security_started_ = false;
  this->discovered_ = false;
  this->setup_started_ = false;
  this->ready_ = false;
  this->references_.clear();
  this->reference_index_ = 0;
  this->subscription_index_ = 0;
  for (auto &report : this->reports_) {
    report.handle = 0;
    report.cccd = 0;
    report.last_received.fill(0);
  }
}

void JBLRemoteRelay::fail_(const char *reason) {
  ESP_LOGW(TAG, "Remote setup/stream failed: %s", reason);
  this->reset_();
  this->parent()->disconnect();
}

void JBLRemoteRelay::begin_discovery_() {
  if (!this->authenticated_ || !this->discovered_ || this->setup_started_)
    return;
  this->setup_started_ = true;
  auto *service = this->parent()->get_service(0x1812);
  if (service == nullptr) {
    this->fail_("HID service missing");
    return;
  }
  if (!service->parsed)
    service->parse_characteristics();
  for (auto *characteristic : service->characteristics) {
    if (characteristic->uuid != esp32_ble_tracker::ESPBTUUID::from_uint16(0x2A4D))
      continue;
    auto *reference = characteristic->get_descriptor(0x2908);
    auto *cccd = characteristic->get_descriptor(0x2902);
    if (reference == nullptr) {
      this->fail_("Report Reference missing");
      return;
    }
    this->references_.push_back({characteristic->handle, reference->handle,
                                 static_cast<uint16_t>(cccd == nullptr ? 0 : cccd->handle),
                                 (characteristic->properties & ESP_GATT_CHAR_PROP_BIT_NOTIFY) != 0});
  }
  if (this->references_.empty()) {
    this->fail_("HID reports missing");
    return;
  }
  this->read_next_reference_();
}

void JBLRemoteRelay::read_next_reference_() {
  if (this->reference_index_ == this->references_.size()) {
    for (const auto &report : this->reports_)
      if (report.handle == 0 || report.cccd == 0) {
        this->fail_("expected Input report 1/3/30 missing");
        return;
      }
    this->subscribe_next_();
    return;
  }
  const auto &reference = this->references_[this->reference_index_];
  if (esp_ble_gattc_read_char_descr(this->parent()->get_gattc_if(), this->parent()->get_conn_id(), reference.ref,
                                  ESP_GATT_AUTH_REQ_NONE) != ESP_OK)
    this->fail_("could not read Report Reference");
}

void JBLRemoteRelay::subscribe_next_() {
  if (this->subscription_index_ == this->reports_.size()) {
    this->cancel_timeout("remote_setup");
    this->ready_ = true;
    this->node_state = esp32_ble_tracker::ClientState::ESTABLISHED;
    this->references_.clear();
    ESP_LOGI(TAG, "Original remote ready: reports 1, 3, 30 subscribed");
    return;
  }
  if (this->parent()->register_for_notify(this->reports_[this->subscription_index_].handle) != ESP_OK)
    this->fail_("could not register report notification");
}

void JBLRemoteRelay::gap_event_handler(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t *param) {
  if (this->is_failed())
    return;
  if (this->remote_pairing_pending_) {
    if (event == ESP_GAP_BLE_REMOVE_BOND_DEV_COMPLETE_EVT && this->remote_bond_remove_pending_ &&
        this->parent()->check_addr(param->remove_bond_dev_cmpl.bd_addr)) {
      const bool success = param->remove_bond_dev_cmpl.status == ESP_BT_STATUS_SUCCESS;
      if (!success)
        ESP_LOGW(TAG, "Original remote bond removal failed: status 0x%02X", param->remove_bond_dev_cmpl.status);
      this->finish_remote_pairing_(success);
    }
    return;  // No discovery or authentication work for the link being removed.
  }
  if (event == ESP_GAP_BLE_SEC_REQ_EVT && this->parent()->check_addr(param->ble_security.ble_req.bd_addr)) {
    this->security_started_ = true;  // Native client already answered it; do not start security twice.
    return;
  }
  if (event != ESP_GAP_BLE_AUTH_CMPL_EVT ||
      !this->parent()->check_addr(param->ble_security.auth_cmpl.bd_addr) ||
      this->parent()->get_conn_id() == esp32_ble_client::UNSET_CONN_ID ||
      this->parent()->state() == esp32_ble_tracker::ClientState::DISCONNECTING)
    return;
  const auto &auth = param->ble_security.auth_cmpl;
  if (!auth.success || auth.addr_type != BLE_ADDR_TYPE_PUBLIC) {
    this->fail_("remote authentication failed");
    return;
  }
  this->authenticated_ = true;
  ESP_LOGI(TAG, "Original remote authenticated");
  this->begin_discovery_();
}

void JBLRemoteRelay::gattc_event_handler(esp_gattc_cb_event_t event, esp_gatt_if_t gattc_if,
                                        esp_ble_gattc_cb_param_t *param) {
  if (this->is_failed())
    return;
  if (this->remote_pairing_pending_) {
    if (event == ESP_GATTC_CONNECT_EVT)
      this->remote_link_closed_ = false;
    if (event == ESP_GATTC_CLOSE_EVT ||
        (event == ESP_GATTC_OPEN_EVT && param->open.status != ESP_GATT_OK &&
         param->open.status != ESP_GATT_ALREADY_OPEN))
      this->remote_link_closed_ = true;
    if (event == ESP_GATTC_DISCONNECT_EVT || event == ESP_GATTC_CLOSE_EVT)
      this->reset_();
    return;  // Native client still completes disconnect, including an in-flight OPEN.
  }
  // BLEClient filters GATTC by interface, peer and connection before node dispatch.
  switch (event) {
    case ESP_GATTC_CONNECT_EVT:
      this->reset_();
      if (param->connect.link_role != 0 || param->connect.ble_addr_type != BLE_ADDR_TYPE_PUBLIC)
        this->fail_("unexpected remote connection role/address type");
      break;
    case ESP_GATTC_OPEN_EVT:
      if ((param->open.status == ESP_GATT_OK || param->open.status == ESP_GATT_ALREADY_OPEN) &&
          this->parent()->state() != esp32_ble_tracker::ClientState::DISCONNECTING &&
          this->parent()->state() != esp32_ble_tracker::ClientState::IDLE) {
        this->set_timeout("remote_setup", 60000, [this]() { this->fail_("authentication/discovery/subscription timeout"); });
        if (!this->security_started_ && !this->authenticated_) {
          this->security_started_ = true;
          if (this->parent()->pair() != ESP_OK)
            this->fail_("could not request remote encryption");
        }
      }
      break;
    case ESP_GATTC_SEARCH_CMPL_EVT:
      if (param->search_cmpl.status != ESP_GATT_OK) {
        this->fail_("service discovery failed");
        break;
      }
      this->discovered_ = true;
      this->begin_discovery_();
      break;
    case ESP_GATTC_READ_DESCR_EVT: {
      if (!this->setup_started_ || this->reference_index_ >= this->references_.size())
        break;
      const auto &reference = this->references_[this->reference_index_];
      if (param->read.handle != reference.ref)
        break;
      if (param->read.status != ESP_GATT_OK || param->read.value_len != 2) {
        this->fail_("invalid Report Reference response");
        break;
      }
      const uint8_t id = param->read.value[0];
      const uint8_t type = param->read.value[1];
      ESP_LOGD(TAG, "Remote ReportRef handle=0x%04X id=%u type=%u", reference.handle, id, type);
      if (type == 1) {
        auto *report = this->report_(id);
        if (report == nullptr || report->handle != 0 || !reference.notify || reference.cccd == 0) {
          this->fail_("unexpected/duplicate Input Report Reference");
          break;
        }
        report->handle = reference.handle;
        report->cccd = reference.cccd;
      } else if (type != 2) {
        this->fail_("unexpected HID report type");
        break;
      }
      this->reference_index_++;
      this->read_next_reference_();
      break;
    }
    case ESP_GATTC_REG_FOR_NOTIFY_EVT:
      if (this->subscription_index_ < this->reports_.size() &&
          param->reg_for_notify.handle == this->reports_[this->subscription_index_].handle &&
          param->reg_for_notify.status != ESP_GATT_OK)
        this->fail_("notification registration failed");
      // Native V1 BLEClient writes this report's CCCD. Wait for its completion.
      break;
    case ESP_GATTC_WRITE_DESCR_EVT:
      if (this->subscription_index_ < this->reports_.size() &&
          param->write.handle == this->reports_[this->subscription_index_].cccd) {
        if (param->write.status != ESP_GATT_OK) {
          this->fail_("remote CCCD subscription failed");
          break;
        }
        ESP_LOGD(TAG, "Remote report %u subscribed", this->reports_[this->subscription_index_].id);
        this->subscription_index_++;
        this->subscribe_next_();
      }
      break;
    case ESP_GATTC_NOTIFY_EVT: {
      if (!this->ready_ || !this->authenticated_)
        break;
      auto found = std::find_if(this->reports_.begin(), this->reports_.end(), [param](const Report &report) {
        return report.handle == param->notify.handle;
      });
      if (found == this->reports_.end()) {
        ESP_LOGW(TAG, "Ignoring unknown notification handle 0x%04X", param->notify.handle);
        break;
      }
      const size_t size = param->notify.value_len;
      if ((found->id == 1 && size != 8) || (found->id == 3 && size != 2) || size == 0 || size > 255) {
        this->fail_("unexpected input payload length");
        break;
      }
      // Reception only: independent of HA send_key() and whether the speaker can accept this report.
      if (const auto *key = observe_remote_key(found->id, param->notify.value, size, found->last_received)) {
        if (this->last_key_text_sensor_ != nullptr)
          this->last_key_text_sensor_->publish_state(key->label);
        if (this->received_event_ != nullptr)
          this->received_event_->trigger(key->label);
      }
      if (found->id != 30) {
        this->cancel_timeout(static_cast<uint32_t>(found->id));
        found->ha_active = false;
        found->physical_down = std::any_of(param->notify.value, param->notify.value + size, [](uint8_t b) { return b != 0; });
      }
      this->emit_(*found, param->notify.value, size);
      ESP_LOGD(TAG, "Remote input report %u bytes=%u speaker_ready=%d", found->id,
               static_cast<unsigned>(size), this->is_speaker_ready());
      break;
    }
    case ESP_GATTC_DISCONNECT_EVT:
    case ESP_GATTC_CLOSE_EVT:
      this->reset_();
      break;
    default:
      break;
  }
}

}  // namespace esphome::jbl_remote_relay
