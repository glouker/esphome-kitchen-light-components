import esphome.codegen as cg
from esphome.components import number
import esphome.config_validation as cv
from esphome.const import (
    CONF_THRESHOLD,
    ENTITY_CATEGORY_CONFIG,
    UNIT_MILLISECOND,
    UNIT_PERCENT,
)

from . import (
    CONF_CONFIRM_SAMPLES,
    CONF_COOLDOWN,
    CONF_HYSTERESIS,
    CONF_MAX_TAP_DURATION,
    CONF_MPR121_BAR_ID,
    MPR121Bar,
    Param,
    mpr121_bar_ns,
)

MPR121BarNumber = mpr121_bar_ns.class_("MPR121BarNumber", number.Number, cg.Component)

# clé YAML -> (paramètre C++, min, max, pas, unité, icône)
NUMBERS = {
    CONF_THRESHOLD: (Param.PARAM_THRESHOLD, 5, 100, 1, None, "mdi:gesture-tap"),
    CONF_HYSTERESIS: (Param.PARAM_HYSTERESIS, 10, 90, 5, UNIT_PERCENT, "mdi:sine-wave"),
    CONF_COOLDOWN: (Param.PARAM_COOLDOWN, 100, 2000, 50, UNIT_MILLISECOND, "mdi:timer-sand"),
    CONF_CONFIRM_SAMPLES: (Param.PARAM_CONFIRM_SAMPLES, 1, 10, 1, None, "mdi:check-all"),
    CONF_MAX_TAP_DURATION: (Param.PARAM_MAX_TAP, 200, 3000, 50, UNIT_MILLISECOND, "mdi:timer-outline"),
}


def _number_schema(unit, icon):
    kwargs = {"entity_category": ENTITY_CATEGORY_CONFIG, "icon": icon}
    if unit is not None:
        kwargs["unit_of_measurement"] = unit
    return number.number_schema(MPR121BarNumber, **kwargs).extend(cv.COMPONENT_SCHEMA)


CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(CONF_MPR121_BAR_ID): cv.use_id(MPR121Bar),
        **{
            cv.Optional(key): _number_schema(unit, icon)
            for key, (_, _, _, _, unit, icon) in NUMBERS.items()
        },
    }
)


async def to_code(config):
    parent = await cg.get_variable(config[CONF_MPR121_BAR_ID])
    for key, (param, min_value, max_value, step, _, _) in NUMBERS.items():
        if key not in config:
            continue
        conf = config[key]
        var = await number.new_number(
            conf, min_value=min_value, max_value=max_value, step=step
        )
        await cg.register_component(var, conf)
        cg.add(var.set_parent(parent))
        cg.add(var.set_param(param))
