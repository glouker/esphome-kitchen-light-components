from esphome import automation, pins
import esphome.codegen as cg
from esphome.components import esp32, esp32_rmt, light
from esphome.components.esp32 import include_builtin_idf_component
import esphome.config_validation as cv
from esphome.components.light.effects import register_monochromatic_effect
from esphome.components.light.types import LightEffect
from esphome.const import (
    CONF_COUNT,
    CONF_DURATION,
    CONF_ID,
    CONF_NAME,
    CONF_TRIGGER_ID,
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
    CONF_NIGHT_BRIGHTNESS,
    CONF_NIGHT_WIDTH,
    CONF_OFF_DURATION,
    CONF_OFF_EFFECT,
    CONF_ON_DURATION,
    CONF_ON_EFFECT,
    CONF_ON_TIMER_FINISHED,
    CONF_REFRESH_INTERVAL,
    CONF_TIMER_BRIGHTNESS,
    CONF_TRANSITION,
    CONF_WARM_WHITE_TEMPERATURE,
    CONF_ZONE_END,
    CONF_ZONE_START,
    PATTERNS,
    Param,
    SpatialLightOutput,
    spatial_light_ns,
)

DEPENDENCIES = ["esp32"]

TimerFinishedTrigger = spatial_light_ns.class_(
    "TimerFinishedTrigger", automation.Trigger.template()
)
TwinkleEffect = spatial_light_ns.class_("TwinkleEffect", LightEffect)
StartTimerAction = spatial_light_ns.class_("StartTimerAction", automation.Action)
CancelTimerAction = spatial_light_ns.class_("CancelTimerAction", automation.Action)
NotifyAction = spatial_light_ns.class_("NotifyAction", automation.Action)

CONF_BASE_BRIGHTNESS = "base_brightness"
CONF_STARS_PER_SECOND = "stars_per_second"
CONF_STAR_DURATION = "star_duration"
CONF_STAR_TEMPERATURE = "star_temperature"

Channel = spatial_light_ns.enum("Channel")
CHANNELS = {
    "amber": Channel.CHANNEL_AMBER,
    "warm": Channel.CHANNEL_WARM,
    "cold": Channel.CHANNEL_COLD,
}


def _validate(config):
    if config[CONF_ZONE_START] >= config[CONF_ZONE_END]:
        raise cv.Invalid(f"{CONF_ZONE_START} doit être < {CONF_ZONE_END}")
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
            cv.Optional(CONF_ZONE_START, default="0%"): cv.percentage,
            cv.Optional(CONF_ZONE_END, default="100%"): cv.percentage,
            cv.Optional(CONF_NIGHT_BRIGHTNESS, default="5%"): cv.percentage,
            cv.Optional(CONF_NIGHT_WIDTH, default="20%"): cv.percentage,
            cv.Optional(CONF_TIMER_BRIGHTNESS, default="40%"): cv.percentage,
            cv.Optional(CONF_ON_TIMER_FINISHED): automation.validate_automation(
                {cv.GenerateID(CONF_TRIGGER_ID): cv.declare_id(TimerFinishedTrigger)}
            ),
            # Les transitions sont gérées par le composant : on neutralise celles d'ESPHome
            cv.Optional(
                CONF_DEFAULT_TRANSITION_LENGTH, default="0s"
            ): cv.positive_time_period_milliseconds,
            cv.Optional(CONF_GAMMA_CORRECT, default=2.0): cv.positive_float,
            # Renvoi périodique de la trame au repos (0 = désactivé)
            cv.Optional(
                CONF_REFRESH_INTERVAL, default="1s"
            ): cv.positive_time_period_milliseconds,
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
    cg.add(
        var.set_refresh_interval(config[CONF_REFRESH_INTERVAL].total_milliseconds)
    )
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
    cg.add(var.set_param(Param.PARAM_ZONE_START, config[CONF_ZONE_START] * 100))
    cg.add(var.set_param(Param.PARAM_ZONE_END, config[CONF_ZONE_END] * 100))
    cg.add(
        var.set_param(Param.PARAM_NIGHT_BRIGHTNESS, config[CONF_NIGHT_BRIGHTNESS] * 100)
    )
    cg.add(var.set_param(Param.PARAM_NIGHT_WIDTH, config[CONF_NIGHT_WIDTH] * 100))
    cg.add(var.set_timer_brightness(config[CONF_TIMER_BRIGHTNESS]))

    for conf in config.get(CONF_ON_TIMER_FINISHED, []):
        trigger = cg.new_Pvariable(conf[CONF_TRIGGER_ID], var)
        await automation.build_automation(trigger, [], conf)


# ---------------------------------------------------------------------------
# Effet "étoiles" (liste des effets de la lumière dans HA)
# ---------------------------------------------------------------------------


@register_monochromatic_effect(
    "spatial_light.twinkle",
    TwinkleEffect,
    "Étoiles",
    {
        cv.Optional(CONF_BASE_BRIGHTNESS, default="30%"): cv.percentage,
        cv.Optional(CONF_STARS_PER_SECOND, default=8): cv.positive_float,
        cv.Optional(
            CONF_STAR_DURATION, default="1200ms"
        ): cv.positive_time_period_milliseconds,
        cv.Optional(CONF_STAR_TEMPERATURE, default="6000K"): cv.color_temperature,
    },
)
async def twinkle_effect_to_code(config, effect_id):
    effect = cg.new_Pvariable(effect_id, config[CONF_NAME])
    cg.add(
        effect.set_params(
            config[CONF_BASE_BRIGHTNESS],
            config[CONF_STARS_PER_SECOND],
            config[CONF_STAR_DURATION].total_milliseconds,
            1e6 / config[CONF_STAR_TEMPERATURE],
        )
    )
    return effect


# ---------------------------------------------------------------------------
# Actions
# ---------------------------------------------------------------------------


def _output(state):
    return cg.RawExpression(
        f"static_cast<spatial_light::SpatialLightOutput *>({state}->get_output())"
    )


ACTION_SCHEMA = cv.Schema({cv.Required(CONF_ID): cv.use_id(light.LightState)})


@automation.register_action(
    "spatial_light.start_timer",
    StartTimerAction,
    ACTION_SCHEMA.extend(
        {
            cv.Required(CONF_DURATION): cv.templatable(
                cv.positive_time_period_milliseconds
            )
        }
    ),
    synchronous=True,
)
async def start_timer_to_code(config, action_id, template_arg, args):
    state = await cg.get_variable(config[CONF_ID])
    var = cg.new_Pvariable(action_id, template_arg, _output(state))
    duration = config[CONF_DURATION]
    if not cg.is_template(duration):
        duration = duration.total_milliseconds
    cg.add(var.set_duration(await cg.templatable(duration, args, cg.uint32)))
    return var


@automation.register_action(
    "spatial_light.cancel_timer", CancelTimerAction, ACTION_SCHEMA, synchronous=True
)
async def cancel_timer_to_code(config, action_id, template_arg, args):
    state = await cg.get_variable(config[CONF_ID])
    return cg.new_Pvariable(action_id, template_arg, _output(state))


@automation.register_action(
    "spatial_light.notify",
    NotifyAction,
    ACTION_SCHEMA.extend(
        {cv.Optional(CONF_COUNT, default=2): cv.templatable(cv.int_range(min=1, max=20))}
    ),
    synchronous=True,
)
async def notify_to_code(config, action_id, template_arg, args):
    state = await cg.get_variable(config[CONF_ID])
    var = cg.new_Pvariable(action_id, template_arg, _output(state))
    cg.add(var.set_count(await cg.templatable(config[CONF_COUNT], args, cg.uint8)))
    return var
