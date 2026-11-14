// stairlight_core.h - pure C++ logic for the stair light (no ESPHome dependencies, unit-testable on a PC).
#pragma once
#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

namespace stair {

struct Rgb {
  uint8_t r = 0, g = 0, b = 0;
  bool operator==(const Rgb &o) const { return r == o.r && g == o.g && b == o.b; }
};

enum Mode : int { MODE_ROLL = 0, MODE_INWARD = 1, MODE_FALL = 2, MODE_SERPENT = 3 };
enum Dir : int { DIR_UP = 0, DIR_DOWN = 1 };  // UP = triggered from bottom, DOWN = triggered from top
enum State : int {
  ST_IDLE, ST_RELAY_WAIT, ST_INTRO, ST_HOLD, ST_OUTRO, ST_RELAY_OFF_WAIT, ST_TEST_WAIT, ST_TEST_HOLD
};

constexpr int MAX_STEPS = 32;
constexpr int MAX_LEDS_PER_STEP = 96;
constexpr uint32_t RELAY_SETTLE_MS = 250;      // relay on -> first frame
constexpr uint32_t RELAY_OFF_DELAY_MS = 500;   // last frame -> relay off
constexpr uint32_t RELAY_OVERRIDE_MS = 5000;   // manual relay toggle duration
constexpr uint8_t TEST_WHITE_LEVEL = 128;      // 50 % white in test pattern

inline int clampi(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

// ---- geometry --------------------------------------------------------------------------------
// s = step, 0 = bottom. c = column across the step, 0 = left. Step 0 runs left->right (">"),
// step 1 right->left ("<"), and so on (serpentine). This is the ONLY place that maps (s,c) -> LED.
inline int pixel_index(int s, int c, int L) { return s * L + ((s & 1) ? (L - 1 - c) : c); }
inline int order_index(int s, int S, Dir d) { return d == DIR_UP ? s : S - 1 - s; }

// ---- timing ----------------------------------------------------------------------------------
inline uint32_t step_delay_ms(int S) { return (uint32_t) clampi(1500 / S, 30, 150); }
inline uint32_t fall_tick_ms(int S) { return (uint32_t) clampi(2500 / (2 * S), 20, 80); }
inline uint32_t serpent_px_ms(int N) { return (uint32_t) clampi(2000 / N, 1, 20); }
constexpr uint32_t INWARD_FILL_MS = 400;
inline uint32_t fade_ms(Mode m) { return m == MODE_ROLL ? 120 : (m == MODE_INWARD ? 60 : 40); }

// Time (ms after animation start) at which pixel (s,c) starts fading in. Modes 1, 2 and 4 only.
inline uint32_t t_on(Mode m, Dir d, int s, int c, int S, int L) {
  int k = order_index(s, S, d);
  switch (m) {
    case MODE_ROLL: return (uint32_t) k * step_delay_ms(S);
    case MODE_INWARD: {
      int half = (L + 1) / 2;
      int dist = std::min(c, L - 1 - c);
      return (uint32_t) k * step_delay_ms(S) + (uint32_t) (dist * (int) INWARD_FILL_MS / half);
    }
    case MODE_SERPENT: {
      int N = S * L, idx = pixel_index(s, c, L);
      int q = d == DIR_UP ? idx : N - 1 - idx;
      return (uint32_t) q * serpent_px_ms(N);
    }
    default: return 0;
  }
}

// Mode 3: blocks (one full step row each) are released every 2 ticks and travel one step per tick.
// UP: start at row 0, stack from the top down (block b lands on row S-1-b).
// DOWN: start at row S-1, stack from the bottom up (block b lands on row b).
inline int fall_landing_tick(Dir d, int s, int S) { return d == DIR_UP ? 2 * S - 2 - s : S - 1 + s; }
inline int fall_block_pos(Dir d, int b, int k, int S) {  // -1 = not released yet
  if (k < 2 * b) return -1;
  int travelled = k - 2 * b;
  return d == DIR_UP ? std::min(travelled, S - 1 - b) : std::max(S - 1 - travelled, b);
}

inline uint32_t duration_ms(Mode m, Dir d, int S, int L) {
  if (m == MODE_FALL) return (uint32_t) (2 * S - 1) * fall_tick_ms(S);
  uint32_t mx = 0;
  for (int s = 0; s < S; s++)
    for (int c = 0; c < L; c++) mx = std::max(mx, t_on(m, d, s, c, S, L));
  return mx + fade_ms(m);
}

inline Rgb scale(Rgb c, int level) {
  Rgb o;
  o.r = (uint8_t) (c.r * level / 255);
  o.g = (uint8_t) (c.g * level / 255);
  o.b = (uint8_t) (c.b * level / 255);
  return o;
}

struct Settings {
  Mode mode = MODE_ROLL;
  int steps = 13;
  int leds = 21;
  uint32_t linger_ms = 10000;
  Rgb color{255, 148, 41};
};

// ---- engine ----------------------------------------------------------------------------------
class Engine {
 public:
  void init(int max_leds) {
    max_leds_ = max_leds < 1 ? 1 : max_leds;
    frame_.assign(max_leds_, Rgb());
    have_render_ = false;
  }

  void apply_settings(const Settings &in, uint32_t now) {
    int S = clampi(in.steps, 1, MAX_STEPS), L = clampi(in.leds, 1, MAX_LEDS_PER_STEP);
    clamped_ = false;
    if (S * L > max_leds_) {
      clamped_ = true;
      if (S > max_leds_) S = max_leds_;
      L = std::max(1, max_leds_ / S);
    }
    bool changed = have_settings_ && (S != S_ || L != L_ || in.mode != mode_);
    S_ = S; L_ = L; mode_ = in.mode; color_ = in.color;
    linger_ms_ = in.linger_ms < 1000 ? 1000 : in.linger_ms;
    have_settings_ = true;
    // Geometry/mode changed mid-run: abort to black, relay follows its normal off-delay.
    if (changed && busy()) enter(ST_RELAY_OFF_WAIT, now);
  }

  void trigger(Dir d, uint32_t now) {
    switch (state_) {
      case ST_IDLE: dir_ = d; relay_on_ = true; enter(ST_RELAY_WAIT, now); break;
      case ST_RELAY_OFF_WAIT: dir_ = d; relay_on_ = true; start_intro(now); break;  // relay still on
      case ST_RELAY_WAIT: case ST_INTRO: break;     // linger starts after the intro anyway
      case ST_HOLD: t_state_ = now; break;           // restart linger timer
      case ST_OUTRO: enter(ST_HOLD, now); break;     // cancel outro, fresh linger
      case ST_TEST_HOLD: dir_ = d; start_intro(now); break;
      case ST_TEST_WAIT: dir_ = d; enter(ST_RELAY_WAIT, now); break;
    }
  }

  void test(uint32_t now) {
    switch (state_) {
      case ST_TEST_WAIT: case ST_TEST_HOLD: enter(ST_RELAY_OFF_WAIT, now); break;  // toggle off
      case ST_IDLE: relay_on_ = true; enter(ST_TEST_WAIT, now); break;
      case ST_RELAY_WAIT: enter(ST_TEST_WAIT, now); break;
      default: enter(ST_TEST_HOLD, now); break;  // relay already on and settled
    }
  }

  void relay_override(uint32_t now) {
    if (override_active_) return;  // second press during the window is ignored
    override_active_ = true;
    override_until_ = now + RELAY_OVERRIDE_MS;
  }

  void tick(uint32_t now) {
    if (override_active_ && (int32_t) (now - override_until_) >= 0) override_active_ = false;
    uint32_t el = now - t_state_;
    switch (state_) {
      case ST_RELAY_WAIT: if (el >= RELAY_SETTLE_MS) start_intro(now); break;
      case ST_INTRO: if (el >= dur_) enter(ST_HOLD, now); break;
      case ST_HOLD: if (el >= linger_ms_) enter(ST_OUTRO, now); break;
      case ST_OUTRO: if (el >= dur_) enter(ST_RELAY_OFF_WAIT, now); break;
      case ST_RELAY_OFF_WAIT: if (el >= RELAY_OFF_DELAY_MS) { relay_on_ = false; enter(ST_IDLE, now); } break;
      case ST_TEST_WAIT: if (el >= RELAY_SETTLE_MS) enter(ST_TEST_HOLD, now); break;
      case ST_TEST_HOLD: if (el >= linger_ms_) enter(ST_RELAY_OFF_WAIT, now); break;
      default: break;
    }
  }

  // Renders into frame(). Cached for 8 ms so both LED outputs get the very same frame.
  void render(uint32_t now) {
    if (have_render_ && (uint32_t) (now - last_render_) < 8) return;
    have_render_ = true;
    last_render_ = now;
    std::fill(frame_.begin(), frame_.end(), Rgb());
    uint32_t t = now - t_state_;
    switch (state_) {
      case ST_INTRO: render_anim(t, false); break;
      case ST_OUTRO: render_anim(t, true); break;
      case ST_HOLD:
        for (int i = 0; i < S_ * L_; i++) frame_[i] = color_;
        break;
      case ST_TEST_HOLD: render_test(); break;
      default: break;
    }
  }

  const std::vector<Rgb> &frame() const { return frame_; }
  bool relay_desired() const { return relay_on_; }
  bool relay_actual() const { return override_active_ ? !relay_on_ : relay_on_; }
  bool override_active() const { return override_active_; }
  bool busy() const { return state_ != ST_IDLE && state_ != ST_RELAY_OFF_WAIT; }
  State state() const { return state_; }
  Dir dir() const { return dir_; }
  int steps() const { return S_; }
  int leds() const { return L_; }
  bool clamped() const { return clamped_; }
  uint32_t duration() const { return dur_; }

  std::string status() const {
    static const char *names[] = {"Idle", "Relay settling", "Playing", "Lingering", "Fading out",
                                  "Relay off delay", "Test: relay settling", "Test pattern"};
    std::string s = names[state_];
    if (clamped_) s += " (clamped to " + std::to_string(S_) + "x" + std::to_string(L_) + ")";
    return s;
  }

 private:
  void enter(State s, uint32_t now) { state_ = s; t_state_ = now; }
  void start_intro(uint32_t now) {
    dur_ = duration_ms(mode_, dir_, S_, L_);
    enter(ST_INTRO, now);
  }

  void render_anim(uint32_t t, bool outro) {
    const int S = S_, L = L_;
    if (mode_ == MODE_FALL) {
      uint32_t tick = fall_tick_ms(S);
      bool lit[MAX_STEPS] = {false};
      if (!outro) {
        int k = (int) (t / tick);
        for (int b = 0; b < S; b++) {
          int p = fall_block_pos(dir_, b, k, S);
          if (p >= 0) lit[p] = true;
        }
      } else {
        for (int s = 0; s < S; s++) lit[s] = t < (uint32_t) fall_landing_tick(dir_, s, S) * tick;
      }
      for (int s = 0; s < S; s++)
        if (lit[s])
          for (int c = 0; c < L; c++) frame_[pixel_index(s, c, L)] = color_;
      return;
    }
    uint32_t fade = fade_ms(mode_);
    for (int s = 0; s < S; s++)
      for (int c = 0; c < L; c++) {
        uint32_t ton = t_on(mode_, dir_, s, c, S, L);
        int lvl = t <= ton ? 0 : (t >= ton + fade ? 255 : (int) (255 * (t - ton) / fade));
        if (outro) lvl = 255 - lvl;
        frame_[pixel_index(s, c, L)] = scale(color_, lvl);
      }
  }

  void render_test() {
    const int S = S_, L = L_;
    for (int c = 0; c < L; c++) {
      frame_[pixel_index(S - 1, c, L)] = Rgb{0, 0, 255};  // top = blue
      frame_[pixel_index(0, c, L)] = Rgb{255, 0, 0};      // bottom = red (wins when S == 1)
    }
    for (int s = 0; s < S; s++) {  // outer LEDs of every step = white (overrides blue/red)
      frame_[pixel_index(s, 0, L)] = Rgb{TEST_WHITE_LEVEL, TEST_WHITE_LEVEL, TEST_WHITE_LEVEL};
      frame_[pixel_index(s, L - 1, L)] = Rgb{TEST_WHITE_LEVEL, TEST_WHITE_LEVEL, TEST_WHITE_LEVEL};
    }
  }

  int max_leds_ = 1;
  std::vector<Rgb> frame_;
  Mode mode_ = MODE_ROLL;
  Dir dir_ = DIR_UP;
  int S_ = 13, L_ = 21;
  uint32_t linger_ms_ = 10000;
  Rgb color_{255, 148, 41};
  bool have_settings_ = false, clamped_ = false;
  State state_ = ST_IDLE;
  uint32_t t_state_ = 0, dur_ = 0, last_render_ = 0, override_until_ = 0;
  bool have_render_ = false, relay_on_ = false, override_active_ = false;
};

// ---- GPIO33 trigger decoding (phase 2: two contact sensors on one ADC pin) ---------------------
// Idle ~3.3 V (10k pull-up). Bottom sensor shorts to GND (~0 V). Top sensor goes through a series
// resistor to GND (~1 V with 4.7k + 10k pull-up). A press is accepted after `samples` consecutive
// readings in the same band, and only after the line was idle for the same number of samples.
class TriggerDecoder {
 public:
  void configure(float bottom_max, float top_min, float top_max, float idle_min, int samples) {
    bottom_max_ = bottom_max; top_min_ = top_min; top_max_ = top_max; idle_min_ = idle_min;
    samples_ = samples < 1 ? 1 : samples;
  }
  // Returns 0 = nothing, 1 = bottom sensor pressed, 2 = top sensor pressed.
  int feed(float v) {
    int band = v >= idle_min_ ? 0 : (v < bottom_max_ ? 1 : ((v >= top_min_ && v <= top_max_) ? 2 : -1));
    if (band == cand_) { if (count_ < 1000) count_++; } else { cand_ = band; count_ = 1; }
    if (count_ == samples_) {
      if (band == 0) armed_ = true;
      else if ((band == 1 || band == 2) && armed_) { armed_ = false; return band; }
    }
    return 0;
  }
 private:
  float bottom_max_ = 0.3f, top_min_ = 0.6f, top_max_ = 1.6f, idle_min_ = 2.5f;
  int samples_ = 3, cand_ = -2, count_ = 0;
  bool armed_ = false;
};

}  // namespace stair
