#pragma once

#include "esphome/core/component.h"
#include "esphome/core/helpers.h"
#include "esphome/components/light/light_output.h"
#include "esphome/components/light/light_state.h"

#include <driver/rmt_tx.h>
#include <vector>

namespace esphome::spatial_light {

// Motif spatial : définit dans quel ordre les LEDs s'allument / s'éteignent.
enum Pattern : uint8_t {
  PATTERN_CENTER_OUT = 0,  // du centre vers les bords
  PATTERN_EDGES_IN,        // des bords vers le centre
  PATTERN_LEFT_TO_RIGHT,
  PATTERN_RIGHT_TO_LEFT,
  PATTERN_FADE,            // fondu global, toutes les LEDs ensemble
  PATTERN_RANDOM,          // ordre aléatoire
  PATTERN_INSTANT,
  PATTERN_COUNT,
};

// Les 3 "couleurs" physiques d'une LED (blanc chaud, ambre, blanc froid).
enum Channel : uint8_t { CHANNEL_AMBER = 0, CHANNEL_WARM, CHANNEL_COLD };

// Paramètres réglables à chaud (numbers Home Assistant).
enum Param : uint8_t {
  PARAM_NUM_LEDS = 0,
  PARAM_ON_DURATION,   // ms
  PARAM_OFF_DURATION,  // ms
  PARAM_TRANSITION,    // ms (changement luminosité / température)
  PARAM_FADE_WIDTH,    // % de la longueur du ruban sur laquelle le front est adouci
};

class SpatialLightOutput : public light::LightOutput, public Component {
 public:
  void setup() override;
  void loop() override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::HARDWARE; }

  light::LightTraits get_traits() override;
  void setup_state(light::LightState *state) override { this->state_ = state; }
  void write_state(light::LightState *state) override;

  // Configuration (YAML)
  void set_pin(uint8_t pin) { this->pin_ = pin; }
  void set_max_leds(uint16_t n) { this->max_leds_ = n; }
  void set_rmt_symbols(uint32_t n) { this->rmt_symbols_ = n; }
  void set_channel_order(uint8_t c0, uint8_t c1, uint8_t c2) {
    this->channel_order_[0] = c0;
    this->channel_order_[1] = c1;
    this->channel_order_[2] = c2;
  }
  void set_temperatures(float amber_k, float warm_k, float cold_k) {
    this->amber_k_ = amber_k;
    this->warm_k_ = warm_k;
    this->cold_k_ = cold_k;
  }
  void set_gamma(float gamma) { this->gamma_ = gamma; }

  // Réglages modifiables à chaud
  void set_param(Param param, float value);
  float get_param(Param param) const;
  void set_pattern(bool on, Pattern pattern);
  Pattern get_pattern(bool on) const { return on ? this->on_pattern_ : this->off_pattern_; }

 protected:
  enum Anim : uint8_t { ANIM_NONE, ANIM_ON, ANIM_OFF };

  void start_animation_(Anim anim);
  void compute_order_(Pattern pattern);
  bool step_(uint32_t now);
  void render_();
  void show_();

  light::LightState *state_{nullptr};
  HighFrequencyLoopRequester high_freq_;

  // Matériel
  uint8_t pin_{2};
  uint16_t max_leds_{150};
  uint32_t rmt_symbols_{48};
  uint8_t channel_order_[3]{CHANNEL_COLD, CHANNEL_AMBER, CHANNEL_WARM};
  rmt_channel_handle_t channel_{nullptr};
  rmt_encoder_handle_t encoder_{nullptr};
  uint8_t *buf_{nullptr};
  uint8_t *tx_buf_{nullptr};
  uint32_t last_show_{0};

  // Rendu couleur
  float amber_k_{2000.0f};
  float warm_k_{3200.0f};
  float cold_k_{6000.0f};
  float gamma_{2.2f};
  std::vector<uint16_t> gamma_lut_;  // 0..1023 -> 0..65535

  // Paramètres
  uint16_t num_leds_{97};
  uint32_t on_duration_{1500};
  uint32_t off_duration_{1500};
  uint32_t transition_{500};
  float fade_width_{0.3f};
  Pattern on_pattern_{PATTERN_CENTER_OUT};
  Pattern off_pattern_{PATTERN_CENTER_OUT};

  // Animation spatiale (masque 0..1 par LED)
  std::vector<float> level_;
  std::vector<float> start_level_;
  std::vector<float> order_;
  Anim anim_{ANIM_NONE};
  Pattern anim_pattern_{PATTERN_CENTER_OUT};
  uint32_t anim_start_{0};
  uint32_t anim_duration_{0};

  // Transition luminosité / température
  bool is_on_{false};
  bool transitioning_{false};
  float bright_{1.0f}, bright_from_{1.0f}, bright_to_{1.0f};
  float kelvin_{3000.0f}, kelvin_from_{3000.0f}, kelvin_to_{3000.0f};
  uint32_t trans_start_{0};

  bool dirty_{true};
};

}  // namespace esphome::spatial_light
