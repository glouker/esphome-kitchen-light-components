from esphome import automation
import esphome.codegen as cg
from esphome.components import i2c
import esphome.config_validation as cv
from esphome.const import CONF_CHANNEL, CONF_ID, CONF_THRESHOLD, CONF_TRIGGER_ID

CODEOWNERS = ["@glouker"]
DEPENDENCIES = ["i2c"]
AUTO_LOAD = ["number", "sensor", "binary_sensor"]
MULTI_CONF = True

mpr121_bar_ns = cg.esphome_ns.namespace("mpr121_bar")
MPR121Bar = mpr121_bar_ns.class_("MPR121Bar", cg.Component, i2c.I2CDevice)
TapTrigger = mpr121_bar_ns.class_("TapTrigger", automation.Trigger.template())
Param = mpr121_bar_ns.enum("Param")

CONF_MPR121_BAR_ID = "mpr121_bar_id"
CONF_HYSTERESIS = "hysteresis"
CONF_COOLDOWN = "cooldown"
CONF_CONFIRM_SAMPLES = "confirm_samples"
CONF_MAX_TAP_DURATION = "max_tap_duration"
CONF_LATCH_TIMEOUT = "latch_timeout"
CONF_ON_TAP = "on_tap"

CONFIG_SCHEMA = (
    cv.Schema(
        {
            cv.GenerateID(): cv.declare_id(MPR121Bar),
            cv.Optional(CONF_CHANNEL, default=0): cv.int_range(min=0, max=11),
            cv.Optional(CONF_THRESHOLD, default=25): cv.int_range(min=1, max=255),
            cv.Optional(CONF_HYSTERESIS, default="50%"): cv.All(
                cv.percentage_int, cv.int_range(min=10, max=90)
            ),
            cv.Optional(
                CONF_COOLDOWN, default="300ms"
            ): cv.positive_time_period_milliseconds,
            cv.Optional(CONF_CONFIRM_SAMPLES, default=3): cv.int_range(min=1, max=20),
            cv.Optional(
                CONF_MAX_TAP_DURATION, default="1s"
            ): cv.positive_time_period_milliseconds,
            cv.Optional(
                CONF_LATCH_TIMEOUT, default="2s"
            ): cv.positive_time_period_milliseconds,
            cv.Optional(CONF_ON_TAP): automation.validate_automation(
                {cv.GenerateID(CONF_TRIGGER_ID): cv.declare_id(TapTrigger)}
            ),
        }
    )
    .extend(cv.COMPONENT_SCHEMA)
    .extend(i2c.i2c_device_schema(0x5A))
)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    await i2c.register_i2c_device(var, config)

    cg.add(var.set_channel(config[CONF_CHANNEL]))
    cg.add(var.set_latch_timeout(config[CONF_LATCH_TIMEOUT].total_milliseconds))
    cg.add(var.set_param(Param.PARAM_THRESHOLD, config[CONF_THRESHOLD]))
    cg.add(var.set_param(Param.PARAM_HYSTERESIS, config[CONF_HYSTERESIS]))
    cg.add(var.set_param(Param.PARAM_COOLDOWN, config[CONF_COOLDOWN].total_milliseconds))
    cg.add(var.set_param(Param.PARAM_CONFIRM_SAMPLES, config[CONF_CONFIRM_SAMPLES]))
    cg.add(
        var.set_param(Param.PARAM_MAX_TAP, config[CONF_MAX_TAP_DURATION].total_milliseconds)
    )

    for conf in config.get(CONF_ON_TAP, []):
        trigger = cg.new_Pvariable(conf[CONF_TRIGGER_ID], var)
        await automation.build_automation(trigger, [], conf)
