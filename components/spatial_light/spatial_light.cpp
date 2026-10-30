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
static const uint32_t MIN_FRAME_MS = 10;  // ~100 images/s max
static const uint16_t GAMMA_LUT_SIZE = 1024;

static const char *const PATTERN_NAMES[PATTERN_COUNT] = {
    "center_out", "edges_in", "left_to_right", "right_to_left", "fade", "random", "instant",
};

static inline float smoothstep(float x) { return x * x * (3.0f - 2.0f * x); }

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
  ESP_LOGCONFIG(TAG, "  LEDs: %u (max %u)", this->num_leds_, this->max_leds_);
  ESP_LOGCONFIG(TAG, "  Temperatures: amber %.0fK, warm %.0fK, cold %.0fK", this->amber_k_, this->warm_k_,
                this->cold_k_);
  ESP_LOGCONFIG(TAG, "  Gamma: %.2f", this->gamma_);
  ESP_LOGCONFIG(TAG, "  On: %s %ums, Off: %s %ums", PATTERN_NAMES[this->on_pattern_], (unsigned) this->on_duration_,
                PATTERN_NAMES[this->off_pattern_], (unsigned) this->off_duration_);
  ESP_LOGCONFIG(TAG, "  Transition: %ums, fade width: %.0f%%", (unsigned) this->transition_,
                this->fade_width_ * 100.0f);
}

light::LightTraits SpatialLightOutput::get_traits() {
  light::LightTraits traits;
  traits.set_supported_color_modes({light::ColorMode::COLOR_TEMPERATURE});
  traits.set_min_mireds(1e6f / this->cold_k_);
  traits.set_max_mireds(1e6f / this->amber_k_);
  return traits;
}

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
  this->high_freq_.start();
}

void SpatialLightOutput::compute_order_(Pattern pattern) {
  const uint16_t n = this->num_leds_;
  const float last = n > 1 ? (float) (n - 1) : 1.0f;
  const float center = (n - 1) / 2.0f;

  switch (pattern) {
    case PATTERN_CENTER_OUT:
    case PATTERN_EDGES_IN:
      for (uint16_t i = 0; i < n; i++) {
        float d = center > 0.0f ? fabsf(i - center) / center : 0.0f;
        this->order_[i] = pattern == PATTERN_CENTER_OUT ? d : 1.0f - d;
      }
      break;
    case PATTERN_LEFT_TO_RIGHT:
      for (uint16_t i = 0; i < n; i++)
        this->order_[i] = i / last;
      break;
    case PATTERN_RIGHT_TO_LEFT:
      for (uint16_t i = 0; i < n; i++)
        this->order_[i] = 1.0f - i / last;
      break;
    case PATTERN_RANDOM: {
      std::vector<uint16_t> perm(n);
      for (uint16_t i = 0; i < n; i++)
        perm[i] = i;
      for (uint16_t i = n - 1; i > 0; i--)
        std::swap(perm[i], perm[random_uint32() % (i + 1)]);
      for (uint16_t i = 0; i < n; i++)
        this->order_[perm[i]] = i / last;
      break;
    }
    default:  // PATTERN_FADE, PATTERN_INSTANT
      for (uint16_t i = 0; i < n; i++)
        this->order_[i] = 0.0f;
      break;
  }
}

bool SpatialLightOutput::step_(uint32_t now) {
  bool active = false;

  if (this->anim_ != ANIM_NONE) {
    const uint32_t elapsed = now - this->anim_start_;
    const float p = this->anim_duration_ == 0 ? 1.0f : std::min(1.0f, (float) elapsed / this->anim_duration_);
    const bool global = this->anim_pattern_ == PATTERN_FADE || this->anim_pattern_ == PATTERN_INSTANT;
    const float w = global ? 1.0f : this->fade_width_;
    const bool on = this->anim_ == ANIM_ON;

    for (uint16_t i = 0; i < this->num_leds_; i++) {
      // Chaque LED démarre son propre fondu à order*(1-w) et le termine w plus tard.
      float f = clamp((p - this->order_[i] * (1.0f - w)) / w, 0.0f, 1.0f);
      f = smoothstep(f);
      const float s = this->start_level_[i];
      this->level_[i] = on ? s + (1.0f - s) * f : s * (1.0f - f);
    }

    if (p >= 1.0f) {
      this->anim_ = ANIM_NONE;
    } else {
      active = true;
    }
  }

  if (this->transitioning_) {
    const uint32_t elapsed = now - this->trans_start_;
    const float t = this->transition_ == 0 ? 1.0f : std::min(1.0f, (float) elapsed / this->transition_);
    const float e = smoothstep(t);
    this->bright_ = this->bright_from_ + (this->bright_to_ - this->bright_from_) * e;
    this->kelvin_ = this->kelvin_from_ + (this->kelvin_to_ - this->kelvin_from_) * e;
    if (t >= 1.0f) {
      this->transitioning_ = false;
    } else {
      active = true;
    }
  }

  return active;
}

void SpatialLightOutput::render_() {
  // Répartition ambre / blanc chaud / blanc froid selon la température
  const float k = clamp(this->kelvin_, this->amber_k_, this->cold_k_);
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
  uint32_t scale[3];
  for (uint8_t c = 0; c < 3; c++)
    scale[c] = (uint32_t) lroundf(mix[this->channel_order_[c]] * 255.0f);

  for (uint16_t i = 0; i < this->max_leds_; i++) {
    uint8_t *px = this->buf_ + i * 3;
    if (i >= this->num_leds_) {
      px[0] = px[1] = px[2] = 0;
      continue;
    }
    const float v = clamp(this->level_[i] * this->bright_, 0.0f, 1.0f);
    const uint32_t g = this->gamma_lut_[(uint16_t) lroundf(v * (GAMMA_LUT_SIZE - 1))];
    for (uint8_t c = 0; c < 3; c++)
      px[c] = (uint8_t) ((g * scale[c] + 32767) / 65535);
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
  if (this->anim_ == ANIM_NONE && !this->transitioning_ && !this->dirty_) {
    this->high_freq_.stop();
    return;
  }
  const uint32_t now = millis();
  if (now - this->last_show_ < MIN_FRAME_MS)
    return;

  this->step_(now);
  this->render_();
  this->show_();
  this->dirty_ = false;
}

void SpatialLightOutput::set_param(Param param, float value) {
  switch (param) {
    case PARAM_NUM_LEDS: {
      const uint16_t n = clamp<uint16_t>((uint16_t) value, 1, this->max_leds_);
      if (n == this->num_leds_)
        return;
      if (!this->level_.empty()) {
        // On termine l'animation en cours et on applique l'état final à toutes les LEDs.
        this->anim_ = ANIM_NONE;
        std::fill(this->level_.begin(), this->level_.end(), this->is_on_ ? 1.0f : 0.0f);
      }
      this->num_leds_ = n;
      break;
    }
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
  }
  this->dirty_ = true;
  this->high_freq_.start();
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
