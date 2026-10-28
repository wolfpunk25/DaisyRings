#!/bin/sh
# Build the Pod firmware -> release/pod-daisy-rings.bin, and the display sketch -> release/display-app.bin
set -e
cd "$(dirname "$0")/.."
export PATH="$PWD/toolchain/bin:$PATH"
make -C firmware -j8
cp firmware/build/daisy_rings.bin release/pod-daisy-rings.bin
arduino-cli compile --fqbn esp32:esp32:esp32 --build-path /tmp/daisyrings-display display/RingsPanel
cp /tmp/daisyrings-display/RingsPanel.ino.bin release/display-app.bin
