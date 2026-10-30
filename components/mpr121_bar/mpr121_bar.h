#pragma once

#include "esphome/core/automation.h"
#include "esphome/core/component.h"
#include "esphome/core/helpers.h"
#include "esphome/components/binary_sensor/binary_sensor.h"
#include "esphome/components/i2c/i2c.h"
#include "esphome/components/sensor/sensor.h"

namespace esphome::mpr121_bar {

// Paramètres réglables à chaud (numbers Home Assistant).
enum Param : uint8_t {
  PARAM_THRESHOLD = 0,  // delta capacitif minimal pour détecter un contact
  PARAM_HYSTERESIS,     // % du seuil sous lequel le contact est considéré relâché
  PARAM_COOLDOWN,       // ms minimum entre deux taps
  PARAM_CONFIRM_SAMPLES,  // nb d'échantillons consécutifs au-dessus du seuil
  PARAM_MAX_TAP,        // ms : un contact plus long n'est pas un tap
};

/// Détection de tap sur une barre métallique reliée à une électrode MPR121.
/// La détection est logicielle (baseline EMA + confirmation + hystérésis + anti-latch) car la
/// détection matérielle du MPR121 n'est pas fiable avec une grande électrode et le bruit des LEDs.
class MPR121Bar : public Component, public i2c::I2CDevice {
 public:
  void setup() override;
  void loop() override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::DATA; }

  void set_channel(uint8_t channel) { this->channel_ = channel; }
  void set_latch_timeout(uint32_t ms) { this->latch_timeout_ = ms; }
  void set_delta_sensor(sensor::Sensor *s) { this->delta_sensor_ = s; }
  void set_touched_binary_sensor(binary_sensor::BinarySensor *s) { this->touched_sensor_ = s; }

  void set_param(Param param, float value);
  float get_param(Param param) const;

  template<typename F> void add_on_tap_callback(F &&callback) {
    this->tap_callback_.add(std::forward<F>(callback));
  }

 protected:
  enum State : uint8_t { STATE_IDLE, STATE_WAIT_CONFIRM, STATE_TOUCHED, STATE_COOLDOWN };

  bool configure_();
  bool read_filtered_(uint16_t *value);
  void recalibrate_(const char *reason);
  void set_touched_(bool touched);

  uint8_t channel_{0};
  uint8_t threshold_{25};
  uint8_t hysteresis_pct_{50};
  uint32_t cooldown_{300};
  uint8_t confirm_samples_{3};
  uint32_t max_tap_{1000};
  uint32_t latch_timeout_{2000};

  State state_{STATE_IDLE};
  uint32_t baseline_scaled_{0};
  uint8_t confirm_count_{0};
  uint8_t i2c_errors_{0};
  int max_delta_{0};
  int last_delta_{0};
  int published_delta_{INT32_MIN};
  uint32_t last_poll_{0};
  uint32_t last_tap_{0};
  uint32_t touch_start_{0};
  uint32_t last_publish_{0};

  sensor::Sensor *delta_sensor_{nullptr};
  binary_sensor::BinarySensor *touched_sensor_{nullptr};
  CallbackManager<void()> tap_callback_;
};

class TapTrigger : public Trigger<> {
 public:
  explicit TapTrigger(MPR121Bar *parent) {
    parent->add_on_tap_callback([this]() { this->trigger(); });
  }
};

}  // namespace esphome::mpr121_bar
