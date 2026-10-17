#include "ble_server.h"

#include "esphome/components/esp32_ble/ble.h"
#include "esphome/core/log.h"
#include "esphome/core/application.h"
#include "esphome/core/version.h"

#ifdef USE_ESP32

#include <cstring>
#include <nvs_flash.h>
#include <freertos/FreeRTOSConfig.h>
#include <esp_bt_main.h>
#ifndef CONFIG_ESP_HOSTED_ENABLE_BT_BLUEDROID
#include <esp_bt.h>
#endif
#include <freertos/task.h>
#include <esp_gap_ble_api.h>

namespace esphome::esp32_ble_server {

static const char *const TAG = "esp32_ble_server";

bool BLEServer::has_speaker_bond_(bool *query_ok) const {
  if (query_ok != nullptr)
    *query_ok = false;
  int count = esp_ble_get_bond_device_num();
  if (count <= 0) {
    if (query_ok != nullptr)
      *query_ok = count == 0;
    return false;
  }
  std::vector<esp_ble_bond_dev_t> bonds(count);
  if (esp_ble_get_bond_device_list(&count, bonds.data()) != ESP_OK)
    return false;
  if (query_ok != nullptr)
    *query_ok = true;
  for (const auto &bond : bonds) {
    if (bond.bd_addr_type == BLE_ADDR_TYPE_PUBLIC && memcmp(bond.bd_addr, this->speaker_address_.data(), this->speaker_address_.size()) == 0)
      return true;
  }
  return false;
}

void BLEServer::setup() {
  if (this->parent_->is_failed()) {
    this->mark_failed();
    ESP_LOGE(TAG, "BLE Server was marked failed by ESP32BLE");
    return;
  }
  global_ble_server = this;
}

void BLEServer::loop() {
  if (!this->parent_->is_active()) {
    return;
  }
  switch (this->state_) {
    case RUNNING: {
      // Start all services that are pending to start
      if (!this->services_to_start_.empty()) {
        size_t write_idx = 0;
        for (auto *service : this->services_to_start_) {
          if (service->is_created()) {
            service->start();  // Needs to be called once per characteristic in the service
          }
          // Remove services that have started or are starting
          if (!service->is_starting() && !service->is_running()) {
            this->services_to_start_[write_idx++] = service;
          }
        }
        this->services_to_start_.erase(this->services_to_start_.begin() + write_idx, this->services_to_start_.end());
      }
      if (this->advertising_required_ && !this->advertising_requested_) {
        bool all_services_running = true;
        for (const auto &entry : this->services_) {
          if (!entry.service->is_running()) {
            all_services_running = false;
            break;
          }
        }
        if (all_services_running) {
          this->restart_advertising_();
          this->request_advertising_();
          ESP_LOGD(TAG, "All BLE services running; advertising requested");
        }
      }
      break;
    }
    case INIT: {
      esp_err_t err = esp_ble_gatts_app_register(0);
      if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ble_gatts_app_register failed: %d", err);
        this->mark_failed();
        return;
      }
      this->state_ = REGISTERING;
      break;
    }
    case REGISTERING: {
      if (this->registered_) {
        // Create the device information service first so
        // it is at the top of the GATT table
        this->device_information_service_->do_create(this);
        // Create all services previously created
        for (auto &entry : this->services_) {
          if (entry.service == this->device_information_service_) {
            continue;
          }
          entry.service->do_create(this);
        }
        this->state_ = STARTING_SERVICE;
      }
      break;
    }
    case STARTING_SERVICE: {
      if (this->device_information_service_->is_running()) {
        this->state_ = RUNNING;
        ESP_LOGD(TAG, "BLE server setup successfully");
      } else if (this->device_information_service_->is_created()) {
        this->device_information_service_->start();
      }
      break;
    }
  }
}

bool BLEServer::can_proceed() { return this->is_running() || !this->parent_->is_active(); }

void BLEServer::restart_advertising_() {
  if (this->is_running()) {
    this->parent_->advertising_set_manufacturer_data(this->manufacturer_data_);
  }
}

void BLEServer::request_advertising_() {
  if (!this->advertising_required_ || this->advertising_requested_)
    return;
  this->advertising_requested_ = true;
  this->parent_->advertising_start();
}

void BLEServer::release_advertising_() {
  if (!this->advertising_requested_)
    return;
  this->advertising_requested_ = false;
  this->parent_->advertising_stop();
}

BLEService *BLEServer::create_service(ESPBTUUID uuid, bool advertise, uint16_t num_handles) {
#if ESPHOME_LOG_LEVEL >= ESPHOME_LOG_LEVEL_VERBOSE
  char uuid_buf[esp32_ble::UUID_STR_LEN];
  uuid.to_str(uuid_buf);
  ESP_LOGV(TAG, "Creating BLE service - %s", uuid_buf);
#endif
  // Calculate the inst_id for the service
  uint8_t inst_id = 0;
  for (; inst_id < 0xFF; inst_id++) {
    if (this->get_service(uuid, inst_id) == nullptr) {
      break;
    }
  }
  if (inst_id == 0xFF) {
    char warn_uuid_buf[esp32_ble::UUID_STR_LEN];
    uuid.to_str(warn_uuid_buf);
    ESP_LOGW(TAG, "Could not create BLE service %s, too many instances", warn_uuid_buf);
    return nullptr;
  }
  BLEService *service =  // NOLINT(cppcoreguidelines-owning-memory)
      new BLEService(uuid, num_handles, inst_id, advertise);
  this->services_.push_back({uuid, inst_id, service});
  if (this->parent_->is_active() && this->registered_) {
    service->do_create(this);
  }
  return service;
}

void BLEServer::remove_service(ESPBTUUID uuid, uint8_t inst_id) {
#if ESPHOME_LOG_LEVEL >= ESPHOME_LOG_LEVEL_VERBOSE
  char uuid_buf[esp32_ble::UUID_STR_LEN];
  uuid.to_str(uuid_buf);
  ESP_LOGV(TAG, "Removing BLE service - %s %d", uuid_buf, inst_id);
#endif
  for (auto it = this->services_.begin(); it != this->services_.end(); ++it) {
    if (it->uuid == uuid && it->inst_id == inst_id) {
      it->service->do_delete();
      delete it->service;  // NOLINT(cppcoreguidelines-owning-memory)
      this->services_.erase(it);
      return;
    }
  }
  char warn_uuid_buf[esp32_ble::UUID_STR_LEN];
  uuid.to_str(warn_uuid_buf);
  ESP_LOGW(TAG, "BLE service %s %d does not exist", warn_uuid_buf, inst_id);
}

BLEService *BLEServer::get_service(ESPBTUUID uuid, uint8_t inst_id) {
  for (auto &entry : this->services_) {
    if (entry.uuid == uuid && entry.inst_id == inst_id) {
      return entry.service;
    }
  }
  return nullptr;
}

void BLEServer::dispatch_callbacks_(CallbackType type, uint16_t conn_id) {
  for (auto &entry : this->callbacks_) {
    if (entry.type == type) {
      entry.callback(conn_id);
    }
  }
}

void BLEServer::register_bonded_characteristic(BLECharacteristic *characteristic) {
  for (auto *existing : this->bonded_characteristics_) {
    if (existing == characteristic)
      return;
  }
  this->bonded_characteristics_.push_back(characteristic);
}

void BLEServer::start_speaker_pairing() {
  if (!this->is_running() || !this->advertising_requested_ || this->speaker_pairing_pending_) {
    ESP_LOGW(TAG, "Speaker pairing unavailable: server not ready or request pending");
    return;
  }
  this->speaker_pairing_pending_ = true;
  this->speaker_bond_removed_ = false;
  // Keep the parent's advertising reference; suppress reconnect while removing the bond.
  if (this->speaker_conn_id_ == 0xFFFF)
    esp_ble_gap_stop_advertising();
  esp_bd_addr_t address;
  memcpy(address, this->speaker_address_.data(), sizeof(address));
  const esp_err_t err = esp_ble_remove_bond_device(address);
  if (err != ESP_OK) {
    this->speaker_pairing_pending_ = false;
    this->parent_->advertising_refresh();
    ESP_LOGW(TAG, "Could not request speaker bond removal: %s", esp_err_to_name(err));
    return;
  }
  ESP_LOGI(TAG, "Removing only the JBL speaker bond for pairing");
}

void BLEServer::finish_speaker_pairing_() {
  if (!this->speaker_pairing_pending_ || !this->speaker_bond_removed_ || this->speaker_conn_id_ != 0xFFFF)
    return;
  this->speaker_authenticated_ = false;
  bool saved = true;
  for (auto *characteristic : this->bonded_characteristics_)
    saved = characteristic->clear_bonded_cccd() && saved;
  const bool synced = global_preferences->sync();
  if (!saved || !synced)
    ESP_LOGW(TAG, "Could not persist all cleared speaker subscriptions");
  this->speaker_pairing_pending_ = false;
  this->speaker_bond_removed_ = false;
  this->parent_->advertising_refresh();
  ESP_LOGI(TAG, "Speaker bond removed; ordinary pairing advertising ready");
}

void BLEServer::gap_event_handler(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t *param) {
  if (param == nullptr)
    return;

  // GAP security events are global; the native BLE client owns the original remote.
  const uint8_t *peer = nullptr;
  switch (event) {
    case ESP_GAP_BLE_SEC_REQ_EVT:
    case ESP_GAP_BLE_PASSKEY_REQ_EVT:
      peer = param->ble_security.ble_req.bd_addr;
      break;
    case ESP_GAP_BLE_AUTH_CMPL_EVT:
      peer = param->ble_security.auth_cmpl.bd_addr;
      break;
    case ESP_GAP_BLE_NC_REQ_EVT:
      peer = param->ble_security.key_notif.bd_addr;
      break;
    default:
      break;
  }
  if (peer != nullptr && memcmp(peer, this->speaker_address_.data(), this->speaker_address_.size()) != 0)
    return;

  // Pairing parameters (NoIO and bonding) are configured by esp32_ble.
  // Respond to the central; do not start a second security procedure here.
  switch (event) {
    case ESP_GAP_BLE_REMOVE_BOND_DEV_COMPLETE_EVT: {
      const auto &removed = param->remove_bond_dev_cmpl;
      if (!this->speaker_pairing_pending_ || memcmp(removed.bd_addr, this->speaker_address_.data(), this->speaker_address_.size()) != 0)
        break;
      bool query_ok = false;
      const bool still_bonded = this->has_speaker_bond_(&query_ok);
      if (removed.status == ESP_BT_STATUS_SUCCESS || (query_ok && !still_bonded)) {
        this->speaker_bond_removed_ = true;
        this->finish_speaker_pairing_();
      } else {
        this->speaker_pairing_pending_ = false;
        this->parent_->advertising_refresh();
        ESP_LOGW(TAG, "Speaker bond removal failed: status 0x%02X", removed.status);
      }
      break;
    }
    case ESP_GAP_BLE_SEC_REQ_EVT: {
      const esp_err_t err = esp_ble_gap_security_rsp(param->ble_security.ble_req.bd_addr, true);
      if (err == ESP_OK) {
        ESP_LOGI(TAG, "Accepted BLE security request");
      } else {
        ESP_LOGW(TAG, "BLE security response failed: %s", esp_err_to_name(err));
      }
      break;
    }
    case ESP_GAP_BLE_AUTH_CMPL_EVT: {
      const auto &auth = param->ble_security.auth_cmpl;
      if (auth.success) {
        ESP_LOGI(TAG, "BLE authentication succeeded (mode 0x%02X, bonds %d)", auth.auth_mode,
                 esp_ble_get_bond_device_num());
        if (!this->speaker_pairing_pending_ && this->speaker_conn_id_ != 0xFFFF && auth.addr_type == BLE_ADDR_TYPE_PUBLIC &&
            memcmp(auth.bd_addr, this->speaker_address_.data(), this->speaker_address_.size()) == 0 && this->has_speaker_bond_()) {
          this->speaker_authenticated_ = true;
          for (auto *characteristic : this->bonded_characteristics_)
            characteristic->restore_bonded_cccd(this->speaker_conn_id_);
        }
      } else {
        ESP_LOGW(TAG, "BLE authentication failed (reason 0x%02X)", auth.fail_reason);
        if (memcmp(auth.bd_addr, this->speaker_address_.data(), this->speaker_address_.size()) == 0)
          this->speaker_authenticated_ = false;
      }
      break;
    }
    case ESP_GAP_BLE_NC_REQ_EVT:
      ESP_LOGW(TAG, "Unexpected numeric comparison in NoIO mode; rejecting");
      esp_ble_confirm_reply(param->ble_security.key_notif.bd_addr, false);
      break;
    case ESP_GAP_BLE_PASSKEY_REQ_EVT:
      ESP_LOGW(TAG, "Unexpected passkey request in NoIO mode; rejecting");
      esp_ble_passkey_reply(param->ble_security.ble_req.bd_addr, false, 0);
      break;
    default:
      break;
  }
}

void BLEServer::gatts_event_handler(esp_gatts_cb_event_t event, esp_gatt_if_t gatts_if,
                                    esp_ble_gatts_cb_param_t *param) {
  // Bluedroid reports outgoing central ACL links to GATTS too. Never let the
  // remote's lifecycle clear speaker CCCDs, affect its count, or restart advertising.
  switch (event) {
    case ESP_GATTS_CONNECT_EVT:
      if (param->connect.link_role != 1 || param->connect.ble_addr_type != BLE_ADDR_TYPE_PUBLIC ||
          memcmp(param->connect.remote_bda, this->speaker_address_.data(), this->speaker_address_.size()) != 0)
        return;
      break;
    case ESP_GATTS_DISCONNECT_EVT:
      if (memcmp(param->disconnect.remote_bda, this->speaker_address_.data(), this->speaker_address_.size()) != 0 ||
          param->disconnect.conn_id != this->speaker_conn_id_)
        return;
      break;
    case ESP_GATTS_READ_EVT:
      if (param->read.conn_id != this->speaker_conn_id_)
        return;
      break;
    case ESP_GATTS_WRITE_EVT:
      if (param->write.conn_id != this->speaker_conn_id_)
        return;
      break;
    case ESP_GATTS_EXEC_WRITE_EVT:
      if (param->exec_write.conn_id != this->speaker_conn_id_)
        return;
      break;
    default:
      break;
  }
  switch (event) {
    case ESP_GATTS_CREATE_EVT:
      ESP_LOGV(TAG, "GATTS CREATE service=0x%04X status=0x%02X", param->create.service_handle, param->create.status);
      break;
    case ESP_GATTS_START_EVT:
      ESP_LOGV(TAG, "GATTS START service=0x%04X status=0x%02X", param->start.service_handle, param->start.status);
      break;
    case ESP_GATTS_ADD_CHAR_EVT:
      ESP_LOGV(TAG, "GATTS ADD_CHAR service=0x%04X handle=0x%04X status=0x%02X", param->add_char.service_handle,
               param->add_char.attr_handle, param->add_char.status);
      break;
    case ESP_GATTS_ADD_CHAR_DESCR_EVT:
      ESP_LOGV(TAG, "GATTS ADD_DESCR service=0x%04X handle=0x%04X status=0x%02X", param->add_char_descr.service_handle,
               param->add_char_descr.attr_handle, param->add_char_descr.status);
      break;
    case ESP_GATTS_READ_EVT:
      ESP_LOGV(TAG, "GATTS READ handle=0x%04X", param->read.handle);
      break;
    case ESP_GATTS_WRITE_EVT:
      ESP_LOGV(TAG, "GATTS WRITE handle=0x%04X len=%u need_rsp=%d", param->write.handle, param->write.len,
               param->write.need_rsp);
      break;
    case ESP_GATTS_CONNECT_EVT: {
      ESP_LOGD(TAG, "BLE Client connected peer=%02X:%02X:%02X:%02X:%02X:%02X", param->connect.remote_bda[0],
               param->connect.remote_bda[1], param->connect.remote_bda[2], param->connect.remote_bda[3],
               param->connect.remote_bda[4], param->connect.remote_bda[5]);
      this->add_client_(param->connect.conn_id);
      if (param->connect.ble_addr_type == BLE_ADDR_TYPE_PUBLIC &&
          memcmp(param->connect.remote_bda, this->speaker_address_.data(), this->speaker_address_.size()) == 0) {
        this->speaker_conn_id_ = param->connect.conn_id;
        this->speaker_authenticated_ = false;
      }
      // Resume advertising so additional clients can discover and connect
      if (!this->speaker_pairing_pending_ && this->client_count_ < this->max_clients_) {
        this->parent_->advertising_refresh();
      }
      this->dispatch_callbacks_(CallbackType::ON_CONNECT, param->connect.conn_id);
      break;
    }
    case ESP_GATTS_DISCONNECT_EVT: {
      ESP_LOGD(TAG, "BLE Client disconnected reason=0x%02X", param->disconnect.reason);
      this->remove_client_(param->disconnect.conn_id);
      if (this->speaker_conn_id_ == param->disconnect.conn_id) {
        this->speaker_conn_id_ = 0xFFFF;
        this->speaker_authenticated_ = false;
      }
      if (this->speaker_pairing_pending_)
        this->finish_speaker_pairing_();
      else
        this->parent_->advertising_refresh();
      this->dispatch_callbacks_(CallbackType::ON_DISCONNECT, param->disconnect.conn_id);
      break;
    }
    case ESP_GATTS_REG_EVT: {
      this->gatts_if_ = gatts_if;
      this->registered_ = true;
      break;
    }
    default:
      break;
  }

  for (auto &entry : this->services_) {
    entry.service->gatts_event_handler(event, gatts_if, param);
  }
}

int8_t BLEServer::find_client_index_(uint16_t conn_id) const {
  for (uint8_t i = 0; i < this->client_count_; i++) {
    if (this->clients_[i] == conn_id)
      return i;
  }
  return -1;
}

void BLEServer::add_client_(uint16_t conn_id) {
  // Check if already in list
  if (this->find_client_index_(conn_id) >= 0)
    return;
  // Add if there's space
  if (this->client_count_ < USE_ESP32_BLE_MAX_CONNECTIONS) {
    this->clients_[this->client_count_++] = conn_id;
  } else {
    // This should never happen since max clients is known at compile time
    ESP_LOGE(TAG, "Client array full");
  }
}

void BLEServer::remove_client_(uint16_t conn_id) {
  int8_t index = this->find_client_index_(conn_id);
  if (index >= 0) {
    // Replace with last element and decrement count (client order not preserved)
    this->clients_[index] = this->clients_[--this->client_count_];
  }
}

void BLEServer::ble_before_disabled_event_handler() {
  // Advertising is re-requested once the server is running again after BLE is re-enabled
  this->release_advertising_();
  // Delete all clients
  this->client_count_ = 0;
  this->speaker_conn_id_ = 0xFFFF;
  this->speaker_authenticated_ = false;
  this->speaker_pairing_pending_ = false;
  this->speaker_bond_removed_ = false;
  // Delete all services
  for (auto &entry : this->services_) {
    entry.service->do_delete();
  }
  this->registered_ = false;
  this->state_ = INIT;
}

float BLEServer::get_setup_priority() const { return setup_priority::AFTER_BLUETOOTH + 10; }

void BLEServer::dump_config() {
  ESP_LOGCONFIG(TAG,
                "ESP32 BLE Server:\n"
                "  Max clients: %u",
                this->max_clients_);
}

BLEServer *global_ble_server = nullptr;  // NOLINT(cppcoreguidelines-avoid-non-const-global-variables)

}  // namespace esphome::esp32_ble_server

#endif
