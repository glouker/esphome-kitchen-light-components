#pragma once

#include "esphome/core/component.h"
#include "esphome/core/preferences.h"
#include "esphome/components/number/number.h"
#include "esphome/components/select/select.h"
#include "spatial_light.h"

namespace esphome::spatial_light {

// Number Home Assistant qui pilote un paramètre du ruban, valeur persistée en flash.
class SpatialLightNumber : public number::Number, public Component {
 public:
  void set_parent(SpatialLightOutput *parent) { this->parent_ = parent; }
  void set_param(Param param) { this->param_ = param; }
  float get_setup_priority() const override { return setup_priority::HARDWARE - 1.0f; }

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

  SpatialLightOutput *parent_{nullptr};
  Param param_{PARAM_NUM_LEDS};
  ESPPreferenceObject pref_;
};

// Select Home Assistant pour l'effet d'allumage ou d'extinction, persisté en flash.
class SpatialLightSelect : public select::Select, public Component {
 public:
  void set_parent(SpatialLightOutput *parent) { this->parent_ = parent; }
  void set_on_effect(bool on) { this->on_ = on; }
  float get_setup_priority() const override { return setup_priority::HARDWARE - 1.0f; }

  void setup() override {
    uint32_t index;
    this->pref_ = this->make_entity_preference<uint32_t>();
    if (!this->pref_.load(&index) || !this->has_index(index))
      index = this->parent_->get_pattern(this->on_);
    this->parent_->set_pattern(this->on_, (Pattern) index);
    this->publish_state(index);
  }

 protected:
  void control(size_t index) override {
    this->parent_->set_pattern(this->on_, (Pattern) index);
    uint32_t value = index;
    this->pref_.save(&value);
    this->publish_state(index);
  }

  SpatialLightOutput *parent_{nullptr};
  bool on_{true};
  ESPPreferenceObject pref_;
};

}  // namespace esphome::spatial_light
