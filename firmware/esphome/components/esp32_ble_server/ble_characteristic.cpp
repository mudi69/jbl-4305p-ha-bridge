#include "ble_characteristic.h"
#include "ble_server.h"
#include "ble_service.h"
#include <cstdio>

#include "esphome/core/helpers.h"
#include "esphome/core/log.h"

#ifdef USE_ESP32

namespace esphome::esp32_ble_server {

static const char *const TAG = "esp32_ble_server.characteristic";

BLECharacteristic::~BLECharacteristic() {
  for (auto *descriptor : this->descriptors_) {
    delete descriptor;  // NOLINT(cppcoreguidelines-owning-memory)
  }
}

BLECharacteristic::BLECharacteristic(const ESPBTUUID uuid, uint32_t properties) : uuid_(uuid) {
  this->properties_ = (esp_gatt_char_prop_t) 0;

  this->set_broadcast_property((properties & PROPERTY_BROADCAST) != 0);
  this->set_indicate_property((properties & PROPERTY_INDICATE) != 0);
  this->set_notify_property((properties & PROPERTY_NOTIFY) != 0);
  this->set_read_property((properties & PROPERTY_READ) != 0);
  this->set_write_property((properties & PROPERTY_WRITE) != 0);
  this->set_write_no_response_property((properties & PROPERTY_WRITE_NR) != 0);
}

void BLECharacteristic::set_value(ByteBuffer buffer) { this->set_value(buffer.get_data()); }

void BLECharacteristic::set_value(std::vector<uint8_t> &&buffer) { this->value_ = std::move(buffer); }

void BLECharacteristic::set_value(std::initializer_list<uint8_t> data) {
  this->set_value(std::vector<uint8_t>(data));  // Delegate to move overload
}

void BLECharacteristic::set_value(const std::string &buffer) {
  this->set_value(std::vector<uint8_t>(buffer.begin(), buffer.end()));  // Delegate to move overload
}

bool BLECharacteristic::notify() {
  if (this->service_ == nullptr || this->service_->get_server() == nullptr ||
      this->service_->get_server()->get_connected_client_count() == 0)
    return false;

  const uint16_t *clients = this->service_->get_server()->get_clients();
  uint8_t client_count = this->service_->get_server()->get_client_count();
  bool accepted = false;

  for (uint8_t i = 0; i < client_count; i++) {
    uint16_t client = clients[i];
    if (this->report_id_ != 0 && !this->service_->get_server()->is_authenticated_speaker(client))
      continue;
    size_t length = this->value_.size();
    // Find the client in the list of clients to notify
    auto *entry = this->find_client_in_notify_list_(client);
    if (entry == nullptr)
      continue;
    bool require_ack = entry->indicate;
    // TODO: Remove this block when INDICATE acknowledgment is supported
    if (require_ack) {
      ESP_LOGW(TAG, "INDICATE acknowledgment is not yet supported (i.e. it works as a NOTIFY)");
      require_ack = false;
    }
    esp_err_t err = esp_ble_gatts_send_indicate(this->service_->get_server()->get_gatts_if(), client, this->handle_,
                                                length, this->value_.data(), require_ack);
    if (err != ESP_OK) {
      ESP_LOGE(TAG, "esp_ble_gatts_send_indicate failed %d", err);
      break;
    }
    accepted = true;
  }
  return accepted;
}

void BLECharacteristic::add_descriptor(BLEDescriptor *descriptor) {
  // If the descriptor is the CCCD descriptor, listen to its write event to know if the client wants to be notified
  if (descriptor->get_uuid() == ESPBTUUID::from_uint16(ESP_GATT_UUID_CHAR_CLIENT_CONFIG)) {
    this->cccd_descriptor_ = descriptor;
    descriptor->on_write([this](std::span<const uint8_t> value, uint16_t conn_id) {
      if (value.size() != 2)
        return;
      uint16_t cccd = encode_uint16(value[1], value[0]);
      if (!this->valid_cccd_(cccd))
        return;
      bool notify = (cccd & 1) != 0;
      bool indicate = (cccd & 2) != 0;
      ESP_LOGD(TAG, "CCCD handle 0x%04X connection %u: notify=%d indicate=%d", this->handle_, conn_id, notify,
               indicate);
      this->last_cccd_write_conn_id_ = conn_id;
      this->last_cccd_write_value_ = cccd;
      if (this->restoring_conn_id_ != 0xFFFF) {
        // A queued restore cannot be cancelled; enqueue the newer real write after it.
        const esp_err_t err = esp_ble_gatts_set_attr_value(this->cccd_handle_, value.size(), value.data());
        if (err != ESP_OK)
          ESP_LOGW(TAG, "Could not reconcile CCCD report %u: %s", this->report_id_, esp_err_to_name(err));
      }
      this->restoring_conn_id_ = 0xFFFF;
      this->apply_cccd_(conn_id, cccd);
      this->save_bonded_cccd_(conn_id, cccd);
    });
  }
  this->descriptors_.push_back(descriptor);
}

bool BLECharacteristic::valid_cccd_(uint16_t value) const {
  const uint16_t allowed = ((this->properties_ & ESP_GATT_CHAR_PROP_BIT_NOTIFY) ? 1 : 0) |
                           ((this->properties_ & ESP_GATT_CHAR_PROP_BIT_INDICATE) ? 2 : 0);
  return (value & ~allowed) == 0;
}

void BLECharacteristic::apply_cccd_(uint16_t conn_id, uint16_t value) {
  this->remove_client_from_notify_list_(conn_id);
  if (value != 0)
    this->clients_to_notify_.push_back({conn_id, (value & 2) != 0});
}

bool BLECharacteristic::can_notify() const {
  for (const auto &client : this->clients_to_notify_)
    if (this->report_id_ == 0 || this->service_->get_server()->is_authenticated_speaker(client.conn_id))
      return true;
  return false;
}

void BLECharacteristic::save_bonded_cccd_(uint16_t conn_id, uint16_t value) {
  if (this->report_id_ == 0 || !this->service_->get_server()->is_authenticated_speaker(conn_id))
    return;
  if (this->cccd_preference_.save(&value) && global_preferences->sync()) {
    ESP_LOGI(TAG, "Saved bonded CCCD report %u: 0x%04X", this->report_id_, value);
  } else {
    ESP_LOGW(TAG, "Could not persist bonded CCCD report %u", this->report_id_);
  }
}

bool BLECharacteristic::clear_bonded_cccd() {
  if (this->report_id_ == 0)
    return true;
  this->clients_to_notify_.clear();
  this->last_cccd_write_conn_id_ = 0xFFFF;
  this->restoring_conn_id_ = 0xFFFF;
  const uint16_t value = 0;
  const uint8_t bytes[2] = {0, 0};
  this->cccd_descriptor_->set_value({0, 0});
  if (this->cccd_handle_ != 0xFFFF)
    esp_ble_gatts_set_attr_value(this->cccd_handle_, sizeof(bytes), bytes);
  ESP_LOGD(TAG, "Cleared speaker CCCD report %u", this->report_id_);
  return this->cccd_preference_.save(&value);
}

void BLECharacteristic::restore_bonded_cccd(uint16_t conn_id) {
  if (this->report_id_ == 0 || this->cccd_handle_ == 0xFFFF ||
      !this->service_->get_server()->is_authenticated_speaker(conn_id))
    return;
  // An actual write on this connection (including unsubscribe) wins over saved state.
  if (this->last_cccd_write_conn_id_ == conn_id) {
    this->save_bonded_cccd_(conn_id, this->last_cccd_write_value_);
    return;
  }
  uint16_t value = 0;
  if (!this->cccd_preference_.load(&value)) {
    ESP_LOGI(TAG, "No saved bonded CCCD report %u; waiting for subscription", this->report_id_);
    return;
  }
  if (!this->valid_cccd_(value)) {
    ESP_LOGW(TAG, "Invalid saved bonded CCCD report %u", this->report_id_);
    return;
  }
  const uint8_t data[2] = {static_cast<uint8_t>(value), static_cast<uint8_t>(value >> 8)};
  this->restoring_conn_id_ = conn_id;
  this->restoring_cccd_ = value;
  const esp_err_t err = esp_ble_gatts_set_attr_value(this->cccd_handle_, sizeof(data), data);
  if (err != ESP_OK) {
    this->restoring_conn_id_ = 0xFFFF;
    ESP_LOGW(TAG, "Could not restore CCCD report %u: %s", this->report_id_, esp_err_to_name(err));
  }
}

void BLECharacteristic::remove_descriptor(BLEDescriptor *descriptor) {
  this->descriptors_.erase(std::remove(this->descriptors_.begin(), this->descriptors_.end(), descriptor),
                           this->descriptors_.end());
}

void BLECharacteristic::do_create(BLEService *service) {
  this->service_ = service;
  esp_attr_control_t control;
  control.auto_rsp = ESP_GATT_RSP_BY_APP;

#if ESPHOME_LOG_LEVEL >= ESPHOME_LOG_LEVEL_VERBOSE
  char uuid_buf[esp32_ble::UUID_STR_LEN];
  this->uuid_.to_str(uuid_buf);
  ESP_LOGV(TAG, "Creating characteristic - %s", uuid_buf);
#endif

  esp_bt_uuid_t uuid = this->uuid_.get_uuid();
  esp_err_t err = esp_ble_gatts_add_char(service->get_handle(), &uuid, static_cast<esp_gatt_perm_t>(this->permissions_),
                                         this->properties_, nullptr, &control);

  if (err != ESP_OK) {
    ESP_LOGE(TAG, "esp_ble_gatts_add_char failed: %d", err);
    this->state_ = FAILED;
    return;
  }

  this->state_ = CREATING;
}

bool BLECharacteristic::is_created() {
  if (this->state_ == CREATED)
    return true;

  if (this->state_ != CREATING_DEPENDENTS)
    return false;

  for (auto *descriptor : this->descriptors_) {
    if (!descriptor->is_created())
      return false;
  }
  // All descriptors are created if we reach here
  this->state_ = CREATED;
  return true;
}

bool BLECharacteristic::is_failed() {
  if (this->state_ == FAILED)
    return true;

  for (auto *descriptor : this->descriptors_) {
    if (descriptor->is_failed()) {
      this->state_ = FAILED;
      return true;
    }
  }
  return false;
}

void BLECharacteristic::set_property_bit_(esp_gatt_char_prop_t bit, bool value) {
  if (value) {
    this->properties_ = (esp_gatt_char_prop_t) (this->properties_ | bit);
  } else {
    this->properties_ = (esp_gatt_char_prop_t) (this->properties_ & ~bit);
  }
}

void BLECharacteristic::set_broadcast_property(bool value) {
  this->set_property_bit_(ESP_GATT_CHAR_PROP_BIT_BROADCAST, value);
}
void BLECharacteristic::set_indicate_property(bool value) {
  this->set_property_bit_(ESP_GATT_CHAR_PROP_BIT_INDICATE, value);
}
void BLECharacteristic::set_notify_property(bool value) {
  this->set_property_bit_(ESP_GATT_CHAR_PROP_BIT_NOTIFY, value);
}
void BLECharacteristic::set_read_property(bool value) { this->set_property_bit_(ESP_GATT_CHAR_PROP_BIT_READ, value); }
void BLECharacteristic::set_write_property(bool value) { this->set_property_bit_(ESP_GATT_CHAR_PROP_BIT_WRITE, value); }
void BLECharacteristic::set_write_no_response_property(bool value) {
  this->set_property_bit_(ESP_GATT_CHAR_PROP_BIT_WRITE_NR, value);
}

void BLECharacteristic::gatts_event_handler(esp_gatts_cb_event_t event, esp_gatt_if_t gatts_if,
                                            esp_ble_gatts_cb_param_t *param) {
  switch (event) {
    case ESP_GATTS_ADD_CHAR_EVT: {
      // Identical report UUIDs are valid; only the pending instance owns this event.
      if (this->state_ == CREATING && this->service_ != nullptr &&
          this->service_->get_handle() == param->add_char.service_handle &&
          this->service_->get_last_created_characteristic() == this &&
          this->uuid_ == ESPBTUUID::from_uuid(param->add_char.char_uuid)) {
        if (param->add_char.status != ESP_GATT_OK) {
          ESP_LOGE(TAG, "Characteristic creation failed: %d", param->add_char.status);
          this->state_ = FAILED;
          break;
        }
        this->handle_ = param->add_char.attr_handle;

        for (auto *descriptor : this->descriptors_) {
          descriptor->do_create(this);
        }

        this->state_ = CREATING_DEPENDENTS;
      }
      break;
    }
    case ESP_GATTS_ADD_CHAR_DESCR_EVT: {
      if (this->service_ == nullptr || this->service_->get_last_created_characteristic() != this ||
          this->service_->get_handle() != param->add_char_descr.service_handle ||
          param->add_char_descr.status != ESP_GATT_OK)
        break;
      const auto uuid = ESPBTUUID::from_uuid(param->add_char_descr.descr_uuid);
      if (uuid == ESPBTUUID::from_uint16(0x2902)) {
        this->cccd_handle_ = param->add_char_descr.attr_handle;
      } else if (uuid == ESPBTUUID::from_uint16(0x2908) && this->report_id_ == 0 &&
                 this->service_->get_uuid() == ESPBTUUID::from_uint16(0x1812) &&
                 this->uuid_ == ESPBTUUID::from_uint16(0x2A4D)) {
        uint16_t length = 0;
        const uint8_t *value = nullptr;
        if (esp_ble_gatts_get_attr_value(param->add_char_descr.attr_handle, &length, &value) == ESP_GATT_OK &&
            length == 2 && value != nullptr && value[1] == 1 && (value[0] == 1 || value[0] == 3 || value[0] == 30)) {
          this->report_id_ = value[0];
          // Stable speaker + service + report identity; never an ephemeral connection/attribute handle.
          const auto &address = this->service_->get_server()->get_speaker_address();
          char identity[64];
          snprintf(identity, sizeof(identity), "jbl4305p.cccd.v1.%02x%02x%02x%02x%02x%02x.1812.input",
                   address[0], address[1], address[2], address[3], address[4], address[5]);
          const uint32_t key = fnv1_hash_extend(fnv1_hash(identity), this->report_id_);
          this->cccd_preference_ = global_preferences->make_preference<uint16_t>(key, true);
          this->service_->get_server()->register_bonded_characteristic(this);
          ESP_LOGD(TAG, "Bonded CCCD storage ready for report %u", this->report_id_);
        }
      }
      break;
    }
    case ESP_GATTS_SET_ATTR_VAL_EVT:
      if (param->set_attr_val.attr_handle == this->cccd_handle_ && this->restoring_conn_id_ != 0xFFFF) {
        const uint16_t conn_id = this->restoring_conn_id_;
        this->restoring_conn_id_ = 0xFFFF;
        if (param->set_attr_val.status == ESP_GATT_OK && this->service_->get_server()->is_authenticated_speaker(conn_id)) {
          this->apply_cccd_(conn_id, this->restoring_cccd_);
          this->cccd_descriptor_->set_value({static_cast<uint8_t>(this->restoring_cccd_),
                                            static_cast<uint8_t>(this->restoring_cccd_ >> 8)});
          ESP_LOGI(TAG, "Restored bonded CCCD report %u: 0x%04X", this->report_id_, this->restoring_cccd_);
        } else {
          ESP_LOGW(TAG, "CCCD restore failed for report %u: status 0x%02X", this->report_id_, param->set_attr_val.status);
        }
      }
      break;
    case ESP_GATTS_DISCONNECT_EVT:
      // Clear this connection's live state; only an authenticated speaker may restore its saved subscription.
      this->remove_client_from_notify_list_(param->disconnect.conn_id);
      if (this->last_cccd_write_conn_id_ == param->disconnect.conn_id)
        this->last_cccd_write_conn_id_ = 0xFFFF;
      this->restoring_conn_id_ = 0xFFFF;
      if (this->report_id_ != 0 && this->cccd_handle_ != 0xFFFF) {
        const uint8_t empty[2] = {0, 0};
        this->cccd_descriptor_->set_value({0, 0});
        esp_ble_gatts_set_attr_value(this->cccd_handle_, sizeof(empty), empty);
      }
      break;
    case ESP_GATTS_READ_EVT: {
      if (param->read.handle != this->handle_)
        break;  // Not this characteristic

      if (!param->read.need_rsp)
        break;  // For some reason you can request a read but not want a response

      if (this->on_read_callback_) {
        (*this->on_read_callback_)(param->read.conn_id);
      }

      // Use the client-supplied offset for long reads; short reads always start at 0.
      // The Bluedroid stack truncates ATT_READ_RSP / ATT_READ_BLOB_RSP to MTU-1, so we
      // just provide as much data as we have from the requested offset and let the stack
      // handle framing. The client issues subsequent blob reads with increasing offsets
      // until it has received the whole value.
      const uint16_t offset = param->read.is_long ? param->read.offset : 0;
      esp_gatt_status_t status = ESP_GATT_OK;
      esp_gatt_rsp_t response;
      response.attr_value.offset = offset;

      if (offset > this->value_.size()) {
        status = ESP_GATT_INVALID_OFFSET;
        response.attr_value.len = 0;
      } else {
        size_t remaining = this->value_.size() - offset;
        if (remaining > ESP_GATT_MAX_ATTR_LEN) {
          ESP_LOGW(TAG, "Characteristic length %u exceeds buffer size of %u, truncating",
                   static_cast<unsigned>(remaining), ESP_GATT_MAX_ATTR_LEN);
          remaining = ESP_GATT_MAX_ATTR_LEN;
        }
        response.attr_value.len = remaining;
        memcpy(response.attr_value.value, this->value_.data() + offset, remaining);
      }

      response.attr_value.handle = this->handle_;
      response.attr_value.auth_req = ESP_GATT_AUTH_REQ_NONE;

      esp_err_t err =
          esp_ble_gatts_send_response(gatts_if, param->read.conn_id, param->read.trans_id, status, &response);
      if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ble_gatts_send_response failed: %d", err);
      }
      break;
    }
    case ESP_GATTS_WRITE_EVT: {
      if (this->handle_ != param->write.handle)
        break;

      esp_gatt_status_t status = ESP_GATT_OK;

      if (param->write.is_prep) {
        const size_t offset = param->write.offset;
        const size_t write_len = param->write.len;
        const size_t new_size = offset + write_len;
        // Clean the buffer on the first prepared write event
        if (offset == 0) {
          this->value_.clear();
        }

        if (offset != this->value_.size()) {
          status = ESP_GATT_INVALID_OFFSET;
        } else if (new_size > ESP_GATT_MAX_ATTR_LEN) {
          status = ESP_GATT_INVALID_ATTR_LEN;
        } else {
          if (this->value_.size() < new_size) {
            this->value_.resize(new_size);
          }
          memcpy(this->value_.data() + offset, param->write.value, write_len);
        }
      } else {
        this->set_value(ByteBuffer::wrap(param->write.value, param->write.len));
      }

      if (param->write.need_rsp) {
        esp_gatt_rsp_t response;

        response.attr_value.len = param->write.len;
        response.attr_value.handle = this->handle_;
        response.attr_value.offset = param->write.offset;
        response.attr_value.auth_req = ESP_GATT_AUTH_REQ_NONE;
        memcpy(response.attr_value.value, param->write.value, param->write.len);

        esp_err_t err =
            esp_ble_gatts_send_response(gatts_if, param->write.conn_id, param->write.trans_id, status, &response);

        if (err != ESP_OK) {
          ESP_LOGE(TAG, "esp_ble_gatts_send_response failed: %d", err);
        }
      }

      if (!param->write.is_prep) {
        if (this->on_write_callback_) {
          (*this->on_write_callback_)(this->value_, param->write.conn_id);
        }
      }

      break;
    }

    case ESP_GATTS_EXEC_WRITE_EVT: {
      // BLE stack will guarantee that ESP_GATTS_EXEC_WRITE_EVT is only received after prepared writes
      if (this->value_.empty())
        break;
      if (param->exec_write.exec_write_flag == ESP_GATT_PREP_WRITE_EXEC) {
        if (this->on_write_callback_) {
          (*this->on_write_callback_)(this->value_, param->exec_write.conn_id);
        }
      }
      esp_err_t err = esp_ble_gatts_send_response(gatts_if, param->exec_write.conn_id, param->exec_write.trans_id,
                                                  ESP_GATT_OK, nullptr);
      if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ble_gatts_send_response failed: %d", err);
      }
      break;
    }
    default:
      break;
  }

  for (auto *descriptor : this->descriptors_) {
    descriptor->gatts_event_handler(event, gatts_if, param);
  }
}

void BLECharacteristic::remove_client_from_notify_list_(uint16_t conn_id) {
  // Since we typically have very few clients (often just 1), we can optimize
  // for the common case by swapping with the last element and popping
  for (size_t i = 0; i < this->clients_to_notify_.size(); i++) {
    if (this->clients_to_notify_[i].conn_id == conn_id) {
      // Swap with last element and pop (safe even when i is the last element)
      this->clients_to_notify_[i] = this->clients_to_notify_.back();
      this->clients_to_notify_.pop_back();
      return;
    }
  }
}

BLECharacteristic::ClientNotificationEntry *BLECharacteristic::find_client_in_notify_list_(uint16_t conn_id) {
  for (auto &entry : this->clients_to_notify_) {
    if (entry.conn_id == conn_id) {
      return &entry;
    }
  }
  return nullptr;
}

}  // namespace esphome::esp32_ble_server

#endif
