import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import switch

from ..climate import MitsubishiClimate, mitsubishi_ns

CONF_MITSUBISHI_ID = "mitsubishi_id"
CONF_POWERFUL = "powerful"
CONF_CURRENT_CUT = "current_cut"
CONF_INTERNAL_CLEAN = "internal_clean"

PowerfulSwitch = mitsubishi_ns.class_("PowerfulSwitch", switch.Switch)
CurrentCutSwitch = mitsubishi_ns.class_("CurrentCutSwitch", switch.Switch)
InternalCleanSwitch = mitsubishi_ns.class_(
    "InternalCleanSwitch", switch.Switch, cg.Component
)

CONFIG_SCHEMA = {
    cv.GenerateID(CONF_MITSUBISHI_ID): cv.use_id(MitsubishiClimate),
    cv.Optional(CONF_POWERFUL): switch.switch_schema(
        PowerfulSwitch, icon="mdi:rocket-launch"
    ),
    cv.Optional(CONF_CURRENT_CUT): switch.switch_schema(
        CurrentCutSwitch, icon="mdi:current-ac"
    ),
    cv.Optional(CONF_INTERNAL_CLEAN): switch.switch_schema(
        InternalCleanSwitch,
        icon="mdi:spray-bottle",
        default_restore_mode="RESTORE_DEFAULT_ON",
    ).extend(cv.COMPONENT_SCHEMA),
}


async def to_code(config):
    parent_id = config[CONF_MITSUBISHI_ID]

    if powerful_config := config.get(CONF_POWERFUL):
        s = await switch.new_switch(powerful_config)
        await cg.register_parented(s, parent_id)
        parent = await cg.get_variable(parent_id)
        cg.add(parent.set_powerful_switch(s))

    if current_cut_config := config.get(CONF_CURRENT_CUT):
        s = await switch.new_switch(current_cut_config)
        await cg.register_parented(s, parent_id)
        parent = await cg.get_variable(parent_id)
        cg.add(parent.set_current_cut_switch(s))

    if internal_clean_config := config.get(CONF_INTERNAL_CLEAN):
        s = await switch.new_switch(internal_clean_config)
        await cg.register_component(s, internal_clean_config)
        await cg.register_parented(s, parent_id)
        parent = await cg.get_variable(parent_id)
        cg.add(parent.set_internal_clean_switch(s))
