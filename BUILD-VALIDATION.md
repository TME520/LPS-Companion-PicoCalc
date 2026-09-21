# Build validation — v1.7, 20 September 2026

Completed:

- All five host test suites pass for v1.7: application, language, Kanban, storage and migration.
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

- v1.7 physical PicoCalc test, including battery-threshold calibration, the low-battery warning, Kanban keys and persistence, the new Reading/Lecture and Shopping activities, missing-SD warning, compact menus, corrected ICS line breaks, palette preview, annual CSV export, actual SD-card behavior and power-loss conditions.
- v1.7 ARM/UF2 build and UF2 structural verification. The current execution environment does not provide CMake or the Arm GNU toolchain.
- UF2 launcher compatibility; this remains a standalone BOOTSEL build.

No prebuilt UF2 is included for v1.7. `test.sh` runs five host test suites (application, language, Kanban, storage and migration). The tests/stubs headers are never included in the Pico firmware build. Dependencies remain pinned and downloaded separately by build.sh.
