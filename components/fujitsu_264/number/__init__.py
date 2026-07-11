import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import number

from ..climate import Fujitsu264Climate, fujitsu_264_ns

CONF_FUJITSU_264_ID = "fujitsu_264_id"
CONF_TEMP_AUTO_OFFSET = "temp_auto_offset"

TempAutoOffsetNumber = fujitsu_264_ns.class_(
    "TempAutoOffsetNumber", number.Number, cg.Component
)

CONFIG_SCHEMA = {
    cv.GenerateID(CONF_FUJITSU_264_ID): cv.use_id(Fujitsu264Climate),
    cv.Optional(CONF_TEMP_AUTO_OFFSET): number.number_schema(
        TempAutoOffsetNumber,
        unit_of_measurement="°C",
        icon="mdi:thermometer-auto",
    ).extend(cv.COMPONENT_SCHEMA),
}


async def to_code(config):
    parent_id = config[CONF_FUJITSU_264_ID]

    if temp_auto_offset_config := config.get(CONF_TEMP_AUTO_OFFSET):
        n = await number.new_number(
            temp_auto_offset_config, min_value=-2.0, max_value=2.0, step=0.5
        )
        await cg.register_component(n, temp_auto_offset_config)
        await cg.register_parented(n, parent_id)
        parent = await cg.get_variable(parent_id)
        cg.add(parent.set_temp_auto_offset_number(n))
