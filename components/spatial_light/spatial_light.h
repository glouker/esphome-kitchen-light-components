#pragma once

#include "esphome/core/automation.h"
#include "esphome/core/component.h"
#include "esphome/core/helpers.h"
#include "esphome/components/light/light_effect.h"
#include "esphome/components/light/light_output.h"
#include "esphome/components/light/light_state.h"

#include <driver/rmt_tx.h>
#include <vector>

namespace esphome::spatial_light {

// Motif d'allumage / extinction. L'ordre est figé : l'index est persisté par les selects.
enum Pattern : uint8_t {
  PATTERN_CENTER_OUT = 0,  // du centre vers les bords
  PATTERN_EDGES_IN,        // des bords vers le centre
  PATTERN_LEFT_TO_RIGHT,
  PATTERN_RIGHT_TO_LEFT,
  PATTERN_FADE,            // fondu global, toutes les LEDs ensemble
  PATTERN_RANDOM,          // LED par LED dans un ordre aléatoire
  PATTERN_INSTANT,
  PATTERN_BLOCKS,          // par segments successifs
  PATTERN_RAIN,            // par petits groupes dans un ordre aléatoire
  PATTERN_COMET,           // une tête lumineuse avec traînée, de gauche à droite
  PATTERN_DOUBLE_COMET,    // deux têtes partant des bords
  PATTERN_ELASTIC,         // du centre, avec un léger dépassement de la cible
  PATTERN_SUN,             // lever (depuis l'ambre) / coucher (vers l'ambre)
  PATTERN_NEON,            // clignotements irréguliers façon tube fluorescent
  PATTERN_COUNT,
};

// Les 3 "couleurs" physiques d'une LED (blanc chaud, ambre, blanc froid).
enum Channel : uint8_t { CHANNEL_AMBER = 0, CHANNEL_WARM, CHANNEL_COLD };

// Paramètres réglables à chaud (numbers Home Assistant). Ordre figé.
enum Param : uint8_t {
  PARAM_NUM_LEDS = 0,
  PARAM_ON_DURATION,       // ms
  PARAM_OFF_DURATION,      // ms
  PARAM_TRANSITION,        // ms (changement luminosité / température)
  PARAM_FADE_WIDTH,        // % de la longueur sur laquelle le front est adouci
  PARAM_ZONE_START,        // % du ruban
  PARAM_ZONE_END,          // % du ruban
  PARAM_NIGHT_BRIGHTNESS,  // %
  PARAM_NIGHT_WIDTH,       // % de la zone
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
  void set_refresh_interval(uint32_t ms) { this->refresh_interval_ = ms; }
  void set_timer_brightness(float b) { this->timer_brightness_ = b; }

  // Réglages modifiables à chaud
  void set_param(Param param, float value);
  float get_param(Param param) const;
  void set_pattern(bool on, Pattern pattern);
  Pattern get_pattern(bool on) const { return on ? this->on_pattern_ : this->off_pattern_; }

  // Fonctions
  void start_timer(uint32_t duration_ms);
  void cancel_timer();
  bool is_timer_running() const { return this->timer_phase_ != TIMER_IDLE; }
  void notify(uint8_t pulses);
  void set_night_light(bool on);
  bool is_night_light() const { return this->night_; }
  // Effet "étoiles" : fond tamisé + scintillements. stars_per_s pour 100 LEDs.
  void set_twinkle(bool on, float base, float stars_per_s, uint32_t star_ms, float star_kelvin);

  template<typename F> void add_on_timer_finished_callback(F &&cb) {
    this->timer_finished_cb_.add(std::forward<F>(cb));
  }
  template<typename F> void add_on_night_light_callback(F &&cb) { this->night_cb_.add(std::forward<F>(cb)); }

 protected:
  enum Anim : uint8_t { ANIM_NONE, ANIM_ON, ANIM_OFF };
  enum TimerPhase : uint8_t { TIMER_IDLE, TIMER_RUNNING, TIMER_ALARM };

  void start_animation_(Anim anim);
  void compute_order_(Pattern pattern);
  float profile_(float p, float x, uint32_t elapsed) const;
  void update_zone_();
  void reset_levels_();
  bool is_active_() const;
  void step_(uint32_t now);
  void render_();
  void show_();
  void channel_scale_(float kelvin, uint32_t *scale) const;

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
  uint32_t refresh_interval_{1000};

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

  // Zone active du ruban : LEDs [zone_begin_, zone_end_)
  float zone_start_pct_{0.0f};
  float zone_end_pct_{100.0f};
  uint16_t zone_begin_{0};
  uint16_t zone_end_{97};

  // Animation spatiale (masque 0..1 par LED)
  std::vector<float> level_;
  std::vector<float> start_level_;
  std::vector<float> order_;
  Anim anim_{ANIM_NONE};
  Pattern anim_pattern_{PATTERN_CENTER_OUT};
  uint32_t anim_start_{0};
  uint32_t anim_duration_{0};
  float anim_kelvin_{0.0f};  // température imposée par l'effet soleil, 0 = aucune

  // Transition luminosité / température
  bool is_on_{false};
  bool transitioning_{false};
  float bright_{1.0f}, bright_from_{1.0f}, bright_to_{1.0f};
  float kelvin_{3000.0f}, kelvin_from_{3000.0f}, kelvin_to_{3000.0f};
  uint32_t trans_start_{0};

  // Veilleuse
  bool night_{false};
  float night_fade_{0.0f};
  float night_brightness_{0.05f};
  float night_width_{0.2f};

  // Minuteur
  TimerPhase timer_phase_{TIMER_IDLE};
  uint32_t timer_start_{0};
  uint32_t timer_duration_{0};
  float timer_remaining_{0.0f};
  float timer_brightness_{0.4f};
  float alarm_pulse_{0.0f};

  // Étoiles
  bool twinkle_{false};
  float twinkle_base_{0.3f};
  float twinkle_rate_{8.0f};
  uint32_t twinkle_star_ms_{1200};
  float twinkle_kelvin_{6000.0f};
  std::vector<float> star_;  // phase 0..2 (0 = éteinte, 0..1 montée, 1..2 descente)

  // Notification
  uint8_t notify_pulses_{0};
  uint32_t notify_start_{0};
  float notify_pulse_{0.0f};

  uint32_t last_step_{0};
  bool dirty_{true};

  CallbackManager<void()> timer_finished_cb_;
  CallbackManager<void(bool)> night_cb_;
};

// Effet de lumière HA : délègue au composant.
class TwinkleEffect : public light::LightEffect {
 public:
  explicit TwinkleEffect(const char *name) : LightEffect(name) {}
  void set_params(float base, float stars_per_s, uint32_t star_ms, float star_kelvin) {
    this->base_ = base;
    this->rate_ = stars_per_s;
    this->star_ms_ = star_ms;
    this->kelvin_ = star_kelvin;
  }
  void start() override { this->output_()->set_twinkle(true, this->base_, this->rate_, this->star_ms_, this->kelvin_); }
  void stop() override { this->output_()->set_twinkle(false, 0, 0, 0, 0); }
  void apply() override {}

 protected:
  SpatialLightOutput *output_() { return static_cast<SpatialLightOutput *>(this->state_->get_output()); }
  float base_{0.3f};
  float rate_{8.0f};
  uint32_t star_ms_{1200};
  float kelvin_{6000.0f};
};

class TimerFinishedTrigger : public Trigger<> {
 public:
  explicit TimerFinishedTrigger(SpatialLightOutput *parent) {
    parent->add_on_timer_finished_callback([this]() { this->trigger(); });
  }
};

}  // namespace esphome::spatial_light
