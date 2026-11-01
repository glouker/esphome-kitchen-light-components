#include "spatial_light.h"

#include "esphome/core/hal.h"
#include "esphome/core/log.h"

#include <cmath>
#include <cstring>

namespace esphome::spatial_light {

static const char *const TAG = "spatial_light";

// Timings WS2812 (identiques à NeoEsp32Rmt0800Kbps), résolution RMT 10 MHz = 100 ns/tick.
static const uint32_t RMT_RESOLUTION_HZ = 10000000;
static const uint16_t T0H = 4, T0L = 10, T1H = 10, T1L = 4;
static const uint16_t GAMMA_LUT_SIZE = 1024;

static const uint32_t FAST_FRAME_MS = 10;  // animations : ~100 images/s
static const uint32_t SLOW_FRAME_MS = 40;  // étoiles, minuteur : ~25 images/s
static const uint32_t NIGHT_FADE_MS = 1000;
static const uint32_t NOTIFY_PERIOD_MS = 700;
static const uint32_t ALARM_PERIOD_MS = 1000;
static const uint8_t ALARM_PULSES = 3;
static const float COMET_TAIL = 0.25f;
static const uint32_t NEON_SLOT_MS = 60;
static const uint8_t BLOCK_COUNT = 8;
static const uint8_t RAIN_DROP = 4;

static const char *const PATTERN_NAMES[PATTERN_COUNT] = {
    "center_out", "edges_in", "left_to_right", "right_to_left", "fade",         "random",  "instant",
    "blocks",     "rain",     "comet",         "double_comet",  "elastic",      "sun",     "neon",
};

static inline float smoothstep(float x) { return x * x * (3.0f - 2.0f * x); }
static inline float pulse(float phase) {
  const float s = sinf(phase * (float) M_PI);
  return s * s;
}
static inline uint32_t hash32(uint32_t x) {
  x ^= x >> 16;
  x *= 0x7feb352d;
  x ^= x >> 15;
  x *= 0x846ca68b;
  x ^= x >> 16;
  return x;
}

// ---------------------------------------------------------------------------
// Initialisation
// ---------------------------------------------------------------------------

void SpatialLightOutput::setup() {
  const size_t size = this->max_leds_ * 3;
  RAMAllocator<uint8_t> allocator(RAMAllocator<uint8_t>::ALLOC_INTERNAL);
  this->buf_ = allocator.allocate(size);
  this->tx_buf_ = allocator.allocate(size);
  if (this->buf_ == nullptr || this->tx_buf_ == nullptr) {
    ESP_LOGE(TAG, "Cannot allocate LED buffers");
    this->mark_failed();
    return;
  }
  memset(this->buf_, 0, size);
  memset(this->tx_buf_, 0, size);

  this->level_.assign(this->max_leds_, 0.0f);
  this->start_level_.assign(this->max_leds_, 0.0f);
  this->order_.assign(this->max_leds_, 0.0f);
  this->num_leds_ = clamp<uint16_t>(this->num_leds_, 1, this->max_leds_);
  this->update_zone_();

  this->gamma_lut_.resize(GAMMA_LUT_SIZE);
  for (uint16_t i = 0; i < GAMMA_LUT_SIZE; i++) {
    float x = (float) i / (GAMMA_LUT_SIZE - 1);
    this->gamma_lut_[i] = (uint16_t) lroundf(powf(x, this->gamma_) * 65535.0f);
  }

  rmt_tx_channel_config_t channel;
  memset(&channel, 0, sizeof(channel));
  channel.clk_src = RMT_CLK_SRC_DEFAULT;
  channel.resolution_hz = RMT_RESOLUTION_HZ;
  channel.gpio_num = gpio_num_t(this->pin_);
  channel.mem_block_symbols = this->rmt_symbols_;
  channel.trans_queue_depth = 1;
  if (rmt_new_tx_channel(&channel, &this->channel_) != ESP_OK) {
    ESP_LOGE(TAG, "RMT channel creation failed");
    this->mark_failed();
    return;
  }

  rmt_bytes_encoder_config_t encoder;
  memset(&encoder, 0, sizeof(encoder));
  encoder.bit0.level0 = 1;
  encoder.bit0.duration0 = T0H;
  encoder.bit0.level1 = 0;
  encoder.bit0.duration1 = T0L;
  encoder.bit1.level0 = 1;
  encoder.bit1.duration0 = T1H;
  encoder.bit1.level1 = 0;
  encoder.bit1.duration1 = T1L;
  encoder.flags.msb_first = 1;
  if (rmt_new_bytes_encoder(&encoder, &this->encoder_) != ESP_OK) {
    ESP_LOGE(TAG, "RMT encoder creation failed");
    this->mark_failed();
    return;
  }

  if (rmt_enable(this->channel_) != ESP_OK) {
    ESP_LOGE(TAG, "RMT enable failed");
    this->mark_failed();
    return;
  }

  // Ruban éteint au démarrage
  this->show_();
}

void SpatialLightOutput::dump_config() {
  ESP_LOGCONFIG(TAG, "Spatial Light:");
  ESP_LOGCONFIG(TAG, "  Pin: GPIO%u", this->pin_);
  ESP_LOGCONFIG(TAG, "  LEDs: %u (max %u), zone %u..%u", this->num_leds_, this->max_leds_, this->zone_begin_,
                this->zone_end_ - 1);
  ESP_LOGCONFIG(TAG, "  Temperatures: amber %.0fK, warm %.0fK, cold %.0fK", this->amber_k_, this->warm_k_,
                this->cold_k_);
  ESP_LOGCONFIG(TAG, "  Gamma: %.2f, refresh interval: %ums", this->gamma_, (unsigned) this->refresh_interval_);
  ESP_LOGCONFIG(TAG, "  On: %s %ums, Off: %s %ums", PATTERN_NAMES[this->on_pattern_], (unsigned) this->on_duration_,
                PATTERN_NAMES[this->off_pattern_], (unsigned) this->off_duration_);
  ESP_LOGCONFIG(TAG, "  Transition: %ums, fade width: %.0f%%", (unsigned) this->transition_,
                this->fade_width_ * 100.0f);
  ESP_LOGCONFIG(TAG, "  Night light: %.0f%% over %.0f%% of the zone", this->night_brightness_ * 100.0f,
                this->night_width_ * 100.0f);
}

light::LightTraits SpatialLightOutput::get_traits() {
  light::LightTraits traits;
  traits.set_supported_color_modes({light::ColorMode::COLOR_TEMPERATURE});
  traits.set_min_mireds(1e6f / this->cold_k_);
  traits.set_max_mireds(1e6f / this->amber_k_);
  return traits;
}

// ---------------------------------------------------------------------------
// Commandes
// ---------------------------------------------------------------------------

void SpatialLightOutput::write_state(light::LightState *state) {
  // On lit les valeurs cibles (remote_values) : les animations et transitions sont gérées ici,
  // pas par le transformer standard d'ESPHome.
  const auto &v = state->remote_values;
  const bool on = v.is_on();
  const float bright = v.get_brightness();
  const float mireds = v.get_color_temperature();
  const float kelvin = mireds > 0.0f ? 1e6f / mireds : this->kelvin_to_;

  if (on && !this->is_on_) {
    this->is_on_ = true;
    this->transitioning_ = false;
    this->bright_ = this->bright_from_ = this->bright_to_ = bright;
    this->kelvin_ = this->kelvin_from_ = this->kelvin_to_ = kelvin;
    this->set_night_light(false);  // la lumière principale remplace la veilleuse
    this->start_animation_(ANIM_ON);
  } else if (!on && this->is_on_) {
    this->is_on_ = false;
    this->start_animation_(ANIM_OFF);
  } else if (on && (fabsf(bright - this->bright_to_) > 1e-4f || fabsf(kelvin - this->kelvin_to_) > 0.5f)) {
    this->bright_from_ = this->bright_;
    this->kelvin_from_ = this->kelvin_;
    this->bright_to_ = bright;
    this->kelvin_to_ = kelvin;
    this->trans_start_ = millis();
    this->transitioning_ = true;
    this->high_freq_.start();
  }
}

void SpatialLightOutput::start_timer(uint32_t duration_ms) {
  if (duration_ms == 0) {
    this->cancel_timer();
    return;
  }
  this->timer_phase_ = TIMER_RUNNING;
  this->timer_start_ = millis();
  this->timer_duration_ = duration_ms;
  this->timer_remaining_ = 1.0f;
  ESP_LOGI(TAG, "Minuteur démarré : %us", (unsigned) (duration_ms / 1000));
}

void SpatialLightOutput::cancel_timer() {
  if (this->timer_phase_ == TIMER_IDLE)
    return;
  this->timer_phase_ = TIMER_IDLE;
  this->dirty_ = true;
  ESP_LOGI(TAG, "Minuteur annulé");
}

void SpatialLightOutput::notify(uint8_t pulses) {
  this->notify_pulses_ = std::max<uint8_t>(1, pulses);
  this->notify_start_ = millis();
  this->high_freq_.start();
}

void SpatialLightOutput::set_night_light(bool on) {
  if (on == this->night_)
    return;
  this->night_ = on;
  // La veilleuse ne s'affiche que lumière éteinte
  if (on && this->is_on_ && this->state_ != nullptr)
    this->state_->turn_off().perform();
  this->high_freq_.start();
  this->night_cb_.call(on);
}

void SpatialLightOutput::set_twinkle(bool on, float base, float stars_per_s, uint32_t star_ms, float star_kelvin) {
  this->twinkle_ = on;
  if (on) {
    this->twinkle_base_ = base;
    this->twinkle_rate_ = stars_per_s;
    this->twinkle_star_ms_ = std::max<uint32_t>(star_ms, 100);
    this->twinkle_kelvin_ = star_kelvin;
    this->star_.assign(this->max_leds_, 0.0f);
  } else {
    this->star_.clear();
    this->star_.shrink_to_fit();
  }
  this->dirty_ = true;
}

// ---------------------------------------------------------------------------
// Animations
// ---------------------------------------------------------------------------

void SpatialLightOutput::update_zone_() {
  const uint16_t n = this->num_leds_;
  uint16_t begin = (uint16_t) lroundf(this->zone_start_pct_ * n / 100.0f);
  uint16_t end = (uint16_t) lroundf(this->zone_end_pct_ * n / 100.0f);
  begin = std::min<uint16_t>(begin, n - 1);
  end = clamp<uint16_t>(end, begin + 1, n);
  this->zone_begin_ = begin;
  this->zone_end_ = end;
}

void SpatialLightOutput::reset_levels_() {
  // Termine l'animation en cours et applique l'état final.
  this->anim_ = ANIM_NONE;
  this->anim_kelvin_ = 0.0f;
  std::fill(this->level_.begin(), this->level_.end(), 0.0f);
  if (this->is_on_)
    std::fill(this->level_.begin() + this->zone_begin_, this->level_.begin() + this->zone_end_, 1.0f);
  this->dirty_ = true;
}

void SpatialLightOutput::start_animation_(Anim anim) {
  if (this->level_.empty())
    return;
  const Pattern pattern = anim == ANIM_ON ? this->on_pattern_ : this->off_pattern_;
  this->compute_order_(pattern);
  // On part des niveaux actuels : une animation interrompue repart de là où elle en était.
  this->start_level_ = this->level_;
  this->anim_ = anim;
  this->anim_pattern_ = pattern;
  this->anim_start_ = millis();
  this->anim_duration_ = pattern == PATTERN_INSTANT ? 0 : (anim == ANIM_ON ? this->on_duration_ : this->off_duration_);
  this->anim_kelvin_ = 0.0f;
  this->high_freq_.start();
}

void SpatialLightOutput::compute_order_(Pattern pattern) {
  const uint16_t b = this->zone_begin_;
  const uint16_t m = this->zone_end_ - this->zone_begin_;
  const float last = m > 1 ? (float) (m - 1) : 1.0f;
  const float center = (m - 1) / 2.0f;

  switch (pattern) {
    case PATTERN_CENTER_OUT:
    case PATTERN_ELASTIC:
    case PATTERN_EDGES_IN:
    case PATTERN_DOUBLE_COMET:
      for (uint16_t j = 0; j < m; j++) {
        float d = center > 0.0f ? fabsf(j - center) / center : 0.0f;
        // Les comètes partent des bords : position 0 au bord
        bool from_edges = pattern == PATTERN_EDGES_IN || pattern == PATTERN_DOUBLE_COMET;
        this->order_[b + j] = from_edges ? 1.0f - d : d;
      }
      break;
    case PATTERN_LEFT_TO_RIGHT:
    case PATTERN_COMET:
      for (uint16_t j = 0; j < m; j++)
        this->order_[b + j] = j / last;
      break;
    case PATTERN_RIGHT_TO_LEFT:
      for (uint16_t j = 0; j < m; j++)
        this->order_[b + j] = 1.0f - j / last;
      break;
    case PATTERN_BLOCKS: {
      const uint16_t blocks = std::min<uint16_t>(BLOCK_COUNT, m);
      for (uint16_t j = 0; j < m; j++) {
        uint16_t block = (uint32_t) j * blocks / m;
        this->order_[b + j] = blocks > 1 ? (float) block / (blocks - 1) : 0.0f;
      }
      break;
    }
    case PATTERN_RANDOM:
    case PATTERN_RAIN: {
      // Ordre aléatoire par groupes (1 LED pour "random", RAIN_DROP LEDs pour "rain")
      const uint16_t group = pattern == PATTERN_RAIN ? RAIN_DROP : 1;
      const uint16_t groups = (m + group - 1) / group;
      std::vector<uint16_t> perm(groups);
      for (uint16_t g = 0; g < groups; g++)
        perm[g] = g;
      for (uint16_t g = groups - 1; g > 0; g--)
        std::swap(perm[g], perm[random_uint32() % (g + 1)]);
      const float glast = groups > 1 ? (float) (groups - 1) : 1.0f;
      for (uint16_t j = 0; j < m; j++)
        this->order_[b + j] = perm[j / group] / glast;
      break;
    }
    default:  // PATTERN_FADE, PATTERN_INSTANT, PATTERN_SUN, PATTERN_NEON : globaux
      for (uint16_t j = 0; j < m; j++)
        this->order_[b + j] = 0.0f;
      break;
  }
}

// Progression 0..1 (parfois un peu plus) d'une LED à la position x, pour une avance p de l'animation.
float SpatialLightOutput::profile_(float p, float x, uint32_t elapsed) const {
  const float w = this->fade_width_;
  switch (this->anim_pattern_) {
    case PATTERN_FADE:
    case PATTERN_INSTANT:
    case PATTERN_SUN:
      return smoothstep(p);

    case PATTERN_NEON: {
      if (p < 0.7f) {
        // Amorçage : flashs irréguliers de plus en plus fréquents
        const uint32_t h = hash32(elapsed / NEON_SLOT_MS + this->anim_start_);
        const float r = (h & 0xFFFF) / 65535.0f;
        const float probability = 0.1f + 0.5f * (p / 0.7f);
        return r < probability ? 0.5f + 0.3f * ((h >> 16) & 0xFF) / 255.0f : 0.0f;
      }
      return 0.6f + 0.4f * smoothstep((p - 0.7f) / 0.3f);
    }

    case PATTERN_COMET:
    case PATTERN_DOUBLE_COMET: {
      const float head = p * (1.0f + COMET_TAIL);
      const float d = head - x;
      if (d < 0.0f)
        return 0.0f;
      const float trail = clamp(1.0f - d / COMET_TAIL, 0.0f, 1.0f);
      const float fill = 0.35f + 0.65f * smoothstep(clamp((p - 0.5f) / 0.5f, 0.0f, 1.0f));
      return std::max(trail, fill);
    }

    case PATTERN_ELASTIC: {
      const float t = clamp((p - x * (1.0f - w)) / w, 0.0f, 1.0f) - 1.0f;
      const float c1 = 1.70158f, c3 = c1 + 1.0f;  // easeOutBack : ~10 % de dépassement
      return 1.0f + c3 * t * t * t + c1 * t * t;
    }

    default:
      return smoothstep(clamp((p - x * (1.0f - w)) / w, 0.0f, 1.0f));
  }
}

bool SpatialLightOutput::is_active_() const {
  const float night_target = this->night_ ? 1.0f : 0.0f;
  return this->anim_ != ANIM_NONE || this->transitioning_ || this->night_fade_ != night_target ||
         this->notify_pulses_ != 0 || this->timer_phase_ != TIMER_IDLE || (this->twinkle_ && this->is_on_);
}

void SpatialLightOutput::step_(uint32_t now) {
  const uint32_t dt = this->last_step_ == 0 ? 0 : std::min<uint32_t>(now - this->last_step_, 200);
  this->last_step_ = now;

  if (this->anim_ != ANIM_NONE) {
    const uint32_t elapsed = now - this->anim_start_;
    const float p = this->anim_duration_ == 0 ? 1.0f : std::min(1.0f, (float) elapsed / this->anim_duration_);
    const bool on = this->anim_ == ANIM_ON;

    for (uint16_t i = this->zone_begin_; i < this->zone_end_; i++) {
      const float f = this->profile_(p, this->order_[i], elapsed);
      const float s = this->start_level_[i];
      this->level_[i] = on ? s + (1.0f - s) * f : s * (1.0f - f);
    }

    if (this->anim_pattern_ == PATTERN_SUN) {
      // Lever : part de l'ambre vers la température demandée ; coucher : l'inverse
      const float e = smoothstep(p);
      this->anim_kelvin_ = on ? this->amber_k_ + (this->kelvin_ - this->amber_k_) * e
                              : this->kelvin_ + (this->amber_k_ - this->kelvin_) * e;
    }

    if (p >= 1.0f)
      this->reset_levels_();
  }

  if (this->transitioning_) {
    const uint32_t elapsed = now - this->trans_start_;
    const float t = this->transition_ == 0 ? 1.0f : std::min(1.0f, (float) elapsed / this->transition_);
    const float e = smoothstep(t);
    this->bright_ = this->bright_from_ + (this->bright_to_ - this->bright_from_) * e;
    this->kelvin_ = this->kelvin_from_ + (this->kelvin_to_ - this->kelvin_from_) * e;
    if (t >= 1.0f)
      this->transitioning_ = false;
  }

  // Veilleuse : fondu de NIGHT_FADE_MS
  const float night_target = this->night_ ? 1.0f : 0.0f;
  const float night_step = (float) dt / NIGHT_FADE_MS;
  if (this->night_fade_ < night_target) {
    this->night_fade_ = std::min(night_target, this->night_fade_ + night_step);
  } else if (this->night_fade_ > night_target) {
    this->night_fade_ = std::max(night_target, this->night_fade_ - night_step);
  }

  if (this->notify_pulses_ != 0) {
    const uint32_t elapsed = now - this->notify_start_;
    if (elapsed >= this->notify_pulses_ * NOTIFY_PERIOD_MS) {
      this->notify_pulses_ = 0;
      this->notify_pulse_ = 0.0f;
    } else {
      this->notify_pulse_ = pulse((float) (elapsed % NOTIFY_PERIOD_MS) / NOTIFY_PERIOD_MS);
    }
  }

  if (this->timer_phase_ == TIMER_RUNNING) {
    const uint32_t elapsed = now - this->timer_start_;
    if (elapsed >= this->timer_duration_) {
      ESP_LOGI(TAG, "Minuteur terminé");
      this->timer_phase_ = TIMER_ALARM;
      this->timer_start_ = now;
      this->timer_remaining_ = 0.0f;
      this->high_freq_.start();
    } else {
      this->timer_remaining_ = 1.0f - (float) elapsed / this->timer_duration_;
    }
  } else if (this->timer_phase_ == TIMER_ALARM) {
    const uint32_t elapsed = now - this->timer_start_;
    if (elapsed >= ALARM_PULSES * ALARM_PERIOD_MS) {
      this->timer_phase_ = TIMER_IDLE;
      this->alarm_pulse_ = 0.0f;
      this->timer_finished_cb_.call();
    } else {
      this->alarm_pulse_ = pulse((float) (elapsed % ALARM_PERIOD_MS) / ALARM_PERIOD_MS);
    }
  }

  if (this->twinkle_ && !this->star_.empty()) {
    // Probabilité qu'une LED s'allume pendant dt, pour twinkle_rate_ étoiles/s par 100 LEDs
    const float chance = this->twinkle_rate_ / 100.0f * dt / 1000.0f;
    const float rise = 0.15f;  // part de la durée consacrée à la montée
    const float up = dt / (this->twinkle_star_ms_ * rise);
    const float down = dt / (this->twinkle_star_ms_ * (1.0f - rise));
    for (uint16_t i = this->zone_begin_; i < this->zone_end_; i++) {
      float &s = this->star_[i];
      if (s <= 0.0f) {
        if (random_float() < chance)
          s = 0.001f;
      } else {
        s += s < 1.0f ? up : down;
        if (s >= 2.0f)
          s = 0.0f;
      }
    }
  }
}

// ---------------------------------------------------------------------------
// Rendu
// ---------------------------------------------------------------------------

void SpatialLightOutput::channel_scale_(float kelvin, uint32_t *scale) const {
  // Répartition ambre / blanc chaud / blanc froid selon la température
  const float k = clamp(kelvin, this->amber_k_, this->cold_k_);
  float mix[3] = {0.0f, 0.0f, 0.0f};
  if (k <= this->warm_k_) {
    const float m = (k - this->amber_k_) / std::max(1.0f, this->warm_k_ - this->amber_k_);
    mix[CHANNEL_AMBER] = 1.0f - m;
    mix[CHANNEL_WARM] = m;
  } else {
    const float m = (k - this->warm_k_) / std::max(1.0f, this->cold_k_ - this->warm_k_);
    mix[CHANNEL_WARM] = 1.0f - m;
    mix[CHANNEL_COLD] = m;
  }
  for (uint8_t c = 0; c < 3; c++)
    scale[c] = (uint32_t) lroundf(mix[this->channel_order_[c]] * 255.0f);
}

void SpatialLightOutput::render_() {
  uint32_t main_scale[3], night_scale[3], star_scale[3];
  this->channel_scale_(this->anim_kelvin_ > 0.0f ? this->anim_kelvin_ : this->kelvin_, main_scale);
  this->channel_scale_(this->amber_k_, night_scale);
  this->channel_scale_(this->twinkle_kelvin_, star_scale);

  const float center = (this->zone_begin_ + this->zone_end_ - 1) / 2.0f;
  const float half = std::max(0.5f, (this->zone_end_ - this->zone_begin_) / 2.0f);
  const float night_half = this->night_width_ * half;
  const float timer_level = this->is_on_ ? std::max(this->bright_, 0.05f) : this->timer_brightness_;
  const bool twinkle = this->twinkle_ && this->is_on_ && !this->star_.empty();

  auto gamma = [this](float v) -> uint32_t {
    return this->gamma_lut_[(uint16_t) lroundf(clamp(v, 0.0f, 1.0f) * (GAMMA_LUT_SIZE - 1))];
  };

  for (uint16_t i = 0; i < this->max_leds_; i++) {
    uint8_t *px = this->buf_ + i * 3;
    if (i < this->zone_begin_ || i >= this->zone_end_) {
      px[0] = px[1] = px[2] = 0;
      continue;
    }
    const float d = fabsf(i - center);

    // Lumière principale
    float main = this->level_[i] * this->bright_;
    float star = 0.0f;
    if (twinkle) {
      const float s = this->star_[i];
      const float intensity = s <= 0.0f ? 0.0f : (s <= 1.0f ? s : 2.0f - s);
      star = main * intensity;
      main *= this->twinkle_base_;
    }
    if (this->timer_phase_ == TIMER_RUNNING) {
      // La barre rétrécit des bords vers le centre
      main = timer_level * clamp(this->timer_remaining_ * half - d + 0.5f, 0.0f, 1.0f);
      star = 0.0f;
    } else if (this->timer_phase_ == TIMER_ALARM) {
      main = timer_level * this->alarm_pulse_;
      star = 0.0f;
    }
    if (this->notify_pulses_ != 0) {
      // Appliquée en dernier pour rester visible par-dessus le minuteur :
      // LED éclairée -> brève baisse, LED éteinte ou faible -> brève lueur
      const float p = this->notify_pulse_;
      main = main >= 0.25f ? main * (1.0f - 0.6f * p) : std::max(main, 0.5f * p);
      star *= 1.0f - p;
    }

    // Veilleuse : segment central en ambre
    const float night = this->night_fade_ * this->night_brightness_ * clamp(night_half - d + 0.5f, 0.0f, 1.0f);

    const uint32_t g_main = gamma(main), g_night = gamma(night), g_star = gamma(star);
    for (uint8_t c = 0; c < 3; c++) {
      uint32_t v = (g_main * main_scale[c] + g_star * star_scale[c] + 32767) / 65535;
      v = std::max(v, (g_night * night_scale[c] + 32767) / 65535);
      px[c] = (uint8_t) std::min<uint32_t>(v, 255);
    }
  }
}

void SpatialLightOutput::show_() {
  if (rmt_tx_wait_all_done(this->channel_, 100) != ESP_OK) {
    ESP_LOGW(TAG, "RMT TX timeout");
    this->status_set_warning();
    return;
  }
  const size_t size = this->max_leds_ * 3;
  memcpy(this->tx_buf_, this->buf_, size);

  rmt_transmit_config_t config;
  memset(&config, 0, sizeof(config));
  if (rmt_transmit(this->channel_, this->encoder_, this->tx_buf_, size, &config) != ESP_OK) {
    ESP_LOGW(TAG, "RMT TX error");
    this->status_set_warning();
    return;
  }
  this->status_clear_warning();
  this->last_show_ = millis();
}

void SpatialLightOutput::loop() {
  const uint32_t now = millis();
  if (!this->is_active_() && !this->dirty_) {
    this->high_freq_.stop();
    this->last_step_ = 0;
    // Renvoi périodique de la dernière trame : corrige les LEDs corrompues par un parasite
    // sur la ligne de données (typiquement les premières LEDs qui se rallument seules).
    if (this->refresh_interval_ != 0 && now - this->last_show_ >= this->refresh_interval_)
      this->show_();
    return;
  }

  // Animations rapides à ~100 images/s ; étoiles et minuteur seuls à ~25 images/s
  const bool fast = this->anim_ != ANIM_NONE || this->transitioning_ || this->notify_pulses_ != 0 ||
                    this->timer_phase_ == TIMER_ALARM || this->night_fade_ != (this->night_ ? 1.0f : 0.0f);
  if (!fast)
    this->high_freq_.stop();
  if (now - this->last_show_ < (fast ? FAST_FRAME_MS : SLOW_FRAME_MS))
    return;

  this->step_(now);
  this->render_();
  this->show_();
  this->dirty_ = false;
}

// ---------------------------------------------------------------------------
// Paramètres
// ---------------------------------------------------------------------------

void SpatialLightOutput::set_param(Param param, float value) {
  switch (param) {
    case PARAM_NUM_LEDS:
      this->num_leds_ = clamp<uint16_t>((uint16_t) value, 1, this->max_leds_);
      break;
    case PARAM_ON_DURATION:
      this->on_duration_ = (uint32_t) value;
      break;
    case PARAM_OFF_DURATION:
      this->off_duration_ = (uint32_t) value;
      break;
    case PARAM_TRANSITION:
      this->transition_ = (uint32_t) value;
      break;
    case PARAM_FADE_WIDTH:
      this->fade_width_ = clamp(value / 100.0f, 0.01f, 1.0f);
      break;
    case PARAM_ZONE_START:
      this->zone_start_pct_ = clamp(value, 0.0f, 100.0f);
      break;
    case PARAM_ZONE_END:
      this->zone_end_pct_ = clamp(value, 0.0f, 100.0f);
      break;
    case PARAM_NIGHT_BRIGHTNESS:
      this->night_brightness_ = clamp(value / 100.0f, 0.0f, 1.0f);
      break;
    case PARAM_NIGHT_WIDTH:
      this->night_width_ = clamp(value / 100.0f, 0.0f, 1.0f);
      break;
  }

  if (param == PARAM_NUM_LEDS || param == PARAM_ZONE_START || param == PARAM_ZONE_END) {
    const uint16_t begin = this->zone_begin_, end = this->zone_end_;
    this->update_zone_();
    if (!this->level_.empty() && (begin != this->zone_begin_ || end != this->zone_end_))
      this->reset_levels_();
  }
  this->dirty_ = true;
}

float SpatialLightOutput::get_param(Param param) const {
  switch (param) {
    case PARAM_NUM_LEDS:
      return this->num_leds_;
    case PARAM_ON_DURATION:
      return this->on_duration_;
    case PARAM_OFF_DURATION:
      return this->off_duration_;
    case PARAM_TRANSITION:
      return this->transition_;
    case PARAM_FADE_WIDTH:
      return this->fade_width_ * 100.0f;
    case PARAM_ZONE_START:
      return this->zone_start_pct_;
    case PARAM_ZONE_END:
      return this->zone_end_pct_;
    case PARAM_NIGHT_BRIGHTNESS:
      return this->night_brightness_ * 100.0f;
    case PARAM_NIGHT_WIDTH:
      return this->night_width_ * 100.0f;
  }
  return NAN;
}

void SpatialLightOutput::set_pattern(bool on, Pattern pattern) {
  if (pattern >= PATTERN_COUNT)
    return;
  if (on) {
    this->on_pattern_ = pattern;
  } else {
    this->off_pattern_ = pattern;
  }
}

}  // namespace esphome::spatial_light
