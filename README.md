# DaisyRings

Mutable Instruments **Rings** (via the [Resonate](https://github.com/tylerreckart/resonate) Daisy port) on a **Daisy Pod**, with a
touchscreen (ESP32 "CYD" 2.8") as the main interface. Same hardware and wiring as
[PodPlaits](https://github.com/wolfpunk25/PodPlaits): UART4 (Seed D12 TX, D11 RX) to screen IO35/IO27, shared 5V and ground.

## What it does
12 engines: the six Rings resonator models (Modal, Sympathetic, Inharmonic string, FM voice, Quantized strings, Strings + reverb)
and the six string-synth ("Easter egg") effects. 1, 2 or 4 voices. Internal exciter or Pod line-in as exciter.

**Touchscreen**
- **PLAY** – engine selector and a strum pad laid out in the current scale (two octaves). Slide a finger across keys to strum;
  vertical position sets Brightness while you play. Keys flash when the generator or MIDI plays.
- **SOUND** – Structure, Brightness, Damping, Position.
- **GEN** – generative sequencer: pattern (Walk, Random, Arp up, Arp up/down, Pedal), scale (8), Rate, Chance, Range (1–4 octaves), Drift (slow modulation of the sound).
- **MIX** – root note, volume, voices, exciter source, RANDOMIZE.
- **PRESET** – six starting patches. The bottom button starts/stops the generator on every page.

**Pod controls**: encoder = engine, encoder click = generator pattern, button 1 = run/stop generator,
button 2 = cycle what the two knobs control (Struct/Bright, Damp/Pos, Rate/Chance, Drift/Vol; with pickup so values don't jump).

**MIDI** (DIN in on the Pod): note-on strums (omni channel, polyphony follows voice count), pitch bend ±2 st,
CC1 position, CC7 volume, CC71 structure, CC74 brightness, CC75 damping, CC76 position.

## Build
```sh
scripts/setup.sh     # libDaisy, DaisySP, ARM toolchain (macOS arm64)
scripts/build.sh     # needs arduino-cli with esp32 core 3.3.x, TFT_eSPI, XPT2046_Touchscreen
```
TFT_eSPI must use `display/RingsPanel/DisplaySetup.h` as its `User_Setup.h`.
Prebuilt binaries are in `release/`.

## Flash
- **Display**: `scripts/flash-display.sh /dev/cu.usbserial-XXXX` (app image at 0x10000). Hold the screen at power-on to recalibrate; first run asks for calibration.
- **Pod**: the app runs from QSPI behind the Daisy bootloader (`daisy-bootloader.bin` at 0x08000000, already installed if you ran PodPlaits).
  Enter the Daisy bootloader (reset, then press BOOT within 2 s) and upload `release/pod-daisy-rings.bin` with the
  [Daisy Web Programmer](https://electro-smith.github.io/Programmer/) or `dfu-util -a 0 -s 0x90040000:leave -D release/pod-daisy-rings.bin -d 0483:df11`.
  A running PodPlaits also accepts `UPDATE` over its USB serial port to enter the programmer.

## Credits and licences
Rings DSP © Émilie Gillet (MIT), ported to Daisy by Ben Sergentanis and extended by Tyler Reckart (Resonate; no licence file in that repo, the
source files carry Émilie's MIT notice). `firmware/third_party/stmlib` is Émilie Gillet's stmlib (MIT). `daisy-bootloader.bin` is the Electrosmith
bootloader (`Bootloader-LICENSE`). libDaisy / DaisySP are fetched by `scripts/setup.sh` under their own licences. Touch/display code derives from PodPlaits.
