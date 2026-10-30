import esphome.codegen as cg
from esphome.components import light, select
import esphome.config_validation as cv
from esphome.const import CONF_LIGHT_ID, ENTITY_CATEGORY_CONFIG

from . import CONF_OFF_EFFECT, CONF_ON_EFFECT, PATTERN_LABELS, spatial_light_ns
from .number import get_output


SpatialLightSelect = spatial_light_ns.class_(
    "SpatialLightSelect", select.Select, cg.Component
)

_SELECT_SCHEMA = select.select_schema(
    SpatialLightSelect, entity_category=ENTITY_CATEGORY_CONFIG, icon="mdi:animation-play"
).extend(cv.COMPONENT_SCHEMA)

CONFIG_SCHEMA = cv.Schema(
    {
        cv.Required(CONF_LIGHT_ID): cv.use_id(light.LightState),
        cv.Optional(CONF_ON_EFFECT): _SELECT_SCHEMA,
        cv.Optional(CONF_OFF_EFFECT): _SELECT_SCHEMA,
    }
)


async def to_code(config):
    state = await cg.get_variable(config[CONF_LIGHT_ID])
    for key, on in ((CONF_ON_EFFECT, True), (CONF_OFF_EFFECT, False)):
        if key not in config:
            continue
        conf = config[key]
        var = await select.new_select(conf, options=PATTERN_LABELS)
        await cg.register_component(var, conf)
        cg.add(var.set_parent(get_output(state)))
        cg.add(var.set_on_effect(on))
