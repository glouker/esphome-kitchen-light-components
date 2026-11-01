#include "mpr121_bar.h"

#include "esphome/core/hal.h"
#include "esphome/core/log.h"

#include <cstdlib>

namespace esphome::mpr121_bar {

static const char *const TAG = "mpr121_bar";

static const uint8_t REG_FILTDATA_0L = 0x04;
static const uint8_t REG_CONFIG1 = 0x5C;
static const uint8_t REG_CONFIG2 = 0x5D;
static const uint8_t REG_ECR = 0x5E;
static const uint8_t REG_SOFT_RESET = 0x80;

static const uint32_t POLL_INTERVAL_MS = 20;  // 50 Hz
static const uint32_t EMA_ALPHA = 128;        // tau ≈ 2.5 s à 50 Hz
static const uint8_t MAX_I2C_ERRORS = 5;
static const uint32_t PUBLISH_INTERVAL_MS = 500;

bool MPR121Bar::configure_() {
  // Reset logiciel puis vérification de la présence du composant
  if (!this->write_byte(REG_SOFT_RESET, 0x63))
    return false;
  delay(1);
  this->write_byte(REG_ECR, 0x00);
  uint8_t config2 = 0;
  if (!this->read_byte(REG_CONFIG2, &config2) || config2 != 0x24) {
    ESP_LOGE(TAG, "MPR121 non détecté (CONFIG2=0x%02X)", config2);
    return false;
  }

  // Courant de charge maximal : adapté aux grandes électrodes (barre métallique)
  this->write_byte(REG_CONFIG1, 0xBF);  // FFI=18, CDC=63uA
  this->write_byte(REG_CONFIG2, 0x60);  // CDT=4us, SFI=4, ESI=1ms

  // Filtres de suivi de baseline matérielle (valeurs NXP)
  static const uint8_t FILTERS[8] = {0x01, 0x01, 0x0E, 0x00, 0x01, 0x05, 0x01, 0x00};
  for (uint8_t i = 0; i < 8; i++)
    this->write_byte(0x2B + i, FILTERS[i]);

  // Run mode, CL=01, électrodes 0..channel actives
  this->write_byte(REG_ECR, 0x40 | (this->channel_ + 1));
  delay(100);
  return true;
}

bool MPR121Bar::read_filtered_(uint16_t *value) {
  uint8_t data[2];
  if (this->read_register(REG_FILTDATA_0L + 2 * this->channel_, data, 2) != i2c::ERROR_OK)
    return false;
  *value = ((data[1] & 0x03) << 8) | data[0];
  return true;
}

void MPR121Bar::recalibrate_(const char *reason) {
  uint16_t current;
  if (!this->read_filtered_(&current))
    return;
  this->baseline_scaled_ = (uint32_t) current * EMA_ALPHA;
  this->state_ = STATE_IDLE;
  this->confirm_count_ = 0;
  this->set_touched_(false);
  ESP_LOGI(TAG, "Recalibration (%s) : baseline=%u", reason, current);
}

void MPR121Bar::set_touched_(bool touched) {
  if (this->touched_sensor_ != nullptr)
    this->touched_sensor_->publish_state(touched);
}

void MPR121Bar::setup() {
  if (!this->configure_()) {
    this->mark_failed();
    return;
  }
  this->recalibrate_("init");
}

void MPR121Bar::dump_config() {
  ESP_LOGCONFIG(TAG, "MPR121 Bar:");
  LOG_I2C_DEVICE(this);
  if (this->is_failed())
    ESP_LOGE(TAG, "  Communication échouée");
  ESP_LOGCONFIG(TAG, "  Électrode: %u", this->channel_);
  ESP_LOGCONFIG(TAG, "  Seuil: %u, hystérésis: %u%%, confirmation: %u", this->threshold_, this->hysteresis_pct_,
                this->confirm_samples_);
  ESP_LOGCONFIG(TAG, "  Cooldown: %ums, tap max: %ums, anti-latch: %ums", (unsigned) this->cooldown_,
                (unsigned) this->max_tap_, (unsigned) this->latch_timeout_);
  LOG_SENSOR("  ", "Delta", this->delta_sensor_);
  LOG_BINARY_SENSOR("  ", "Touched", this->touched_sensor_);
}

void MPR121Bar::loop() {
  const uint32_t now = millis();
  if (now - this->last_poll_ < POLL_INTERVAL_MS)
    return;
  this->last_poll_ = now;

  uint16_t filtered;
  if (!this->read_filtered_(&filtered)) {
    if (++this->i2c_errors_ >= MAX_I2C_ERRORS) {
      ESP_LOGW(TAG, "Trop d'erreurs I2C, réinitialisation du MPR121");
      this->i2c_errors_ = 0;
      if (this->configure_())
        this->recalibrate_("i2c-reset");
    }
    this->status_set_warning();
    return;
  }
  this->i2c_errors_ = 0;
  this->status_clear_warning();

  const uint16_t baseline = this->baseline_scaled_ / EMA_ALPHA;
  const int delta = (int) baseline - (int) filtered;
  const int abs_delta = abs(delta);
  const int release = this->threshold_ * this->hysteresis_pct_ / 100;
  this->last_delta_ = delta;

  switch (this->state_) {
    case STATE_IDLE:
      // Suivi lent de la baseline (dérive thermique)
      this->baseline_scaled_ = this->baseline_scaled_ - this->baseline_scaled_ / EMA_ALPHA + filtered;
      if (abs_delta > this->threshold_) {
        this->confirm_count_ = 1;
        this->state_ = STATE_WAIT_CONFIRM;
      }
      break;

    case STATE_WAIT_CONFIRM:
      if (abs_delta > this->threshold_) {
        if (++this->confirm_count_ >= this->confirm_samples_) {
          this->touch_start_ = now;
          this->max_delta_ = abs_delta;
          this->long_press_fired_ = false;
          this->state_ = STATE_TOUCHED;
          this->set_touched_(true);
        }
      } else {
        this->state_ = STATE_IDLE;  // pic de bruit isolé
      }
      break;

    case STATE_TOUCHED:
      if (abs_delta > this->max_delta_)
        this->max_delta_ = abs_delta;
      if (!this->long_press_fired_ && abs_delta >= release && now - this->touch_start_ >= this->max_tap_) {
        // Appui long : déclenché pendant le contact, sans attendre le relâchement
        this->long_press_fired_ = true;
        this->last_tap_ = now;
        ESP_LOGD(TAG, "Appui long");
        this->long_press_callback_.call();
      }
      if (abs_delta < release) {
        const uint32_t duration = now - this->touch_start_;
        if (!this->long_press_fired_ && duration <= this->max_tap_ && now - this->last_tap_ > this->cooldown_) {
          this->last_tap_ = now;
          ESP_LOGD(TAG, "Tap ! pic=%d durée=%ums", this->max_delta_, (unsigned) duration);
          this->tap_callback_.call();
        }
        this->set_touched_(false);
        this->state_ = STATE_COOLDOWN;
      } else if (now - this->touch_start_ > this->latch_timeout_) {
        this->recalibrate_("anti-latch");
      }
      break;

    case STATE_COOLDOWN:
      if (abs_delta < release) {
        this->baseline_scaled_ = (this->baseline_scaled_ * 7 + (uint32_t) filtered * EMA_ALPHA) / 8;
        this->state_ = STATE_IDLE;
      }
      break;
  }

  if (this->delta_sensor_ != nullptr && now - this->last_publish_ >= PUBLISH_INTERVAL_MS &&
      delta != this->published_delta_) {
    this->last_publish_ = now;
    this->published_delta_ = delta;
    this->delta_sensor_->publish_state(delta);
  }
}

void MPR121Bar::set_param(Param param, float value) {
  switch (param) {
    case PARAM_THRESHOLD:
      this->threshold_ = clamp<int>((int) value, 1, 255);
      break;
    case PARAM_HYSTERESIS:
      this->hysteresis_pct_ = clamp<int>((int) value, 10, 90);
      break;
    case PARAM_COOLDOWN:
      this->cooldown_ = (uint32_t) value;
      break;
    case PARAM_CONFIRM_SAMPLES:
      this->confirm_samples_ = clamp<int>((int) value, 1, 20);
      break;
    case PARAM_MAX_TAP:
      this->max_tap_ = (uint32_t) value;
      break;
  }
}

float MPR121Bar::get_param(Param param) const {
  switch (param) {
    case PARAM_THRESHOLD:
      return this->threshold_;
    case PARAM_HYSTERESIS:
      return this->hysteresis_pct_;
    case PARAM_COOLDOWN:
      return this->cooldown_;
    case PARAM_CONFIRM_SAMPLES:
      return this->confirm_samples_;
    case PARAM_MAX_TAP:
      return this->max_tap_;
  }
  return NAN;
}

}  // namespace esphome::mpr121_bar
