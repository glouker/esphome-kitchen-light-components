import esphome.codegen as cg
from esphome.components import binary_sensor
import esphome.config_validation as cv

from . import CONF_MPR121_BAR_ID, MPR121Bar

CONF_TOUCHED = "touched"

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(CONF_MPR121_BAR_ID): cv.use_id(MPR121Bar),
        cv.Optional(CONF_TOUCHED): binary_sensor.binary_sensor_schema(
            icon="mdi:gesture-tap"
        ),
    }
)


async def to_code(config):
    parent = await cg.get_variable(config[CONF_MPR121_BAR_ID])
    if conf := config.get(CONF_TOUCHED):
        sens = await binary_sensor.new_binary_sensor(conf)
        cg.add(parent.set_touched_binary_sensor(sens))
