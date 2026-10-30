import esphome.codegen as cg
from esphome.components import light

CODEOWNERS = ["@glouker"]
AUTO_LOAD = ["number", "select"]

spatial_light_ns = cg.esphome_ns.namespace("spatial_light")
SpatialLightOutput = spatial_light_ns.class_(
    "SpatialLightOutput", light.LightOutput, cg.Component
)

Pattern = spatial_light_ns.enum("Pattern")
PATTERNS = {
    "center_out": Pattern.PATTERN_CENTER_OUT,
    "edges_in": Pattern.PATTERN_EDGES_IN,
    "left_to_right": Pattern.PATTERN_LEFT_TO_RIGHT,
    "right_to_left": Pattern.PATTERN_RIGHT_TO_LEFT,
    "fade": Pattern.PATTERN_FADE,
    "random": Pattern.PATTERN_RANDOM,
    "instant": Pattern.PATTERN_INSTANT,
}
# Libellés affichés dans Home Assistant (même ordre que l'enum C++)
PATTERN_LABELS = [
    "Centre → bords",
    "Bords → centre",
    "Gauche → droite",
    "Droite → gauche",
    "Fondu global",
    "Aléatoire",
    "Instantané",
]

Param = spatial_light_ns.enum("Param")

CONF_SPATIAL_LIGHT_ID = "spatial_light_id"
CONF_MAX_LEDS = "max_leds"
CONF_CHANNEL_ORDER = "channel_order"
CONF_ON_EFFECT = "on_effect"
CONF_OFF_EFFECT = "off_effect"
CONF_ON_DURATION = "on_duration"
CONF_OFF_DURATION = "off_duration"
CONF_TRANSITION = "transition"
CONF_FADE_WIDTH = "fade_width"
CONF_AMBER_TEMPERATURE = "amber_temperature"
CONF_WARM_WHITE_TEMPERATURE = "warm_white_temperature"
CONF_COLD_WHITE_TEMPERATURE = "cold_white_temperature"
