import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import select

from ..climate import MitsubishiClimate, mitsubishi_ns

CONF_MITSUBISHI_ID = "mitsubishi_id"
CONF_DRY_LEVEL = "dry_level"
CONF_VERTICAL_VANE = "vertical_vane"

DryLevelSelect = mitsubishi_ns.class_("DryLevelSelect", select.Select, cg.Component)
VerticalVaneSelect = mitsubishi_ns.class_(
    "VerticalVaneSelect", select.Select, cg.Component
)

CONFIG_SCHEMA = {
    cv.GenerateID(CONF_MITSUBISHI_ID): cv.use_id(MitsubishiClimate),
    cv.Optional(CONF_DRY_LEVEL): select.select_schema(
        DryLevelSelect, icon="mdi:water-percent"
    ).extend(cv.COMPONENT_SCHEMA),
    cv.Optional(CONF_VERTICAL_VANE): select.select_schema(
        VerticalVaneSelect, icon="mdi:pan-vertical"
    ).extend(cv.COMPONENT_SCHEMA),
}


async def to_code(config):
    parent_id = config[CONF_MITSUBISHI_ID]

    if dry_level_config := config.get(CONF_DRY_LEVEL):
        s = await select.new_select(dry_level_config, options=["弱", "標準", "強"])
        await cg.register_component(s, dry_level_config)
        await cg.register_parented(s, parent_id)
        parent = await cg.get_variable(parent_id)
        cg.add(parent.set_dry_level_select(s))

    if vertical_vane_config := config.get(CONF_VERTICAL_VANE):
        s = await select.new_select(
            vertical_vane_config, options=["自動", "1", "2", "3", "4", "5"]
        )
        await cg.register_component(s, vertical_vane_config)
        await cg.register_parented(s, parent_id)
        parent = await cg.get_variable(parent_id)
        cg.add(parent.set_vertical_vane_select(s))
