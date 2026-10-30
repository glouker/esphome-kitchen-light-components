from esphome import pins
import esphome.codegen as cg
from esphome.components import esp32, esp32_rmt, light
from esphome.components.esp32 import include_builtin_idf_component
import esphome.config_validation as cv
from esphome.const import (
    CONF_DEFAULT_TRANSITION_LENGTH,
    CONF_GAMMA_CORRECT,
    CONF_NUM_LEDS,
    CONF_NUMBER,
    CONF_OUTPUT_ID,
    CONF_PIN,
    CONF_RMT_SYMBOLS,
)

from . import (
    CONF_AMBER_TEMPERATURE,
    CONF_CHANNEL_ORDER,
    CONF_COLD_WHITE_TEMPERATURE,
    CONF_FADE_WIDTH,
    CONF_MAX_LEDS,
    CONF_OFF_DURATION,
    CONF_OFF_EFFECT,
    CONF_ON_DURATION,
    CONF_ON_EFFECT,
    CONF_TRANSITION,
    CONF_WARM_WHITE_TEMPERATURE,
    PATTERNS,
    Param,
    SpatialLightOutput,
    spatial_light_ns,
)

DEPENDENCIES = ["esp32"]

Channel = spatial_light_ns.enum("Channel")
CHANNELS = {
    "amber": Channel.CHANNEL_AMBER,
    "warm": Channel.CHANNEL_WARM,
    "cold": Channel.CHANNEL_COLD,
}


def _validate(config):
    if config[CONF_NUM_LEDS] > config[CONF_MAX_LEDS]:
        raise cv.Invalid(f"{CONF_NUM_LEDS} doit être <= {CONF_MAX_LEDS}")
    # cv.color_temperature renvoie des mireds : plus chaud = plus grand
    if not (
        config[CONF_AMBER_TEMPERATURE]
        > config[CONF_WARM_WHITE_TEMPERATURE]
        > config[CONF_COLD_WHITE_TEMPERATURE]
    ):
        raise cv.Invalid("Il faut amber < warm_white < cold_white")
    return config


CONFIG_SCHEMA = cv.All(
    esp32.only_on_variant(
        unsupported=list(esp32_rmt.VARIANTS_NO_RMT),
        msg_prefix="spatial_light",
    ),
    light.BRIGHTNESS_ONLY_LIGHT_SCHEMA.extend(
        {
            cv.GenerateID(CONF_OUTPUT_ID): cv.declare_id(SpatialLightOutput),
            cv.Required(CONF_PIN): pins.internal_gpio_output_pin_schema,
            cv.Optional(CONF_MAX_LEDS, default=150): cv.int_range(min=1, max=1000),
            cv.Optional(CONF_NUM_LEDS, default=97): cv.int_range(min=1, max=1000),
            cv.Optional(CONF_CHANNEL_ORDER, default=["cold", "amber", "warm"]): cv.All(
                cv.ensure_list(cv.enum(CHANNELS, lower=True)),
                cv.Length(min=3, max=3),
            ),
            cv.Optional(CONF_AMBER_TEMPERATURE, default="2000K"): cv.color_temperature,
            cv.Optional(
                CONF_WARM_WHITE_TEMPERATURE, default="3200K"
            ): cv.color_temperature,
            cv.Optional(
                CONF_COLD_WHITE_TEMPERATURE, default="6000K"
            ): cv.color_temperature,
            cv.Optional(CONF_ON_EFFECT, default="center_out"): cv.enum(
                PATTERNS, lower=True
            ),
            cv.Optional(CONF_OFF_EFFECT, default="center_out"): cv.enum(
                PATTERNS, lower=True
            ),
            cv.Optional(
                CONF_ON_DURATION, default="1500ms"
            ): cv.positive_time_period_milliseconds,
            cv.Optional(
                CONF_OFF_DURATION, default="1500ms"
            ): cv.positive_time_period_milliseconds,
            cv.Optional(
                CONF_TRANSITION, default="500ms"
            ): cv.positive_time_period_milliseconds,
            cv.Optional(CONF_FADE_WIDTH, default="30%"): cv.percentage,
            # Les transitions sont gérées par le composant : on neutralise celles d'ESPHome
            cv.Optional(
                CONF_DEFAULT_TRANSITION_LENGTH, default="0s"
            ): cv.positive_time_period_milliseconds,
            cv.Optional(CONF_GAMMA_CORRECT, default=2.0): cv.positive_float,
            cv.SplitDefault(
                CONF_RMT_SYMBOLS,
                esp32=192,
                esp32_c3=96,
                esp32_c5=96,
                esp32_c6=96,
                esp32_h2=96,
                esp32_s2=192,
                esp32_s3=192,
            ): cv.int_range(min=48),
        }
    ).extend(cv.COMPONENT_SCHEMA),
    _validate,
)


async def to_code(config):
    include_builtin_idf_component("esp_driver_rmt")

    var = cg.new_Pvariable(config[CONF_OUTPUT_ID])
    await cg.register_component(var, config)
    await light.register_light(var, config)

    cg.add(var.set_pin(config[CONF_PIN][CONF_NUMBER]))
    cg.add(var.set_max_leds(config[CONF_MAX_LEDS]))
    cg.add(var.set_rmt_symbols(config[CONF_RMT_SYMBOLS]))
    cg.add(var.set_channel_order(*config[CONF_CHANNEL_ORDER]))
    cg.add(
        var.set_temperatures(
            1e6 / config[CONF_AMBER_TEMPERATURE],
            1e6 / config[CONF_WARM_WHITE_TEMPERATURE],
            1e6 / config[CONF_COLD_WHITE_TEMPERATURE],
        )
    )
    cg.add(var.set_gamma(config[CONF_GAMMA_CORRECT]))
    cg.add(var.set_pattern(True, config[CONF_ON_EFFECT]))
    cg.add(var.set_pattern(False, config[CONF_OFF_EFFECT]))
    cg.add(var.set_param(Param.PARAM_NUM_LEDS, config[CONF_NUM_LEDS]))
    cg.add(
        var.set_param(Param.PARAM_ON_DURATION, config[CONF_ON_DURATION].total_milliseconds)
    )
    cg.add(
        var.set_param(
            Param.PARAM_OFF_DURATION, config[CONF_OFF_DURATION].total_milliseconds
        )
    )
    cg.add(
        var.set_param(Param.PARAM_TRANSITION, config[CONF_TRANSITION].total_milliseconds)
    )
    cg.add(var.set_param(Param.PARAM_FADE_WIDTH, config[CONF_FADE_WIDTH] * 100))
