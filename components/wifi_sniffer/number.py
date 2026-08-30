import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import number
from . import wifi_sniffer_ns

SnifferNumber = wifi_sniffer_ns.class_("SnifferNumber", number.Number)

CONFIG_SCHEMA = number.NUMBER_SCHEMA.extend({
    cv.GenerateID(): cv.declare_id(SnifferNumber),
})

async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await number.register_number(var, config, min_value=1, max_value=14, step=1)
