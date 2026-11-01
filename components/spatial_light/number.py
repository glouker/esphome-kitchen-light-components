import esphome.codegen as cg
from esphome.components import light, number
import esphome.config_validation as cv
from esphome.const import (
    CONF_LIGHT_ID,
    CONF_NUM_LEDS,
    ENTITY_CATEGORY_CONFIG,
    UNIT_MILLISECOND,
    UNIT_PERCENT,
)

from . import (
    CONF_FADE_WIDTH,
    CONF_NIGHT_BRIGHTNESS,
    CONF_NIGHT_WIDTH,
    CONF_OFF_DURATION,
    CONF_ON_DURATION,
    CONF_TRANSITION,
    CONF_ZONE_END,
    CONF_ZONE_START,
    Param,
    spatial_light_ns,
)


SpatialLightNumber = spatial_light_ns.class_(
    "SpatialLightNumber", number.Number, cg.Component
)

# clé YAML -> (paramètre C++, min, max, pas, unité, icône)
NUMBERS = {
    CONF_NUM_LEDS: (Param.PARAM_NUM_LEDS, 1, 1000, 1, None, "mdi:led-strip-variant"),
    CONF_ON_DURATION: (Param.PARAM_ON_DURATION, 0, 10000, 50, UNIT_MILLISECOND, "mdi:timer-play-outline"),
    CONF_OFF_DURATION: (Param.PARAM_OFF_DURATION, 0, 10000, 50, UNIT_MILLISECOND, "mdi:timer-stop-outline"),
    CONF_TRANSITION: (Param.PARAM_TRANSITION, 0, 5000, 50, UNIT_MILLISECOND, "mdi:transition"),
    CONF_FADE_WIDTH: (Param.PARAM_FADE_WIDTH, 1, 100, 1, UNIT_PERCENT, "mdi:gradient-horizontal"),
    CONF_ZONE_START: (Param.PARAM_ZONE_START, 0, 99, 1, UNIT_PERCENT, "mdi:ray-start"),
    CONF_ZONE_END: (Param.PARAM_ZONE_END, 1, 100, 1, UNIT_PERCENT, "mdi:ray-end"),
    CONF_NIGHT_BRIGHTNESS: (Param.PARAM_NIGHT_BRIGHTNESS, 1, 50, 1, UNIT_PERCENT, "mdi:weather-night"),
    CONF_NIGHT_WIDTH: (Param.PARAM_NIGHT_WIDTH, 5, 100, 5, UNIT_PERCENT, "mdi:arrow-expand-horizontal"),
}


def _number_schema(unit, icon):
    kwargs = {"entity_category": ENTITY_CATEGORY_CONFIG, "icon": icon}
    if unit is not None:
        kwargs["unit_of_measurement"] = unit
    return number.number_schema(SpatialLightNumber, **kwargs).extend(
        cv.COMPONENT_SCHEMA
    )


CONFIG_SCHEMA = cv.Schema(
    {
        cv.Required(CONF_LIGHT_ID): cv.use_id(light.LightState),
        **{
            cv.Optional(key): _number_schema(unit, icon)
            for key, (_, _, _, _, unit, icon) in NUMBERS.items()
        },
    }
)


def get_output(state):
    return cg.RawExpression(
        f"static_cast<spatial_light::SpatialLightOutput *>({state}->get_output())"
    )


async def to_code(config):
    state = await cg.get_variable(config[CONF_LIGHT_ID])
    for key, (param, min_value, max_value, step, _, _) in NUMBERS.items():
        if key not in config:
            continue
        conf = config[key]
        var = await number.new_number(
            conf, min_value=min_value, max_value=max_value, step=step
        )
        await cg.register_component(var, conf)
        cg.add(var.set_parent(get_output(state)))
        cg.add(var.set_param(param))
