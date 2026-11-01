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
    refresh_interval: 1s               # renvoi périodique au repos (0s = désactivé)
    # center_out | edges_in | left_to_right | right_to_left | fade | random | instant
    # blocks | rain | comet | double_comet | elastic | sun | neon
    on_effect: center_out
    off_effect: center_out
    on_duration: 1500ms
    off_duration: 1500ms
    transition: 500ms                  # changement de luminosité / température
    fade_width: 30%                    # largeur du front de l'animation
    zone_start: 0%                     # zone allumée
    zone_end: 100%
    night_brightness: 5%               # veilleuse : segment central en ambre
    night_width: 20%
    timer_brightness: 40%              # minuteur lampe éteinte
    on_timer_finished:
      - logger.log: "Minuteur terminé"
    effects:
      - spatial_light.twinkle:         # étoiles scintillantes sur fond tamisé
          name: Étoiles
          base_brightness: 30%
          stars_per_second: 8          # pour 100 LEDs
          star_duration: 1200ms
          star_temperature: 6000K

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
    zone_start: { name: Zone début }
    zone_end: { name: Zone fin }
    night_brightness: { name: Veilleuse luminosité }
    night_width: { name: Veilleuse largeur }

switch:
  - platform: spatial_light
    light_id: strip
    night_light: { name: Veilleuse }   # affichée uniquement lumière éteinte
```

### Effets d'allumage / extinction

| Clé | Effet |
|---|---|
| `center_out` / `edges_in` | du centre vers les bords / l'inverse |
| `left_to_right` / `right_to_left` | balayage |
| `fade` | fondu global |
| `random` | LED par LED, ordre aléatoire |
| `blocks` | par segments successifs |
| `rain` | par petits groupes aléatoires |
| `comet` / `double_comet` | tête lumineuse avec traînée (depuis la gauche / les deux bords) |
| `elastic` | depuis le centre avec léger rebond |
| `sun` | lever depuis l'ambre / coucher vers l'ambre |
| `neon` | clignotements façon tube fluorescent |
| `instant` | sans animation |

### Actions

```yaml
- spatial_light.start_timer:     # la barre rétrécit puis clignote à la fin
    id: strip
    duration: 10min
- spatial_light.cancel_timer:
    id: strip
- spatial_light.notify:          # pulsation brève
    id: strip
    count: 2
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
  max_tap_duration: 1s   # au-delà, appui long
  latch_timeout: 2s      # recalibration si contact bloqué
  on_tap:
    - light.toggle: strip
  on_long_press:         # contact maintenu plus de max_tap_duration
    - switch.toggle: night_light

number:
  - platform: mpr121_bar
    threshold: { name: Tactile seuil }
    hysteresis: { name: Tactile hystérésis }
    cooldown: { name: Tactile anti-rebond }
    confirm_samples: { name: Tactile confirmations }
    max_tap_duration: { name: Tactile durée appui long }

sensor:
  - platform: mpr121_bar
    delta: { name: Tactile delta }       # utile pour régler le seuil

binary_sensor:
  - platform: mpr121_bar
    touched: { name: Tactile contact }
```
