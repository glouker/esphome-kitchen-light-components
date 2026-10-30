#pragma once

#include "esphome/core/component.h"
#include "esphome/core/preferences.h"
#include "esphome/components/number/number.h"
#include "mpr121_bar.h"

namespace esphome::mpr121_bar {

// Number Home Assistant qui règle la sensibilité du tactile, valeur persistée en flash.
class MPR121BarNumber : public number::Number, public Component {
 public:
  void set_parent(MPR121Bar *parent) { this->parent_ = parent; }
  void set_param(Param param) { this->param_ = param; }
  float get_setup_priority() const override { return setup_priority::DATA - 1.0f; }

  void setup() override {
    float value;
    this->pref_ = this->make_entity_preference<float>();
    if (!this->pref_.load(&value))
      value = this->parent_->get_param(this->param_);  // défaut = valeur du YAML
    this->parent_->set_param(this->param_, value);
    this->publish_state(this->parent_->get_param(this->param_));
  }

 protected:
  void control(float value) override {
    this->parent_->set_param(this->param_, value);
    value = this->parent_->get_param(this->param_);
    this->pref_.save(&value);
    this->publish_state(value);
  }

  MPR121Bar *parent_{nullptr};
  Param param_{PARAM_THRESHOLD};
  ESPPreferenceObject pref_;
};

}  // namespace esphome::mpr121_bar
