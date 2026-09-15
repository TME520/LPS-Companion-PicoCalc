# LPS Companion for PicoCalc (RP2040)

A complete C++/Pico SDK project for the original PicoCalc with a Raspberry Pi
Pico/Pico H (RP2040, 2 MiB flash). Builds a standalone UF2. It includes the real
LCD, keyboard and SD adapter, not placeholder hardware functions.

## Compile on Fedora

Install tools once:

```bash
sudo dnf install git cmake ninja-build gcc-c++ python3 \
  arm-none-eabi-gcc-cs arm-none-eabi-gcc-cs-c++ arm-none-eabi-newlib
```

Unzip the package, open a terminal in `LPS-Companion-PicoCalc`, then:

```bash
bash build.sh
```

Output: **`build/lps_companion.uf2`**.
The first build downloads three pinned source dependencies from GitHub plus the
Pico SDK's picotool build dependency; it requires internet access. Subsequent
builds reuse them. No Arduino IDE or manual source editing is required.
The host can be x86_64 or ARM64 provided its Fedora cross-compiler packages are
available. If using Arm's downloadable toolchain instead, put its `bin` folder
on PATH so `arm-none-eabi-gcc` and `arm-none-eabi-g++` are found.

`LPS_JOBS=2 bash build.sh` limits parallel compiler jobs on a small development machine.

## Install on the PicoCalc

This is **direct BOOTSEL firmware**, built at flash address `0x10000000`.
It has not been adapted or tested for pelrun/uf2loader, other launchers, RP2350,
Pico 2, or alternative PicoCalc modules. Do not assume launcher compatibility.

1. Put an existing FAT16/FAT32 SD card in the PicoCalc. The app never formats it.
2. Connect the **Pico module's USB port** while holding its BOOTSEL button.
3. When the `RPI-RP2` USB drive appears, copy `build/lps_companion.uf2` onto it.
4. The Pico reboots into LPS Companion.

Direct flashing replaces the current Pico firmware, including an installed
launcher/interpreter. Keep its original UF2 if you want to restore it later.
The keyboard-controller firmware is not flashed by this project.
A `prebuilt/lps_companion.uf2` is included if an actual ARM build was completed;
see `BUILD-VALIDATION.md` for the precise checks and limits.

## Controls and features

- Up/Down selects a menu item; Enter opens it; Esc returns home.
- Menu: Activities, Note du jour, Poids du jour, Pour demain, Terminer le jour.
- Activities: **Marche, Repas, Bible, Messe, Travail, Projet, Sieste, Gurumed,
  Maladie**, in that order. Left/Right changes the two activity pages. Keys 1–9
  toggle the corresponding activity from either page.
- The existing prototype's rule is retained: 10 XP per checked item and 10 extra
  for at least three checked items. This is prototype scoring, not a judgement
  about illness, meals or religious practice.
- A daily note holds 96 printable ASCII characters. Enter saves, Esc cancels.
  The display uses the upstream 8×12 font padded into 8×16 cells, 40×20 cells.
- Expected and actual weight are optional daily entries in kg. Decimal comma or
  point works, up to three decimals. Up/Down or Tab changes field; Enter saves.
  Leave a field empty if not recorded. Backspace removes digits.
- The comparison is actual minus expected; weights are stored as integer grams.
  No target or weight-loss schedule is inferred or generated.
- "Pour demain" retains the prototype collectible mechanic under its renamed
  label: six keepsakes at 30, 70, 120, 180, 250 and 330 accumulated XP.
- Finishing a day shows a confirmation screen; Enter banks XP and advances once.
  Quiet days incur no penalty. Days are sequential numbers, not RTC dates.

## Save behaviour

The app creates `LPS/SAVE_A.BIN` and `LPS/SAVE_B.BIN` on the SD card. Each is a
3,584-byte envelope containing a generation number, CRC-protected application
state and an outer CRC. The current note, activities, weights and total XP are
saved, plus the last 31 closed days in a ring buffer. There is no Journal menu.
Older closed-day records are overwritten after the 31-day retention window.

Activity toggles save immediately; note/weight changes save on Enter. The inactive
slot is replaced and synced, then read back before the app acknowledges it.
Startup chooses the newest valid slot; an interrupted/corrupt slot can fall back
to the other. This reduces incomplete-write risk but does not make FAT or the SD
card immune to power loss. Back up the entire LPS directory periodically.

If no card can be mounted, the UI opens but changes cannot be saved; insert a
working card and restart. A corrupt save is not silently treated as a new profile.
An I/O error blocks loading, rather than pretending the file is absent. After a
late write error, further writes are blocked until restart because commit status
may be uncertain. The preceding good slot is retained.

## Project contents

- `src/lps_companion.cpp`: portable application and embedded unit tests.
- `src/main.cpp`: PicoCalc screen/keyboard adapter and A/B SD storage.
- `src/hw_config.c`: SD SPI configuration.
- `CMakeLists.txt`: executable, dependencies and RAM/stack layout.
- `build.sh`: dependency bootstrap and UF2 build.
- `verify_uf2.py`: structural, address and RP2040 family checks.
- `test.sh`: host-side application tests.

## Hardware and memory

LCD: SPI1, SCK 10, MOSI 11, MISO 12, CS 13, DC 14, RESET 15.
Keyboard: I2C1, SDA 6, SCL 7, address 0x1F, 10 kHz.
SD: SPI0, SCK 18, MOSI 19, MISO 16, CS 17, 12.5 MHz maximum.
These are GPIO numbers, not physical header pin numbers.

No external PSRAM, Wi-Fi, audio or second CPU core is used. A cached text grid
redraws only changed rows, using one 640-byte monochrome rendering buffer.
A generated linker script reserves 64 KiB for the main stack, keeping ordinary
RAM allocations in the first 192 KiB. This accommodates bounded save/restore
buffers. Do not substitute a different SDK linker script without reviewing that
reservation.

## Troubleshooting

- Missing `arm-none-eabi-g++` / `<array>`: install the C++ cross-compiler and newlib.
- Missing host C++ compiler: install `gcc-c++`; picotool is built on the host.
- SDK warnings about TinyUSB: expected; this application disables USB serial.
- No USB drive via the case USB-C port: BOOTSEL flashing uses the Pico module USB.
- Blank display or unresponsive keys: report the PicoCalc board/module and existing
  keyboard firmware version. No physical-device test has been performed here.
- Editing SDK/dependency revisions: use a fresh build directory; `build.sh` refuses
  to overwrite a modified dependency when switching to its pinned revision.

The earlier standalone `lps_companion.cpp` desktop `.sav` file is not imported:
this firmware uses versioned A/B envelopes on SD.

## References

ClockworkPi driver source and wiring:
https://github.com/clockworkpi/PicoCalc/tree/f91519806d4b2e0a62c4638a9f695cd5162c5479/Code

Raspberry Pi Pico SDK 2.2.0:
https://github.com/raspberrypi/pico-sdk/tree/a1438dff1d38bd9c65dbd693f0e5db4b9ae91779

Carl Kugler's FatFs SPI library:
https://github.com/carlk3/no-OS-FatFS-SD-SPI-RPi-Pico/tree/196016f525e5b9c161f2b965ddd3045a4ef87649

Fedora toolchain packages:
https://packages.fedoraproject.org/pkgs/arm-none-eabi-gcc-cs/arm-none-eabi-gcc-cs-c++/

Third-party source retains its upstream licensing and notices. Source dependencies
are downloaded separately; see their repositories for licensing before redistributing
derived firmware. In particular, the ClockworkPi font carries its own upstream
attribution in `lcdspi/fonts/font1.h`.
