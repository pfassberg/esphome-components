import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.const import CONF_ID

# Define the namespace matching your C++ file
wifi_sniffer_ns = cg.esphome_ns.namespace('wifi_sniffer')
WifiSniffer = wifi_sniffer_ns.class_('WifiSniffer', cg.Component)

CONFIG_SCHEMA = cv.Schema({
    cv.GenerateID(): cv.declare_id(WifiSniffer),
}).extend(cv.COMPONENT_SCHEMA)

async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    
    # Correct way: Inject native ESP-IDF component linking flags
    cg.add_build_flag("-lesp_wifi")
