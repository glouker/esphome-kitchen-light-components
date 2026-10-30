import esphome.codegen as cg
from esphome.components import sensor
import esphome.config_validation as cv
from esphome.const import ENTITY_CATEGORY_DIAGNOSTIC, STATE_CLASS_MEASUREMENT

from . import CONF_MPR121_BAR_ID, MPR121Bar

CONF_DELTA = "delta"

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(CONF_MPR121_BAR_ID): cv.use_id(MPR121Bar),
        cv.Optional(CONF_DELTA): sensor.sensor_schema(
            icon="mdi:delta",
            accuracy_decimals=0,
            state_class=STATE_CLASS_MEASUREMENT,
            entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
        ),
    }
)


async def to_code(config):
    parent = await cg.get_variable(config[CONF_MPR121_BAR_ID])
    if conf := config.get(CONF_DELTA):
        sens = await sensor.new_sensor(conf)
        cg.add(parent.set_delta_sensor(sens))
