import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import switch

from ..climate import Fujitsu264Climate, fujitsu_264_ns

CONF_FUJITSU_264_ID = "fujitsu_264_id"
CONF_INTERNAL_CLEAN = "internal_clean"

InternalCleanSwitch = fujitsu_264_ns.class_(
    "InternalCleanSwitch", switch.Switch, cg.Component
)

CONFIG_SCHEMA = {
    cv.GenerateID(CONF_FUJITSU_264_ID): cv.use_id(Fujitsu264Climate),
    cv.Optional(CONF_INTERNAL_CLEAN): switch.switch_schema(
        InternalCleanSwitch,
        icon="mdi:spray-bottle",
        default_restore_mode="RESTORE_DEFAULT_ON",
    ).extend(cv.COMPONENT_SCHEMA),
}


async def to_code(config):
    parent_id = config[CONF_FUJITSU_264_ID]

    if internal_clean_config := config.get(CONF_INTERNAL_CLEAN):
        s = await switch.new_switch(internal_clean_config)
        await cg.register_component(s, internal_clean_config)
        await cg.register_parented(s, parent_id)
        parent = await cg.get_variable(parent_id)
        cg.add(parent.set_internal_clean_switch(s))
