#!/bin/sh
# Usage: scripts/flash-display.sh /dev/cu.usbserial-XXXX
arduino-cli upload -p "$1" --fqbn esp32:esp32:esp32:UploadSpeed=460800 --input-dir /tmp/daisyrings-display
