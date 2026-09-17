# Build validation — v1.5, 17 September 2026

Completed:

- ARM Cortex-M0+ build with Arm GNU Toolchain 14.2.Rel1 and Pico SDK 2.2.0.
- UF2 verified: 601 blocks, 307,712 bytes, RP2040 family and valid flash addresses.
- Date parser, 2000–2099 range, normal rollover, leap-day rollover, year rollover and final-date boundary tested.
- The real PicoCalc/FatFs adapter wrote and read an English leap-day ICS export. Its DTSTART/DTEND, activities and XP fields were verified. French activities, weights and escaped commas/semicolons/backslashes in notes were also checked.
- Export write failure removes the incomplete file, does not advance the day or save a new A/B generation, and a later retry succeeds.
- English default; exact EN/FR home labels; language selection by arrows and shortcuts; Save, Cancel, failed-save rollback and language persistence after restart tested.
- Palette labels are tested in their selected language. A changed palette invalidates the cached LCD frame, forcing all 20 lines to redraw immediately.
- Menus, activity labels, notepad, weight fields, instructions, errors, day closure, rewards and read-only history tested in both languages. Display-cell width assertions check text fits the 40-column screen.
- Screens rendered with the actual font and firmware drawing adapter were visually checked: English/French home, language picker and French activities, including accents.
- Application tests: activity, weight, note, XP and save checks; archived note text, weights, difference and checked Weekend displayed from the selected day; return to current-day values.
- Storage integration tests compile the actual src/main.cpp A/B adapter against host-only FatFs/hardware stand-ins. They write ordinary temporary files, not an SD card.
- Forty distinct daily records saved across alternating A/B snapshots, then loaded through a fresh adapter/application instance.
- All 31 retained completed days retrieved correctly after the circular history wraps; oldest boundary and direct return to today checked.
- Read-only history rejects edits and closing a day, leaves XP unchanged and preserves both save files byte-for-byte. Editing today still saves and survives a fresh load.
- Damaged newest snapshot falls back to the older valid snapshot; missing newest file also falls back. Invalid existing records and simulated read errors block writes.
- Format 4 adds one palette byte. Formats 1–3 remain accepted; v1–v2 dates remain deliberately unset and v1–v4 palettes default to green when absent.
- Actual adapter tested with mixed old/new A/B snapshots in both slot orders; damaged first v1.3 write falls back to the original v1.2 save with English as default. Both slots subsequently upgrade safely. Invalid language values with recomputed CRC are rejected.
- v1.2 was confirmed working on a physical PicoCalc by the user.

Pending:

- v1.5 physical PicoCalc test, including live palette preview, persistence after restart, actual SD-card behavior and power-loss conditions.
- UF2 launcher compatibility; this remains a standalone BOOTSEL build.

The prebuilt UF2 matches the included source. test.sh runs four host test suites (application, language, storage and migration). The tests/stubs headers are never included in the Pico firmware build. Dependencies remain pinned and downloaded separately by build.sh.
