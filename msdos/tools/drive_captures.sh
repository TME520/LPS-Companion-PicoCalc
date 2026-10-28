#!/bin/sh
# Drive host binary through main screens; plat_host with capture writes captures/*.txt
# For now use a small C driver.
set -e
cd "$(dirname "$0")/.."
gcc -std=c89 -Wall -Isrc -DLPS_HOST -o tools/capture_drive tools/capture_drive.c \
  src/state.c src/ics.c src/save.c src/paths.c src/rosary.c src/app.c src/plat_host.c
mkdir -p captures
rm -rf /tmp/lps-msdos-cap
mkdir -p /tmp/lps-msdos-cap
./tools/capture_drive
echo "Captures written under captures/"
ls -la captures | head
