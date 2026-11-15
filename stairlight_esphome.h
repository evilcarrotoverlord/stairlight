// stairlight_esphome.h - thin glue between stairlight_core.h and ESPHome.
#pragma once
#include "stairlight_core.h"
#include "esphome/core/color.h"

namespace stair {

inline Engine &engine() { static Engine e; return e; }
inline TriggerDecoder &decoder() { static TriggerDecoder d; return d; }
inline FramePusher &pusher() { static FramePusher p; return p; }

// Copies the engine frame into BOTH strips (GPIO16 and GPIO2) and asks each to transmit once.
template<typename A, typename B> inline void write_frame(A &a, B &b, const std::vector<Rgb> &f) {
  int n = (int) a.size();
  if (n > (int) b.size()) n = (int) b.size();
  if (n > (int) f.size()) n = (int) f.size();
  for (int i = 0; i < n; i++) {
    esphome::Color c(f[i].r, f[i].g, f[i].b);
    a[i] = c;
    b[i] = c;
  }
  a.schedule_show();
  b.schedule_show();
}

}  // namespace stair
