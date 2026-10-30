# esphome-kitchen-light-components

Composants externes ESPHome (ESP32, framework ESP-IDF) :

- **`spatial_light`** : ruban LED 3 canaux blancs (ambre / blanc chaud / blanc froid, puces WS2812)
  piloté en température de couleur, avec animations spatiales d'allumage et d'extinction.
  Nombre de LEDs, effets, durées et douceur sont réglables à chaud depuis Home Assistant.
- **`mpr121_bar`** : détection de tap sur une barre métallique reliée à un MPR121
  (baseline logicielle EMA, confirmation, hystérésis, anti-latch). Sensibilités réglables depuis HA.

## Installation

```yaml
external_components:
  - source: github://glouker/esphome-kitchen-light-components@main
```

## spatial_light

```yaml
light:
  - platform: spatial_light
    id: strip
    name: None
    pin: GPIO2
    max_leds: 200                      # taille du buffer
    num_leds: 97                       # défaut, réglable via number
    channel_order: [cold, amber, warm] # ordre des octets sur le bus
    amber_temperature: 2000K
    warm_white_temperature: 3200K
    cold_white_temperature: 6000K
    gamma_correct: 2.0
    on_effect: center_out              # center_out | edges_in | left_to_right | right_to_left | fade | random | instant
    off_effect: center_out
    on_duration: 1500ms
    off_duration: 1500ms
    transition: 500ms                  # changement de luminosité / température
    fade_width: 30%                    # largeur du front de l'animation

select:
  - platform: spatial_light
    light_id: strip
    on_effect: { name: Effet allumage }
    off_effect: { name: Effet extinction }

number:
  - platform: spatial_light
    light_id: strip
    num_leds: { name: Nombre de LEDs }
    on_duration: { name: Durée allumage }
    off_duration: { name: Durée extinction }
    transition: { name: Durée transition }
    fade_width: { name: Douceur animation }
```

Les valeurs modifiées dans HA sont persistées en flash et prennent le pas sur le YAML.
Les transitions d'ESPHome sont désactivées (`default_transition_length: 0s`) : le composant
gère lui-même animations et transitions, le paramètre `transition` envoyé par HA est ignoré.

## mpr121_bar

```yaml
i2c:
  sda: GPIO6
  scl: GPIO7

mpr121_bar:
  id: touch
  address: 0x5A
  channel: 0
  threshold: 25          # delta capacitif minimal
  hysteresis: 50%        # relâché quand delta < seuil × hystérésis
  cooldown: 300ms        # délai minimal entre deux taps
  confirm_samples: 3     # échantillons consécutifs (50 Hz) au-dessus du seuil
  max_tap_duration: 1s   # au-delà, pas de tap
  latch_timeout: 2s      # recalibration si contact bloqué
  on_tap:
    - light.toggle: strip

number:
  - platform: mpr121_bar
    threshold: { name: Tactile seuil }
    hysteresis: { name: Tactile hystérésis }
    cooldown: { name: Tactile anti-rebond }
    confirm_samples: { name: Tactile confirmations }
    max_tap_duration: { name: Tactile durée max appui }

sensor:
  - platform: mpr121_bar
    delta: { name: Tactile delta }       # utile pour régler le seuil

binary_sensor:
  - platform: mpr121_bar
    touched: { name: Tactile contact }
```
