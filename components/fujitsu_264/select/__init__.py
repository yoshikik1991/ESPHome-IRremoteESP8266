import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import select

from ..climate import Fujitsu264Climate, fujitsu_264_ns

CONF_FUJITSU_264_ID = "fujitsu_264_id"
CONF_WEAK_DRY = "weak_dry"
CONF_VERTICAL_ANGLE = "vertical_angle"
CONF_HORIZONTAL_ANGLE = "horizontal_angle"

WeakDrySelect = fujitsu_264_ns.class_("WeakDrySelect", select.Select, cg.Component)
VerticalAngleSelect = fujitsu_264_ns.class_(
    "VerticalAngleSelect", select.Select, cg.Component
)
HorizontalAngleSelect = fujitsu_264_ns.class_(
    "HorizontalAngleSelect", select.Select, cg.Component
)

CONFIG_SCHEMA = {
    cv.GenerateID(CONF_FUJITSU_264_ID): cv.use_id(Fujitsu264Climate),
    cv.Optional(CONF_WEAK_DRY): select.select_schema(
        WeakDrySelect, icon="mdi:water-percent"
    ).extend(cv.COMPONENT_SCHEMA),
    cv.Optional(CONF_VERTICAL_ANGLE): select.select_schema(
        VerticalAngleSelect, icon="mdi:pan-vertical"
    ).extend(cv.COMPONENT_SCHEMA),
    cv.Optional(CONF_HORIZONTAL_ANGLE): select.select_schema(
        HorizontalAngleSelect, icon="mdi:pan-horizontal"
    ).extend(cv.COMPONENT_SCHEMA),
}


async def to_code(config):
    parent_id = config[CONF_FUJITSU_264_ID]

    if weak_dry_config := config.get(CONF_WEAK_DRY):
        s = await select.new_select(weak_dry_config, options=["通常", "ひかえめ"])
        await cg.register_component(s, weak_dry_config)
        await cg.register_parented(s, parent_id)
        parent = await cg.get_variable(parent_id)
        cg.add(parent.set_weak_dry_select(s))

    if vertical_angle_config := config.get(CONF_VERTICAL_ANGLE):
        s = await select.new_select(
            vertical_angle_config,
            options=["1", "2", "3", "4", "5", "6", "7", "8"],
        )
        await cg.register_component(s, vertical_angle_config)
        await cg.register_parented(s, parent_id)
        parent = await cg.get_variable(parent_id)
        cg.add(parent.set_vertical_angle_select(s))

    if horizontal_angle_config := config.get(CONF_HORIZONTAL_ANGLE):
        s = await select.new_select(
            horizontal_angle_config,
            options=["1", "2", "3", "4", "5"],
        )
        await cg.register_component(s, horizontal_angle_config)
        await cg.register_parented(s, parent_id)
        parent = await cg.get_variable(parent_id)
        cg.add(parent.set_horizontal_angle_select(s))
