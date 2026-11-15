// stairlight_esphome.h - thin glue between stairlight_core.h and ESPHome.
#pragma once
#include "stairlight_core.h"
#include "esphome/core/color.h"
#ifdef ESP_PLATFORM
#include "driver/gpio.h"
#include "esp_rom_gpio.h"
#include "soc/gpio_sig_map.h"
#endif

namespace stair {

inline Engine &engine() { static Engine e; return e; }
inline TriggerDecoder &decoder() { static TriggerDecoder d; return d; }
inline FramePusher &pusher() { static FramePusher p; return p; }

// Copies the engine frame into the strip and asks it to transmit once.
template<typename A> inline void write_frame(A &a, const std::vector<Rgb> &f) {
  int n = (int) a.size();
  if (n > (int) f.size()) n = (int) f.size();
  for (int i = 0; i < n; i++) a[i] = esphome::Color(f[i].r, f[i].g, f[i].b);
  a.schedule_show();
}

// Diagnostic: detach GPIO16 from the RMT LED driver and drive it as a plain HIGH output so the level
// shifter can be measured with a multimeter. The controller reboots afterwards to hand the pin back.
inline void hold_data_pin_high() {
#ifdef ESP_PLATFORM
  gpio_set_direction(GPIO_NUM_16, GPIO_MODE_OUTPUT);
  esp_rom_gpio_connect_out_signal(16, SIG_GPIO_OUT_IDX, false, false);
  gpio_set_level(GPIO_NUM_16, 1);
#endif
}

}  // namespace stair
