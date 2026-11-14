# Validation v1.14 (in progress) — 14 November 2026

- LPS-114-CHG01: Meal Planner.
  - New `tests/meal_planner_test.cpp`. The deck has 16 cards in rows of 6, 8 and 2 in the agreed order, with separate duplicates; every English and French name and portion fits the card (22 cells).
  - Space on Home opens the planner, with no visible menu entry; Space and Esc return to Home. After a visit, the home digits, arrows and Enter still open Activities, Weight and Close the day. Space still types a space in the Kanban editor and is ignored on the Activities screen. Visiting the planner writes nothing.
  - Navigation: Left/Right wrap inside a row (Drinks 1/6 → 6/6 → 1/6, Food 1/8 → 8/8), Up/Down wrap between rows and keep the column, or clamp to the last card of the Blank row.
  - Enter plays a card and saves at once: header `PLAYED #n`, counter `15/16 left`, footer `Enter:Put back`. Enter again puts it back, and later plays move up one place. Both Coffee (Black) cards are played separately. Reopening lands on the first card still in the deck. With all 16 played, the counter reads `0/16 left`. French labels: `PLANIFICATEUR DE REPAS`, `JOUÉE #1`, `Boissons 1/6`, `0/16 restantes`, `Entrée:Remettre`.
  - Persistence: `"meals":{"played":[8,2,1]}` is written at the end of `state` and the order survives a restart. A schema-2 save without `meals` (v1.13) loads with a full deck. Unknown keys inside `meals` are skipped. An index of 16 or -1, a duplicate, a string, a fraction or a non-object `meals` is rejected as damage. The schema stays 2.
  - A failed save rolls the play back and shows `Save failed`; a damaged save blocks plays. From an archived day, Space shows `Archived day: read-only`.
  - Closing the day resets the deck to 16 cards in RAM and in the save; a failed close keeps today's deck.
  - Drawing (`drawMealCard` into a host frame): everything stays inside the 192 × 190 card at (64, 81), with rounded corners left untouched and the row colour on the border. The apple sprite puts red in the centre. A played card is entirely grey. Labels are centred inside the border (`Food`, `Apple`, `1 medium`; `Boissons`, `Thé à la menthe`).
  - PicoCalc adapter: the card is drawn after the text rows with `draw_rect_spi` (fills) and `draw_buffer_spi` (one 128-pixel row at a time, 576-byte row buffer in BSS), and its labels with the same accent-composed 8×16 glyphs as the text rows (glyph code shared). Entering or leaving the planner repaints all rows, so no card remnants are left on Home. Previews were rendered on the host through the real adapter and upstream font.
  - Memory: `sizeof(State)` grows from 30288 to 30308 bytes (17-byte deck state). Firmware object for 32-bit ARM (Cortex-M0+, `-Os`): BSS +0.6 KB (three States, card views, row buffer), flash about +10 KB (6.7 KB of sprites in `.rodata`, plus code). The render path adds about 1.2 KB of stack (row of 128 colours + label bitmap).
  - All twelve host test suites pass with C++17 and warnings treated as errors. Code also compiles warning-free for 32-bit ARM (`-Wshorten-64-to-32`), but no UF2 was built: no Arm GNU toolchain on the build box.
- Pending on device: Space key reaches the app; card colours and sprite look on the real LCD; drawing speed of the card; save time per play.

# Validation v1.13 — 9 November 2026

- LPS-113-CHG09: title bar `LPS COMPANION v1.13`; header comment v1.13; language_test asserts the new title.

- LPS-113-CHG06: JSON save files `SAVE_A.JSN`/`SAVE_B.JSN`.
  - New `tests/json_save_test.cpp`: round trip of a fully populated state (French, pink palette, every activity, 31-day wrapped history, 24 tasks, 3 reminders, undated day, missing weights) with read chunks of 1, 7, 256 and 4096 bytes. History reloads oldest first. Typical full save 7.9 KB; worst case (every text field full of characters needing escapes) 16.6 KB, under the 64 KB parser limit.
  - Every truncation of the file and every single-bit flip is rejected. A wrong `crc32`, a missing `crc32`, a member after it and trailing bytes are rejected. Unknown keys at every level (nested objects/arrays, a 300-byte value, a key longer than the key buffer) are skipped once the crc32 is recomputed. Unknown activity names are ignored.
  - Escaping: quotes, backslashes and control characters are escaped; UTF-8 (é, «», 😀) passes through; `\u00e9` and surrogate pairs decode to UTF-8; lone surrogates, `\u0000` and bad escapes are refused. Field limits match the binary loader (97-char note, day number 0, invalid date, weight 0, unknown palette, 32 history days, unnamed task, 366-day reminder).
  - A newer schema with a valid crc32 is reported as unsupported; an I/O error part-way is reported as an I/O error.
  - `storage_test` (real `src/main.cpp` adapter on host files): 40 alternating A/B saves, newest generation loaded, archived-day browsing leaves both `.JSN` files byte-for-byte unchanged, damaged or missing newest slot falls back, both damaged or read errors block writes. ICS and CSV assertions are unchanged.
  - `migration_test`: the authentic v1.2 fixture (format 1) in either `.BIN` slot and a full v1.12 (format 6) save both migrate to JSON with every field kept. The first save writes `SAVE_A.JSN` with the next generation. The `.BIN` files stay byte-for-byte identical. An interrupted first JSON write falls back to the `.BIN`. Once JSON exists, two damaged `.JSN` files block writes rather than falling back to the stale `.BIN`. The newest JSON generation wins in both slot orders. A newer-schema `.JSN` blocks writes.
  - The application, language, Kanban, daily review, reminders, Rosary and timer suites now go through the JSON save. The binary encoder survives only in `tests/save_helpers.h`, to build legacy files.
  - The CRC-32 written by the desktop build was checked against Python's `zlib.crc32`.
  - All ten host test suites pass with C++17 and warnings treated as errors. Code also compiles warning-free for 32-bit ARM (`-Wshorten-64-to-32`), but no UF2 was built: no Arm GNU toolchain on the build box.
- Pending on device: first start with an existing v1.12 card (migration), then a reboot (loads `.JSN`), save speed on the SD card, and power loss during a save.
- LPS-113-CHG08: default list of 22 activities.
  - Day activity flags widen from 16 to 32 bits (design capacity 30 for user-defined activities); binary saves still decode 16 bits, mapped to the first 16 names unchanged.
  - `language_test`, in English and French: home counter `0/22`, then `2/22`; pages `1/3`, `2/3` (items 11-20, including Sport, Cooking/Cuisine, Chores/Ménage, Family/Famille) and `3/3` with only Friends/Amis and Meditation/Méditation, rows 7-14 blank. Up/Down wrap within the two rows of the short page, Left/Right wrap across the three pages. Ticking Méditation and Amis survives a reboot from the JSON save, shows in the daily review, counts 2 on the Close-the-day screen, and is shown ticked, read-only, on page 3 of the archived day. All labels fit 40 columns with the accented display cells.
  - `json_save_test`: new activities round-trip by French name (`"Ménage"`, `"Méditation"`, `"Sport"`), and the full list in order; full and worst-case saves now use all 22 activities (8.9 KB and 18.4 KB).
  - `storage_test` (real adapter): ICS with `Activities: Walk, Sport, Meditation` and `Activités: Cuisine, Ménage, Famille, Amis`; the existing first-16 ICS and CSV assertions are unchanged.
  - `migration_test`: v1.2 and v1.12 BIN days keep their 16 activities.
  - All ten host test suites pass; also compiles warning-free for 32-bit ARM. No UF2 built.
  - LPS-113-CHG08b: `language_test`, in English and French: after leaving Activities on page 3, opening it again from Home shows page `1/3` with the cursor on Walk/Marche (Enter ticks it). From page 1, key 2 ticks Diet/Régime and stays on page 1. From page 3, keys 1 and 2 tick Friends/Amis and Meditation/Méditation; keys 3, 9 and 0 are ignored. From page 2, key 1 ticks Weekend (activity 11, not Walk) and key 0 ticks Family/Famille (activity 20). Opening Activities from an archived day also starts on page 1; on that archived day, keys 1, 2, 0 and Enter do not change the saved flags.
  - LPS-113-CHG08c: `language_test`, in English and French: page 1 shows `1  Walk`/`1  Marche` through `0  Day off`/`0  Congé`; page 2 shows `1  Weekend`, `2  Outing`/`2  Sortie`, `7  Sport`, `8  Cooking`/`8  Cuisine`, `9  Chores`/`9  Ménage` and `0  Family`/`0  Famille`; page 3 shows `1  Friends`/`1  Amis` and `2  Meditation`/`2  Méditation` (also when the archived day is viewed). The prefix matches the home menu (`digit`, two spaces, name). Every checkbox on page 1 sits in column 28.
  - LPS-113-CHG07: new `tests/activities_test.cpp` (English and French). Rename applies to the current language only (Walk → Stroll leaves Marche; the French screen still shows Marche). An activity created in French as Piano is shown as Piano in English (no blank, no '?'). Empty Enter cancels; surrounding spaces are trimmed; a case-insensitive duplicate in the current language is refused (`  stroll  `). Today's tick stays on an activity that is renamed or moved with F5/F4. Del shows `Delete Stroll?` / Enter yes, Esc cancels, and a confirmed delete unticks it today. After closing a day that ticked Walk and Sport, renaming Marche to Promenade and deleting Sport, the archived day still lists Marche and Sport as checked extras and the home counter reads `2/21`; E/N/Del/F4/F5/Enter do not change that archived day. ICS export names for that day stay `Marche` and `Sport` (frozen); a live tick of the renamed activity still exports as `Walk` in English. Adding up to 30 activities, refusing a 31st, deleting the last page returning to `2/2`, and an empty list showing `N: add an activity` / `0/0 activities` are covered. A v1 JSON save without a catalog, and a v6 BIN payload, both load the default 22. After save/load, renaming Marche to Promenade in French keeps today's tick and stores the new identity name. JSON schema is 2; v1 files still load; a higher version still blocks writes. Typical full save is now 9775 bytes and worst-case 19321 bytes (the catalog adds about 1 KB). All eleven host test suites pass with C++17 and warnings treated as errors. Code also compiles warning-free for 32-bit ARM (`-Wshorten-64-to-32`), but no UF2 was built: no Arm GNU toolchain on the build box.
  - LPS-113-CHG07b: Activities F4 on the first item moves it to the end (page 3, `2  Walk`/`2  Marche`, tick follows); F5 on the last item moves it to the top; the cursor and page follow. Adjacent F5/F4 still swap with the neighbour. English and French cases are in `activities_test`. Large `State` copies are no longer made on the stack: App mutations use a BSS `draft` scratch (`stage()`), BIN `decode` writes into its output, and Pico BIN slot checks reuse `PicoCalc::scratch`. `sizeof(State)` remains 30288 bytes. Firmware BSS adds one extra State (~30 KB) beside `App::state` and `PicoCalc::scratch` (three States + the 16 KB CSV buffer ≈ 107 KB of the 192 KB region). Worst-case stack drops from two stacked States during BIN migration (~60 KB, enough to overflow the 64 KB stack) to the two ~5 KB `Record` envelopes plus small I/O/render buffers. All eleven host test suites pass; also compiles warning-free for 32-bit ARM. No UF2 built.

# Validation v1.12 — 8 November 2026

- LPS-112-CHG04: the title bar reads `LPS COMPANION v1.12`, asserted in both languages.

- LPS-112-CHG01: timer tests cover keys 1-3 starting each duration from the selection screen, restarting a new duration from the completion screen, and being ignored while a countdown runs.
- LPS-112-CHG02: timer tests cover 5/30/60-minute durations (initial 05:00, 30:00, 60:00), normal colour at 01:00, red at 00:59, pause/resume and completion for each.
- LPS-112-CHG03: language tests assert that no screen row contains "XP" (Home, Close the day, Day saved, EN and FR), that keepsakes are no longer announced, and that line 1 shows the long date with ISO week in both languages. The ISO week computation was checked against Python's calendar for every date from 2000-01-01 to 2099-12-31 (36,525 days, no mismatch). ICS exports keep an empty `XP:` line; lps-console ignores it.
- LPS-112-CHG05: application and language tests assert the renumbered home menu (0 Date … 8 Timer, 9 Close the day, reachable with a single key), the two-entry archived-day menu (1 Activities, 2 Weight tracker), read-only rejection of keys 0 and 3-9 on archived days, and that no row shows Notepad/Bloc notes in either language. All suites using home shortcuts were updated. The real adapter test still exports a legacy note to the ICS `Note:` line.
- All nine host test suites pass with C++17 and warnings treated as errors.

# Validation v1.11 — 2 October 2026

- All nine host test suites pass with C++17 and warnings treated as errors.
- Timer tests cover 5/15/30-minute durations, normal colour at 01:00, red at 00:59, pause/resume, completion, cancellation, no save writes and item 10 navigation.
- The actual PicoCalc hardware adapter compiles against the existing host hardware/FatFs stubs.
- The terminal desktop build compiles with warnings treated as errors and now ticks while waiting for input.
- Countdown on PicoCalc uses a 4× scaled bitmap font, centred at 160×64 pixels, with palette foreground or red.
- ARM toolchain unavailable in this environment: no UF2 generated. Physical display, timing and keyboard operation still require PicoCalc testing.

## Earlier validation (historical)

# Build validation — v1.8, 22 September 2026

Completed:

- All six host test suites pass for v1.8: application, language, Kanban, Rosary, storage and migration.
- The dedicated Rosary suite covers all four modes, date-based Thursday/Luminous selection, manual decade choice, prayer pagination, Scripture, meditation, contemplative silence and successful completion.
- English and French Rosary menus are rendered under the same 40-column bounds checks as the rest of the application. Uppercase È support was added to the PicoCalc accent compositor.
- The dedicated Kanban suite covers creation, editing, deletion, circular column navigation, circular selection, F1–F3 state moves, F4/F5 reordering, failed-save retry/rollback and restart persistence.
- Official PicoCalc key codes are mapped for Del and F1–F5 and covered by the hardware-adapter test.
- Date parser, 2000–2099 range, normal rollover, leap-day rollover, year rollover and final-date boundary tested.
- The real PicoCalc/FatFs adapter wrote and read an English leap-day ICS export. Its DTSTART/DTEND, activities and XP fields were verified. French activities, weights and escaped commas/semicolons/backslashes in notes were also checked.
- Export write failure removes the incomplete file, does not advance the day or save a new A/B generation, and a later retry succeeds.
- Closing a dated day writes `LPS/WEIGHTS/YYYY.csv` with expected and measured kilograms. The header, blank optional fields, a second date in the same year and duplicate-date protection are covered by the real adapter test.
- English default; exact EN/FR home labels; language selection by arrows and shortcuts; Save, Cancel, failed-save rollback and language persistence after restart tested.
- Palette labels are tested in their selected language. A changed palette invalidates the cached LCD frame, forcing all 20 lines to redraw immediately.
- Missing SD storage is distinguished from a damaged save and produces a clear startup warning with writes blocked. Menu-row bounds and RFC 5545 ICS `\\N` line breaks are covered by host tests.
- A below-20% battery condition is tested through the same bold-red alert path as the missing-SD warning.
- The actual PicoCalc adapter renders the missing-SD warning in red with an overdrawn glyph for bold weight; activity pagination is tested with ten rows on page one and six rows on page two.
- Menus, activity labels, notepad, weight fields, instructions, errors, day closure, rewards and read-only history tested in both languages. Display-cell width assertions check text fits the 40-column screen.
- Screens rendered with the actual font and firmware drawing adapter were visually checked: English/French home, language picker and French activities, including accents.
- Application tests: activity, weight, note, XP and save checks; archived note text, weights, difference and checked Weekend displayed from the selected day; return to current-day values.
- Storage integration tests compile the actual src/main.cpp A/B adapter against host-only FatFs/hardware stand-ins. They write ordinary temporary files, not an SD card.
- Forty distinct daily records saved across alternating A/B snapshots, then loaded through a fresh adapter/application instance.
- All 31 retained completed days retrieved correctly after the circular history wraps; oldest boundary and direct return to today checked.
- Read-only history rejects edits and closing a day, leaves XP unchanged and preserves both save files byte-for-byte. Editing today still saves and survives a fresh load.
- Damaged newest snapshot falls back to the older valid snapshot; missing newest file also falls back. Invalid existing records and simulated read errors block writes.
- Format 5 adds the global 24-task Kanban. Formats 1–4 remain accepted with an empty Kanban; v1–v2 dates remain deliberately unset and palettes default to green when absent.
- Actual adapter tested with mixed old/new A/B snapshots in both slot orders; damaged first v1.3 write falls back to the original v1.2 save with English as default. Both slots subsequently upgrade safely. Invalid language values with recomputed CRC are rejected.
- v1.2 was confirmed working on a physical PicoCalc by the user.

Pending:

- v1.8 physical PicoCalc test, including Rosary navigation/readability/timing, battery-threshold calibration, the low-battery warning, Kanban keys and persistence, Reading/Lecture and Shopping activities, missing-SD warning, compact menus, corrected ICS line breaks, palette preview, annual CSV export, actual SD-card behavior and power-loss conditions.
- v1.8 ARM/UF2 build and UF2 structural verification. The current execution environment may not provide CMake or the Arm GNU toolchain.
- UF2 launcher compatibility; this remains a standalone BOOTSEL build.

No prebuilt UF2 is included for v1.8 unless `build.sh` succeeds. `test.sh` runs six host test suites (application, language, Kanban, Rosary, storage and migration). The tests/stubs headers are never included in the Pico firmware build. Dependencies remain pinned and downloaded separately by build.sh.

## v1.9 menu update

Main menu: 5 Rosary, 6 Daily review (date, selected activities, actual weight, TODO and DOING with scrolling), 7 Config, 8 Close the day. Host tests pass; device validation remains pending.

## v1.10 reminders

Host checks cover three slots, popup on each startup, inclusive dates over leap day, expired-record deletion, failed writes, and v5-to-v6 decoding. Hardware and PicoCalc keyboard mapping remain to be checked on device.

## v1.10.1 palettes and helper text

Host checks verify English/French palette labels, Orange/Pink preview and persistence, and emphasized key spans in both languages. Actual LCD appearance awaits device testing.
