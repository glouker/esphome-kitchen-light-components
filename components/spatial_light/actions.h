#pragma once

#include "esphome/core/automation.h"
#include "spatial_light.h"

namespace esphome::spatial_light {

template<typename... Ts> class StartTimerAction : public Action<Ts...> {
 public:
  explicit StartTimerAction(SpatialLightOutput *parent) : parent_(parent) {}
  TEMPLATABLE_VALUE(uint32_t, duration)
  void play(const Ts &...x) override { this->parent_->start_timer(this->duration_.value(x...)); }

 protected:
  SpatialLightOutput *parent_;
};

template<typename... Ts> class CancelTimerAction : public Action<Ts...> {
 public:
  explicit CancelTimerAction(SpatialLightOutput *parent) : parent_(parent) {}
  void play(const Ts &...x) override { this->parent_->cancel_timer(); }

 protected:
  SpatialLightOutput *parent_;
};

template<typename... Ts> class NotifyAction : public Action<Ts...> {
 public:
  explicit NotifyAction(SpatialLightOutput *parent) : parent_(parent) {}
  TEMPLATABLE_VALUE(uint8_t, count)
  void play(const Ts &...x) override { this->parent_->notify(this->count_.value(x...)); }

 protected:
  SpatialLightOutput *parent_;
};

}  // namespace esphome::spatial_light
