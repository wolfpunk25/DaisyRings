#!/bin/sh
# Fetch + build libDaisy and DaisySP, and the ARM toolchain (macOS arm64) into this repo.
set -e
cd "$(dirname "$0")/.."
mkdir -p deps
[ -d deps/libDaisy ] || git clone --depth 1 --recurse-submodules --shallow-submodules https://github.com/electro-smith/libDaisy.git deps/libDaisy
[ -d deps/DaisySP ] || git clone --depth 1 --recurse-submodules --shallow-submodules https://github.com/electro-smith/DaisySP.git deps/DaisySP
if [ ! -d toolchain ]; then
  curl -fL -o /tmp/arm-gcc.tar.xz https://developer.arm.com/-/media/Files/downloads/gnu/13.3.rel1/binrel/arm-gnu-toolchain-13.3.rel1-darwin-arm64-arm-none-eabi.tar.xz
  mkdir -p toolchain-tmp && tar xf /tmp/arm-gcc.tar.xz -C toolchain-tmp && mv toolchain-tmp/* toolchain && rmdir toolchain-tmp
fi
export PATH="$PWD/toolchain/bin:$PATH"
make -C deps/libDaisy -j8
