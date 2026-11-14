# Stairlight (ESPHome, ESP32-WROOM-32E N4, WS2815)

## Status of this build (v1)
- Engine logic (`stairlight_core.h`): complete, unit-tested on a PC (`tests/run.sh`, ~250k checks), cross-compiles with the Xtensa ESP32 toolchain.
- ESPHome config: validates with `esphome config` (ESPHome 2026.10.2) for both trigger variants.
- **NOT yet compiled into a firmware image or run on hardware.** The sandbox this was written in could not reach the ESP component registry. Run `esphome compile stairlight.yaml` on your machine first and send back any errors.

## Files
- `stairlight.yaml` - main config. `packages/trigger_gpio.yaml` (phase 1 button) / `packages/trigger_adc.yaml` (phase 2, two sensors on one pin).
- `stairlight_core.h` - all logic: pixel mapping, 4 animations, state machine, relay, test pattern, ADC decoder.
- `stairlight_esphome.h` - glue (frame copy into both strips).
- `secrets.yaml` - your credentials (git-ignored). `secrets.yaml.example` is the template.

## Pins
GPIO16 = LED data (DI), GPIO2 = backup data (BI, identical frame), GPIO12 = relay, GPIO33 = trigger input.
Strapping pins: GPIO12 must be LOW at reset (check the board's relay driver), GPIO2 should be low/floating. If serial flashing fails, unplug BI.
If the relay is active-low set `relay_inverted: "true"`.

## Network
`http://stairlight.local` (user `admin`, password `stair1234`). If `Root-Network` is unreachable the board starts AP `stairlight` (pass `stair1234`); open `http://192.168.4.1`. mDNS may not resolve in AP mode. OTA also works from the web page.

## Web page
Configuration: Animation Mode, Steps (1-32, def 13), LEDs per Step (1-96, def 21), Linger Time (def 10 s), Color.
Actions: Trigger Top, Trigger Bottom, Test Size, Relay (flips the relay for 5 s, second press ignored).
Status: Relay On, Relay Indicator light (green = on, red = off - ESPHome cannot colour a button itself), Status text.
Changing mode/steps/LEDs while running aborts the animation. A colour change applies live.

## Layout assumption
Strip starts at the BOTTOM step, left to right (">"), next step right to left ("<"), etc. If yours starts at the top, change `pixel_index()` in `stairlight_core.h` (one function).

## LED capacity vs frame rate
`max_leds` (compile-time) defaults to 3072 = 32x96, about 10 fps at that size. 1024 gives ~30 fps. Steps x LEDs above `max_leds` is clamped and shown in Status.

## Phase 2 (two sensors, one pin)
Swap the package line in `stairlight.yaml` to `trigger_adc.yaml`. Wire a 10k pull-up from GPIO33 to 3.3 V; bottom sensor = contact to GND; top sensor = contact to GND through ~4.7k. Tune the `adc_*` substitutions after reading the voltages in the log.

## Behaviour summary
Trigger -> relay on, 250 ms settle -> intro in trigger direction -> hold for Linger -> outro (same order/direction, erasing) -> relay off after 500 ms.
Retrigger during intro/hold restarts linger only; during outro it cancels the outro. Test Size: top blue, bottom red, first/last LED of every step white (50 %), shown for the linger time or until pressed again; any trigger cancels it. (Test ends with an instant switch-off, not an outro.)
