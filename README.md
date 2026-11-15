# Stairlight (ESPHome, ESP32-WROOM-32E N4, WS2815)

## v3 changes (board research: DOMRAEM DOM-WLE-AM)
- **GPIO2 / backup data line removed.** One LED output (GPIO16) only. Half the RMT/interrupt load.
- **Corrupted-data fixes (hardware-independent):**
  - `CONFIG_RMT_TX_ISR_CACHE_SAFE=y`: ESP-IDF otherwise pauses the LED-signal interrupt whenever flash is written (ESPHome saves settings/colour to flash), which cuts a frame short. This was the most likely cause of the "some points" glitches in v2.
  - `rmt_symbols: 384` (was 192): bigger hardware buffer, tolerates more Wi-Fi interrupt latency. Max 512 with one strip.
  - `wifi: power_save_mode: none`: removes periodic Wi-Fi sleep/wake jitter.
- **Diagnostics built into the web panel** (group "Diagnostics"): buttons to test the relay (GPIO12) and the data pin / level shifter (GPIO16) with a multimeter. See "Diagnostics" below.

## v2 changes
- **Flicker fix:** v1 used an ESPHome light effect, which re-sends a frame every 16 ms even when nothing changed, and each send blocks the main loop. Both strips were transmitting almost continuously, which corrupts WS281x data next to Wi-Fi. Now frames are written directly and sent only when the picture changes (max every 20 ms), with a 250 ms refresh while lit and nothing at all while idle (`FramePusher` in `stairlight_core.h`, unit-tested).
- **LED capacity:** `max_leds` default is now 512 (about 15 ms per frame instead of ~92 ms). Raise it in `stairlight.yaml` if you need more than 512 LEDs; steps x LEDs above the cap is clamped and shown in Status.
## Status of this build
- Engine logic (`stairlight_core.h`): complete, unit-tested on a PC (`tests/run.sh`, ~250k checks), cross-compiles with the Xtensa ESP32 toolchain.
- ESPHome config: validates with `esphome config` (ESPHome 2026.10.2) for both trigger variants.
- **NOT compiled into a firmware image here (sandbox limits) and not hardware-tested by me.** The sandbox this was written in could not reach the ESP component registry. Run `esphome compile stairlight.yaml` on your machine first and send back any errors.

## Files
- `stairlight.yaml` - main config. `packages/trigger_gpio.yaml` (phase 1 button) / `packages/trigger_adc.yaml` (phase 2, two sensors on one pin).
- `stairlight_core.h` - all logic: pixel mapping, 4 animations, state machine, relay, test pattern, ADC decoder.
- `stairlight_esphome.h` - glue (frame copy into both strips).
- `secrets.yaml` - your credentials (git-ignored). `secrets.yaml.example` is the template.

## Pins
GPIO16 = LED data (output 1, level-shifted on the board), GPIO12 = relay (strip power, active-LOW on this board), GPIO33 = trigger input (the board's IO33 connector, meant for push switches/sensors).
GPIO12 is a strapping pin. DOMRAEM/WLED use it as the relay on this very board, so it is expected to boot fine; if the board ever fails to boot, check what is pulling GPIO12 high at reset.
Not used: GPIO2 (second output), GPIO0 (Opt button), microphone pins.

## Network
`http://stairlight.local` (no login). If `Root-Network` is unreachable the board starts AP `stairlight` (pass `stair1234`); open `http://192.168.4.1`. mDNS may not resolve in AP mode. OTA also works from the web page.

## Web page
Configuration: Animation Mode, Steps (1-32, def 13), LEDs per Step (1-96, def 21), Linger Time (def 10 s), Color.
Actions: Trigger Top, Trigger Bottom, Test Size, Relay (flips the relay for 5 s, second press ignored).
Status: Relay On, Relay Indicator light (green = on, red = off - ESPHome cannot colour a button itself), Status text.
Changing mode/steps/LEDs while running aborts the animation. A colour change applies live.

## Layout assumption
Strip starts at the BOTTOM step, left to right (">"), next step right to left ("<"), etc. If yours starts at the top, change `pixel_index()` in `stairlight_core.h` (one function).

## LED capacity vs frame rate
`max_leds` (compile-time) defaults to 512 (~15 ms per frame). 1024 = ~31 ms, 3072 (full 32x96) = ~92 ms per frame. Steps x LEDs above `max_leds` is clamped and shown in Status.

## Phase 2 (two sensors, one pin)
Swap the package line in `stairlight.yaml` to `trigger_adc.yaml`. Wire a 10k pull-up from GPIO33 to 3.3 V; bottom sensor = contact to GND; top sensor = contact to GND through ~4.7k. Tune the `adc_*` substitutions after reading the voltages in the log.

## Level shifter: what is known
- The DOM-WLE-AM listing and DOMRAEM's manuals say "Level Shifter" and list only these things to configure: LED output GPIO16 (default) / GPIO2, button GPIO33, and **Relay = GPIO12, Invert**. No enable/OE pin or other GPIO for the shifter is documented anywhere I could find, so it should be passive (always on when the board is powered).
- DOMRAEM stresses "LEDs stay off unless the relay GPIO12 is configured". That means the strip supply runs through the relay. If the shifter's output stage is fed from that switched rail, it only works while the relay is ON. The firmware always has the relay on (with a 250 ms settle) before sending data, so that is covered.
- Not confirmed: no public schematic exists, so I can't prove how the shifter is powered. Use the Diagnostics buttons below.

## Diagnostics (web panel, group "Diagnostics")
Use a multimeter (DC volts). Disconnect nothing; the strip may stay connected. The Status line shows the current phase and a countdown.

**Diagnose Relay GPIO12** - 24 s, no reboot. Strip power OFF 8 s, ON 8 s, OFF 8 s. Measure between the strip's **V+ terminal and GND**: about 0 V in the OFF phases and your supply voltage in the ON phase. If it is the other way round the relay polarity is wrong: flip `relay_inverted` in `stairlight.yaml`.

**Diagnose Data GPIO16** - 30 s, then the controller **reboots** (it has to hand GPIO16 back to the LED driver). GPIO16 is held HIGH. Phase 1 (15 s): strip power OFF. Phase 2 (15 s): strip power ON. Measure between the **DATA terminal of output 1 and GND** in each phase:
- about 5 V = the level shifter works
- about 3.3 V = signal is not being shifted
- about 0 V = the shifter has no power or is disabled
- If phase 1 and phase 2 differ, the shifter is powered from the relay-switched rail (normal; the firmware always switches the relay on before sending data).

**Cancel Diagnostic** stops either test early (the data test still reboots). Triggers, Test Size and the relay button are ignored while a diagnostic runs.

## If it still flickers
Electrical checklist: strip ground shared with the controller ground, data wire short, ~330 ohm series resistor at the first LED's DI, 1000 uF capacitor across the strip supply at the strip start, and power-inject long runs. WS2815 12 V strips take 5 V data fine; 3.3 V data is marginal.

## Behaviour summary
Trigger -> relay on, 250 ms settle -> intro in trigger direction -> hold for Linger -> outro (same order/direction, erasing) -> relay off after 500 ms.
Retrigger during intro/hold restarts linger only; during outro it cancels the outro. Test Size: top blue, bottom red, first/last LED of every step white (50 %), shown for the linger time or until pressed again; any trigger cancels it. (Test ends with an instant switch-off, not an outro.)
