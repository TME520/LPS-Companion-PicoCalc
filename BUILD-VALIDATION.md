# Build validation — v1.2, 16 September 2026

Completed:

- ARM Cortex-M0+ build with Arm GNU Toolchain 14.2.Rel1 and Pico SDK 2.2.0.
- UF2 verified: 527 blocks, 269,824 bytes, RP2040 family and valid flash addresses.
- Application tests: activity, weight, note, XP and save checks; archived note text, weights, difference and checked Weekend displayed from the selected day; return to current-day values.
- Storage integration tests compile the actual src/main.cpp A/B adapter against host-only FatFs/hardware stand-ins. They write ordinary temporary files, not an SD card.
- Forty distinct daily records saved across alternating A/B snapshots, then loaded through a fresh adapter/application instance.
- All 31 retained completed days retrieved correctly after the circular history wraps; oldest boundary and direct return to today checked.
- Read-only history rejects edits and closing a day, leaves XP unchanged and preserves both save files byte-for-byte. Editing today still saves and survives a fresh load.
- Damaged newest snapshot falls back to the older valid snapshot; missing newest file also falls back. Invalid existing records and simulated read errors block writes.
- Existing wire format and hardware adapter are unchanged, preserving v1.0/v1.1 save compatibility.

Pending:

- v1.2 physical PicoCalc test, including actual SD-card behavior and power-loss conditions.
- UF2 launcher compatibility; this remains a standalone BOOTSEL build.

The prebuilt UF2 matches the included source. test.sh runs both host test suites. The tests/stubs headers are never included in the Pico firmware build. Dependencies remain pinned and downloaded separately by build.sh.
