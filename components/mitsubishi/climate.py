import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import climate_ir
from esphome.components import ir_remote_base
from esphome.const import CONF_MODEL

CONF_SUPPORTS_AUTO = "supports_auto"
CONF_SUPPORTS_FAN_ONLY = "supports_fan_only"
CONF_HORIZONTAL_SWING = "horizontal_swing"

AUTO_LOAD = ["climate_ir", "ir_remote_base"]

mitsubishi_ns = cg.esphome_ns.namespace("mitsubishi")
MitsubishiClimate = mitsubishi_ns.class_("MitsubishiClimate", climate_ir.ClimateIR)

Model = mitsubishi_ns.enum("Model")
MODELS = {
    "MITSUBISHI_AC": Model.MITSUBISHI_AC,
    "MITSUBISHI136": Model.MITSUBISHI136,
    "MITSUBISHI112": Model.MITSUBISHI112,
}

CONFIG_SCHEMA = climate_ir.climate_ir_with_receiver_schema(MitsubishiClimate).extend(
    {
        cv.Required(CONF_MODEL): cv.enum(MODELS),
        # Some physical units don't support everything Model::MITSUBISHI_AC
        # otherwise exposes (e.g. no auto/fan-only mode, no horizontal swing).
        # These only ever narrow support, never widen it beyond the model's
        # own default.
        cv.Optional(CONF_SUPPORTS_AUTO): cv.boolean,
        cv.Optional(CONF_SUPPORTS_FAN_ONLY): cv.boolean,
        cv.Optional(CONF_HORIZONTAL_SWING): cv.boolean,
    }
)

async def to_code(config):
    ir_remote_base.load_ir_remote()

    var = await climate_ir.new_climate_ir(config)
    cg.add(var.set_model(config[CONF_MODEL]))
    if CONF_SUPPORTS_AUTO in config:
        cg.add(var.set_supports_auto(config[CONF_SUPPORTS_AUTO]))
    if CONF_SUPPORTS_FAN_ONLY in config:
        cg.add(var.set_supports_fan_only(config[CONF_SUPPORTS_FAN_ONLY]))
    if CONF_HORIZONTAL_SWING in config:
        cg.add(var.set_horizontal_swing_supported(config[CONF_HORIZONTAL_SWING]))
