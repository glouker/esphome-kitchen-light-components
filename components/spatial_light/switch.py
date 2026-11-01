import esphome.codegen as cg
from esphome.components import light, switch
import esphome.config_validation as cv
from esphome.const import CONF_LIGHT_ID

from . import CONF_NIGHT_LIGHT, spatial_light_ns
from .number import get_output

SpatialLightNightSwitch = spatial_light_ns.class_(
    "SpatialLightNightSwitch", switch.Switch, cg.Component
)

CONFIG_SCHEMA = cv.Schema(
    {
        cv.Required(CONF_LIGHT_ID): cv.use_id(light.LightState),
        cv.Optional(CONF_NIGHT_LIGHT): switch.switch_schema(
            SpatialLightNightSwitch,
            icon="mdi:weather-night",
            default_restore_mode="ALWAYS_OFF",
        ).extend(cv.COMPONENT_SCHEMA),
    }
)


async def to_code(config):
    state = await cg.get_variable(config[CONF_LIGHT_ID])
    if conf := config.get(CONF_NIGHT_LIGHT):
        var = await switch.new_switch(conf)
        await cg.register_component(var, conf)
        cg.add(var.set_parent(get_output(state)))
