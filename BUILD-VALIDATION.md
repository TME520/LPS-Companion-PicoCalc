# Build validation — 15 September 2026

Completed:

- Full ARM Cortex-M0+ cross-compilation and linking with Arm GNU Toolchain
  14.2.Rel1 (GCC 14.2.1), Pico SDK 2.2.0 and Ninja.
- Real ClockworkPi LCD and I2C keyboard sources compiled into the executable.
- FatFs/SPI SD driver and hardware adapter compiled into the executable.
- `bash build.sh` completed and produced a 268,288-byte RP2040 UF2.
- UF2 check: 524 blocks, valid framing, consecutive block indices, RP2040
  family 0xE48BFF56, flash data begins at 0x10000000 and stays inside 2 MiB.
- Host application tests passed: weight parsing, checkbox toggles, XP bonus,
  save failure rollback, notes, closing days, restore, history ring rollover,
  CRC corruption rejection, and rendering coordinates inside 320x320.
- Linker symbols checked: main stack 0x20030000–0x20040000 (64 KiB),
  ordinary RAM ends at 0x20030000; __bss_end__ at 0x20003bd0.
- Build and test shell scripts pass `bash -n`.

Not tested:

- Physical PicoCalc boot, LCD appearance, actual keyboard events, SD-card writes,
  removal, brownout, or power-loss recovery.
- Firmware revisions/boards different from the upstream original PicoCalc.
- uf2loader or other launcher compatibility (direct BOOTSEL build only).

The host tests exercise application logic, not the physical LCD/I2C/SPI hardware.
The source dependencies are intentionally not bundled. Their exact revisions are
in build.sh; picotool is pinned in CMakeLists.txt. First-time download paths still
require working GitHub access and installed build prerequisites.
