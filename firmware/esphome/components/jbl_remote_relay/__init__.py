import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import ble_client, esp32_ble, esp32_ble_server, event, text_sensor
from esphome.const import CONF_ID, CONF_MAC_ADDRESS
import esphome.final_validate as fv

DEPENDENCIES = ["ble_client", "esp32_ble_server"]
AUTO_LOAD = ["event", "text_sensor"]
ns = cg.esphome_ns.namespace("jbl_remote_relay")
JBLRemoteRelay = ns.class_("JBLRemoteRelay", cg.Component, ble_client.BLEClientNode)
REPORTS = {"keyboard_input_id": 1, "consumer_input_id": 3, "vendor_input_id": 30}

CONFIG_SCHEMA = cv.Schema({
    cv.GenerateID(): cv.declare_id(JBLRemoteRelay),
    **{cv.Required(key): cv.use_id(esp32_ble_server.BLECharacteristic) for key in REPORTS},
    cv.Optional("received_event_id"): cv.use_id(event.Event),
    cv.Optional("last_key_text_sensor_id"): cv.use_id(text_sensor.TextSensor),
    cv.Optional("sent_event_id"): cv.use_id(event.Event),
    cv.Optional("last_send_text_sensor_id"): cv.use_id(text_sensor.TextSensor),
}).extend(cv.COMPONENT_SCHEMA).extend(ble_client.BLE_CLIENT_SCHEMA)


def final_validate(config):
    full_config = fv.full_config.get()
    client = next(client for client in full_config["ble_client"]
                  if client[CONF_ID] == config[ble_client.CONF_BLE_CLIENT_ID])
    remote_mac = esp32_ble.validate_target_mac(client[CONF_MAC_ADDRESS])
    if remote_mac.parts == full_config["esp32_ble_server"]["speaker_mac"].parts:
        raise cv.Invalid("Remote and speaker must use different public Bluetooth MACs")
    return config


FINAL_VALIDATE_SCHEMA = final_validate


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    await ble_client.register_ble_node(var, config)
    for key, report_id in REPORTS.items():
        characteristic = await cg.get_variable(config[key])
        cg.add(var.set_output(report_id, characteristic))
    if "received_event_id" in config:
        cg.add(var.set_received_event(await cg.get_variable(config["received_event_id"])))
    if "last_key_text_sensor_id" in config:
        cg.add(var.set_last_key_text_sensor(await cg.get_variable(config["last_key_text_sensor_id"])))
    if "sent_event_id" in config:
        cg.add(var.set_sent_event(await cg.get_variable(config["sent_event_id"])))
    if "last_send_text_sensor_id" in config:
        cg.add(var.set_last_send_text_sensor(await cg.get_variable(config["last_send_text_sensor_id"])))
