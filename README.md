# LPS-Companion v1.2

**A pocket-sized daily activity tracker for the PicoCalc.**

LPS-Companion helps you record daily activities, write a short note, and compare your expected and actual weight. Completing activities earns experience points (XP) and unlocks collectible keepsakes.

It runs offline on the original **RP2040 PicoCalc**, with a **320 × 320 display** and physical keyboard. Your entries and progress are saved to the SD card.

**Status:** v1.0 boot and persistent saves were confirmed on a PicoCalc by the user. v1.2 has been compiled and tested on the host; device testing remains pending.

## Changes in v1.2

- On the home screen, **Left** steps back one saved day. Press again to go further back, up to the 31 retained completed days.
- On the home screen, **Right** returns directly to the current day.
- Past days are marked **JOUR ARCHIVE - LECTURE SEULE**. Open Activities, Note or Weight to inspect that day's entries. Historical entries cannot be edited or closed again.
- **Esc** returns to the selected day's menu. In Activities, Left/Right still switches activity pages; return to the menu before navigating days.
- The existing save format is unchanged: v1.0/v1.1 history loads automatically. Browsing never writes either save file or awards XP.

## Changes in v1.1

- Repas renamed to **Régime**.
- **Congé** and **Weekend** added.
- The **Pour demain** screen and its navigation removed.
- **Terminer le jour** is now menu item **4**.
- Existing v1.0 saves load without conversion. The previous Repas checkbox becomes Régime; all other existing activity positions are retained.

Back up the SD card’s `LPS` folder before updating. New activity flags are not supported by v1.0; restore your backup if reverting to that version.

## Build

### Requirements

- Original PicoCalc with a Raspberry Pi Pico/Pico H (RP2040).
- FAT16- or FAT32-formatted SD card.
- Linux development computer; the instructions below use Fedora.
- Internet access for the first build to download dependencies.

### Install the Fedora build tools

```bash
sudo dnf install git cmake ninja-build gcc-c++ python3 \
  arm-none-eabi-gcc-cs arm-none-eabi-gcc-cs-c++ arm-none-eabi-newlib
```

### Compile the firmware

Extract the source package, open a terminal in its directory, and run:

```bash
cd LPS-Companion-PicoCalc
bash build.sh
```

The script downloads the pinned dependencies, compiles the application, and checks the generated UF2 file.

**Output:** `build/lps_companion.uf2`

Subsequent builds reuse the downloaded dependencies. To reduce memory usage while compiling:

```bash
LPS_JOBS=2 bash build.sh
```

The package also contains **`prebuilt/lps_companion.uf2`**, which can be installed without compiling.

## Install

This project produces standalone firmware for **direct BOOTSEL flashing**. Compatibility with UF2 launchers and other Pico modules has not been established.

1. Insert a FAT16/FAT32 SD card into the PicoCalc.
2. Hold the **BOOTSEL** button on the Pico module while connecting the module's USB port to your computer.
3. When the **`RPI-RP2`** drive appears, release BOOTSEL.
4. Copy `build/lps_companion.uf2` or `prebuilt/lps_companion.uf2` onto that drive.
5. The Pico restarts into LPS-Companion.

Use the **Pico module's USB port** for BOOTSEL flashing, rather than assuming the case USB-C port provides it.

Flashing replaces the current Pico firmware, including any installed interpreter or launcher. Keep its UF2 if you want to restore it later. LPS-Companion creates its save directory automatically; no graphics or other asset files need copying to the SD card.

## Usage

### Quick start

1. Open **Activites** and check the activities you have completed.
2. Open **Note du jour**, type a short note, and press Enter to save it.
3. Open **Poids du jour** to enter your expected and actual weight.
4. At the end of the day, choose **Terminer le jour** and press Enter to confirm.
5. Your XP is banked, any new keepsakes are unlocked, and the next day begins.

Days advance manually. Leaving the device switched off does not automatically start a new day.

### Main menu

The device uses French labels. Régime and Congé include their accented character; note entry remains ASCII-only.

| Key | Menu | Purpose |
| --- | --- | --- |
| `1` | Activites | Check or uncheck today's activities. |
| `2` | Note du jour | Write a short daily note. |
| `3` | Poids du jour | Enter expected and actual weight. |
| `4` | Terminer le jour | Save and close today, then advance. |

Use **Up/Down** to select an item, **Enter** to open it, and **Esc** to return to the main menu.

### Activities

| Key | Activity |
| --- | --- |
| `1` | Marche |
| `2` | Régime |
| `3` | Bible |
| `4` | Messe |
| `5` | Travail |
| `6` | Projet |
| `7` | Sieste |
| `8` | Gurumed |
| `9` | Maladie |
| Arrows + Enter | Congé (10) |
| Arrows + Enter | Weekend (11) |

Activities are displayed across three pages (5 + 5 + 1). **Left/Right** changes page; **Up/Down** selects an activity; **Enter** toggles its checkbox. Keys **1–9** toggle the corresponding activity from any page. Select **Congé** on page 2 or **Weekend** on page 3 with Up/Down, then press Enter.

Each checkbox can be counted once per day. Checking or unchecking an activity saves immediately.

### Daily note

Type up to **96 printable ASCII characters**. Use Backspace to delete characters.

- **Enter:** save the note.
- **Esc:** cancel the edit and return home.

Accented characters are not supported by the current text-entry implementation.

### Weight tracking

The two optional fields are:

- **Attendu:** expected weight for today.
- **Effectif:** actual measured weight for today.

Enter values in **kilograms**, using either a decimal point or comma, with up to three decimal places. For example, `110.5` and `110,500` represent the same weight.

Use **Up/Down** or **Tab** to switch fields, **Backspace** to edit, **Enter** to save both fields, and **Esc** to cancel. Leave a field blank when it is not recorded.

The displayed difference is **actual minus expected**. For example, expected `110.5 kg` and actual `111.2 kg` gives `+0.700 kg`.

Expected weight is entered manually. The app does not calculate a diet target or schedule. Both weight fields start empty on the next day.

### XP and keepsakes

The current scoring rules are:

- **10 XP** for each checked activity.
- **10 bonus XP** for checking at least three different activities.
- No penalty for an empty day.

For example, three checked activities earn **40 XP**. Pending XP becomes permanent when you close the day.

New keepsakes are announced after closing a day. There is no collection browsing screen.

| Total XP | Keepsake |
| --- | --- |
| 30 | Carnet de poche |
| 70 | Tasse de the |
| 120 | Boussole |
| 180 | Radio de poche |
| 250 | Mini-ordinateur |
| 330 | Lanterne |

Keepsakes unlock automatically; XP is not spent when unlocking them.

### Finishing the day

Open **Terminer le jour** to review the day's activity count, bonus and total XP. Press **Enter** to confirm or **Esc** to return without closing the day.

Confirmation saves the completed day, banks its XP, and clears the activity checkboxes, note and weight fields for the next day. The following screen announces any new keepsakes. Press Enter to continue.

## Saved data

LPS-Companion stores data in the SD card's `LPS` directory:

- `SAVE_A.BIN`
- `SAVE_B.BIN`

Each file is a complete snapshot of the current day, total XP and retained history; the two files are not assigned to different days. The app alternates writes between them. At startup it checks both records and loads the highest-generation valid snapshot. If the newest is missing or fails validation, it uses the older intact snapshot, which may lack the latest change. If loading fails due to an I/O error or neither existing record is valid, writes are blocked.

Day navigation uses the history from that loaded snapshot in RAM. It indexes backwards from the history ring's next-write position, so the correct day is selected even after the 31 slots wrap. It does not reload the card at every arrow press, switch between A and B to change days, or write while browsing. Back up the **whole `LPS` directory** to preserve your data.

The save contains the current day, total XP and the last **31 completed days**. Older completed days are replaced as new days are added. Browse the retained entries with Left on the home screen. The total XP shown remains your current cumulative total; the activity summary shows XP for the selected day.

If saving fails, restart with a working SD card before continuing. A missing, unreadable or corrupt card does not silently become a new saved profile. Avoid removing the SD card while the app is running.

## Current limits

- Days are numbered sequentially; there is no calendar-date or RTC integration.
- Activities are daily checkboxes, without duration or repetition counts.
- Notes support ASCII text only.
- Saved history is limited to 31 completed days; earlier days cannot be recovered by browsing. Past days are read-only.
- The current build targets the original RP2040 model.

## Development

| File | Role |
| --- | --- |
| `src/lps_companion.cpp` | Application logic, screens and host tests. |
| `src/main.cpp` | PicoCalc display, keyboard and persistent storage adapter. |
| `src/hw_config.c` | SD-card SPI configuration. |
| `CMakeLists.txt` | Firmware build and memory layout. |
| `build.sh` | Dependency download and compilation. |
| `test.sh` | Application tests and A/B storage integration tests with host files. |
| `tests/` | Hardware/FatFs stand-ins used only for host tests of the actual firmware adapter. |
| `verify_uf2.py` | UF2 format, target and address checks. |

Run the application tests on your development computer with:

```bash
bash test.sh
```

Built with the [Raspberry Pi Pico SDK](https://github.com/raspberrypi/pico-sdk), [ClockworkPi PicoCalc drivers](https://github.com/clockworkpi/PicoCalc), and [Carl Kugler's FatFs SPI library](https://github.com/carlk3/no-OS-FatFS-SD-SPI-RPi-Pico). Dependency revisions are pinned in the build configuration; third-party components retain their upstream licensing and notices.
