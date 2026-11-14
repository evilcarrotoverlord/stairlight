// stairlight_esphome.h - thin glue between stairlight_core.h and ESPHome.
#pragma once
#include "stairlight_core.h"
#include "esphome/core/color.h"

namespace stair {

inline Engine &engine() { static Engine e; return e; }
inline TriggerDecoder &decoder() { static TriggerDecoder d; return d; }

// Called from the "Stair Engine" addressable_lambda effect of BOTH strips (GPIO16 and GPIO2).
template<typename It> inline void copy_frame(It &it, uint32_t now) {
  Engine &e = engine();
  e.render(now);
  const std::vector<Rgb> &f = e.frame();
  int n = (int) it.size();
  if (n > (int) f.size()) n = (int) f.size();
  for (int i = 0; i < n; i++) it[i] = esphome::Color(f[i].r, f[i].g, f[i].b);
}

}  // namespace stair
