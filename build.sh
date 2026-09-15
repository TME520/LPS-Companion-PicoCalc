#!/usr/bin/env bash
set -euo pipefail
cd -- "$(dirname -- "${BASH_SOURCE[0]}")"
for tool in git cmake ninja c++ arm-none-eabi-gcc arm-none-eabi-g++ python3; do
  command -v "$tool" >/dev/null || { echo "Missing: $tool. See README.md (Fedora setup)." >&2; exit 1; }
done
fetch() {
 local url="$1" dir="$2" rev="$3"
 if [[ ! -d "$dir/.git" ]]; then
   mkdir -p "$dir"
   git -C "$dir" init -q
   git -C "$dir" remote add origin "$url"
 fi
 if [[ "$(git -C "$dir" rev-parse HEAD 2>/dev/null || true)" != "$rev" ]]; then
   if [[ -n "$(git -C "$dir" status --porcelain)" ]]; then echo "Modified dependency: $dir; refusing to overwrite it." >&2; exit 1; fi
   git -C "$dir" fetch --filter=blob:none --depth 1 origin "$rev"
   git -C "$dir" checkout --detach FETCH_HEAD
 fi
}
fetch https://github.com/raspberrypi/pico-sdk.git deps/pico-sdk a1438dff1d38bd9c65dbd693f0e5db4b9ae91779
# Sparse checkout avoids unrelated demos, PDFs and prebuilt firmware.
if [[ ! -d deps/PicoCalc/.git ]]; then
 mkdir -p deps/PicoCalc
 git -C deps/PicoCalc init -q
 git -C deps/PicoCalc remote add origin https://github.com/clockworkpi/PicoCalc.git
 git -C deps/PicoCalc sparse-checkout init --cone
 git -C deps/PicoCalc sparse-checkout set Code/picocalc_helloworld
fi
fetch https://github.com/clockworkpi/PicoCalc.git deps/PicoCalc f91519806d4b2e0a62c4638a9f695cd5162c5479
fetch https://github.com/carlk3/no-OS-FatFS-SD-SPI-RPi-Pico.git deps/FatFs 196016f525e5b9c161f2b965ddd3045a4ef87649
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release "$@"
cmake --build build --parallel "${LPS_JOBS:-4}"
python3 verify_uf2.py build/lps_companion.uf2
printf '\nBuilt: %s/build/lps_companion.uf2\n' "$PWD"
