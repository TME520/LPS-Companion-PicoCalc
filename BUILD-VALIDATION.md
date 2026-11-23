# Validation v1.16 — 23 November 2026

- LPSC116-CHG07: title bar `LPS COMPANION v1.16`; header comment v1.16 (2026-11-23); README title, status, `Changes in v1.16`, usage (Daily Review, Kanban **F**, archive menu 1–3) and a short PicoCalc vs DOS note; `language_test` and `daily_review_test` assert the new title. `./test.sh` fully green (all fourteen host test suites). `sizeof(State)` remains 30332. Typical full save 9815 bytes, worst-case 19435. No UF2 built: `./build.sh` stops at the tool check (`cmake`, `ninja`, `c++`, `arm-none-eabi-gcc` and `arm-none-eabi-g++` are missing on the box; `git` and `python3` are present). Title previews: `/workspace/lps/previews/lpsc116_chg07_*.png`.

- PicoCalc vs DOS v1.16, documented in the README: **Next:** / **Prochain :** on two lines (user decision, 40 columns); 20-row layout (review page of 16 lines on rows 1–16, messages on 17, footer on 19); French range footer with single spaces so the longest form is 40 columns; Home/End/PgUp/PgDn as Shift+Tab/Del/Up/Down (`0xD2`/`0xD5`/`0xD6`/`0xD7`); DOS CHG07b fast-scroll is not ported.

- LPSC116-FEAT01-CHG01/CHG02: **F** toggles Kanban focus (`*` before the name); DONE clears it. JSON writes `"focus":true` only on focused cards; an unfocused save stays byte-identical to v1.15. Schema version stays 2.

- LPSC116-FEAT02-CHG03: Daily Review opens with the full date and ISO week in the language in use.

- LPSC116-FEAT02-CHG04: new review layout (weight, focused cards, blank line before the meal title, two-line Next, activities).

- LPSC116-FEAT02-CHG05: 16-line window, Up/Down one line with no wrap, Left/Right or PgUp/PgDn one page, Home/End to the ends, drawn arrows at column 39, range footer on line 19, offset kept through Tab/Space.

- LPSC116-FEAT02-CHG06: archived-day menu adds **3 Daily review** / **3 Revue journalière**; that review is read-only (no KANBAN, meal lines `--`); Esc restores the cursor on entry 3; Tab/Space show `Archived day: read-only` / `Jour archivé : lecture seule`.

# Validation v1.15 — 15 November 2026

- LPS-115-CHG06: title bar `LPS COMPANION v1.15`; header comment v1.15 (2026-11-15); README title, status and `Changes in v1.15`; language_test asserts the new title. All fourteen host test suites pass; compiles warning-free for 32-bit ARM (`-Wshorten-64-to-32`). No UF2 built. Previews regenerated (`/workspace/lps/previews/115_*.png`).

- LPS-115-CHG01: Tab opens/closes Config.
  - Home menu `HomeScreens[]` (9 entries, static_assert against the labels): 0 Date, 1 Activities, 2 Weight tracker, 3 Kanban, 4 Christian Rosary, 5 Daily review, 6 Reminders, 7 Timer, 8 Close the day. `language_test` checks rows 12–14 in EN/FR (`6  Rappels`, `7  Minuteur`, `8  Terminer le jour`), no `Config` row and no `9` entry, and that key `9` leaves Home unchanged.
  - Home footer `Up/Down  Enter:OK  Tab:Config` (FR `Haut/Bas  Entrée:OK  Tab:Config`), with `Tab` emphasized and `Config` plain; the Config screen shows `Tab: close Config` / `Tab : fermer la configuration`. All rows still fit 40 cells.
  - Return target: `openConfig()` records the screen and cursor it was opened from (`configReturn`, `configReturnSelection`); `closeConfig()` goes back there. Tests: Tab from Home with the cursor on Weight opens Config, Tab again returns to Home with Weight still highlighted; Tab from Language and from Colors closes Config entirely (the Colors preview is dropped, palette unchanged, no write); a pending language choice is dropped by Tab; Esc from Colors returns to Config and Esc from Config returns to Home with the cursor restored. Saving Config/Colors returns to Home. Tab on an archived day shows `Archived day: read-only` (`meal_planner_test`).
  - Tab reaches the app: the PicoCalc `keymap()` already passes code 9; only Home and the Config screens react to it, and the text editors accept only 32–126, so Tab is ignored there. Desktop build: new `tab` command.
  - Tests updated for the renumbering (Reminders 6, Timer 7, Close the day 8, Config via Tab) in the activities, language, meal planner, migration, reminders, storage and timer suites, plus the built-in `LPS_TEST` run.
  - Firmware object for 32-bit ARM (Cortex-M0+, `-Os`): +186 bytes total; BSS +8 bytes (return screen and cursor).
  - All twelve host test suites pass; compiles warning-free for 32-bit ARM (`-Wshorten-64-to-32`). No UF2 built.
- LPS-115-CHG02: Tab and Space from any screen.
  - One return point per overlay (`configReturn`, `mealReturn`: screen + cursor), no stack. `returnTo()` restores the screen without `go()`, so drafts, Activities page, Kanban column, review scroll, Rosary step/page and the Meal Planner card are kept; the Rosary page and review scroll are clamped in case a newly saved language shortened them.
  - Screen audit. Opens Config/Meal Planner: Home, Activities (list), Kanban, Daily review, Reminders (list), Timer select/running/finished, Rosary mode/set/decade/prayer/complete, Reward, Meal Planner (Tab only). Keeps its own keys: Date, Weight, Activity name editor, Kanban task editor, Reminder editor (text entry), Activities `Delete X?` and Close the day (confirmations), reminder popup (any key dismisses). Only conflict found: Weight already used Tab (code 9) to switch Expected/Actual; kept in CHG02, replaced by Config in CHG02b. No screen used Space other than the text editors and the Meal Planner.
  - Cross-open: Tab in the planner opens Config, which closes back to the planner; Space is ignored in Config, Language and Colors.
  - Timer: `tick()` now runs while a countdown is active on any screen (`timerActive`), not only on the timer screen; Esc on the running timer clears it (cancel, as before).
  - New `tests/overlay_keys_test.cpp` (13th suite): Tab/Esc/Tab-from-Language and Space/Esc round trips return the identical frame on 14 screens (Home, Kanban Doing column, Activities page 2, Daily review, Reminders, Timer select, Rosary mode/set/decade/prayer/complete, Reward); Space typed and Tab ignored in the task, activity and reminder editors and Date; Weight Tab switches field; Delete confirmation, Close the day and reminder popup; planner ⇄ Config cross-open, Space ignored in Config/Colors, Esc one level at a time; CSV written on every planner exit and its failure shown on Kanban; Config save returns to Kanban in French; archive Home and Activities read-only; timer counts behind Config and the planner (04:00, 02:59), stays paused, expires into `TIMER FINISHED!` from the planner (CSV written), from Config (pending language dropped, no save) and from Config over the planner (CSV written); Esc cancels; reminder popup at the next start-up. `meal_planner_test` (Space on Activities now opens the planner) and `storage_test` (leaves the Reward screen first) were adjusted.
  - Firmware object for 32-bit ARM: +376 bytes total over CHG01; BSS +16 bytes.
  - All thirteen host test suites pass; compiles warning-free for 32-bit ARM (`-Wshorten-64-to-32`). No UF2 built.
- LPS-115-CHG02b: Tab opens Config from Weight.
  - Weight drops Tab (code 9) as its Expected/Actual switch; Up/Down still switch and Enter still validates both fields and returns Home (the on-screen hint never mentioned Tab: `Up/Down: field - Enter: save`). Tab on Weight now opens Config; Space stays ignored (not an overlay key there).
  - In-progress input is kept: the return uses `returnTo()` (no `go()`), so the selected field and both unsaved drafts survive Config, including a language save. `overlay_keys_test`: with `1` in Expected and `2.` in Actual (Actual selected), Space changes nothing; Tab/Tab, Tab → Language → Esc/Esc and Tab → Colors → Tab all return the identical frame; typing `5` then Enter saves 1.000 / 2.500 kg; a French save from Config returns to Weight with the draft `9` kept and saves 9.000. Archived Weight: Tab shows `Archived day: read-only`.
  - Firmware size unchanged within a few bytes; all thirteen host suites pass; ARM check warning-free. No UF2 built.
- LPS-115-CHG03: Config menu entries sub-screen.
  - Config is now 1 Language, 2 Colors, 3 Activities (`3  Activities: n/9` / `3  Activités: n/9`), 4 Save (Save moved from 3 to 4; every test that saved Config with `3` now uses `4`). Hint `Language, Activities: then Save.` / `Langue, Activités : puis enregistrer.`
  - New `Screen::HomeEntries` (in Config: Tab closes Config entirely, Esc goes to Configuration, Space ignored). Title `ACTIVITIES` / `ACTIVITÉS`, subtitle `Home menu entries shown` / `Entrées affichées dans le menu`, nine rows `[x] name` / `[ ] name` from a shared `homeLabel()` (the home menu now uses it too, output unchanged), Date and Close the day right-tagged `(locked)` / `(verrouillé)`; every row fits 40 cells in both languages.
  - Save model: same as Language. Toggles change `pendingHidden`; Config's Save writes `language` and `State::hiddenHome` (bit i = home entry i, locked bits can never be set) in one commit; Tab, Esc from Configuration, timer expiry and reopening reset the pending value from State. A failed save keeps the pending choice.
  - Scope: `hiddenHome` lives in `State` only (no size change: padding). The home menu is not filtered yet (CHG04) and the JSON save does not carry it yet (CHG05).
  - New `tests/home_entries_test.cpp` (14th suite): entry and count, nine checkboxes, Enter on Date and Close the day shows `Always shown` and changes nothing, Up wraps, double toggle, Space ignored, pending until Save (state unchanged, no write), Esc keeps pending and shows 8/9, Save returns to the opening screen with `hiddenHome=0x002`, home menu unchanged, Tab drops pending from the sub-screen and returns to Kanban, hide all hideable entries together with a French language save (`0x0fc`), failed save keeps pending and state, timer expiry drops pending. `language_test` checks Config rows and the sub-screen in EN/FR.
  - Firmware object for 32-bit ARM: +594 bytes over CHG02b; BSS unchanged. All fourteen host suites pass; ARM check warning-free. No UF2 built.
  - Previews: `/workspace/lps/previews/115_config_menu_config.png`, `115_config_menu.png` (EN), `115_config_menu_fr.png` (FR), with @2x copies.
- LPS-115-CHG04: home menu shows only checked entries.
  - Home `selection` is now a position among the shown entries: `homeVisibleCount()`, `homeEntryAt(position)` and `homePositionOf(entry)` (a hidden entry maps to the next shown one below it; Close the day is locked, so one always exists). The mask is sanitised (`HomeAll & ~HomeLocked`). Rendering, digits, Up/Down and Enter use these; the archive menu (1 Activities, 2 Weight tracker) is untouched.
  - Return points (`ReturnPoint`) on today's Home also record the entry, so `returnTo()` after Config (directly or via the Meal Planner) puts the cursor back on the same entry, or on the next shown one if it was hidden. Every other `go(Home)` already starts on Date (position 0, always shown).
  - No other code depended on fixed home indices (the Daily review, exports, reminders and timer never read the menu position); the built-in `LPS_TEST` run and all suites pass unchanged with every entry shown.
  - `home_entries_test` additions: the spec example (Activities and Daily review hidden) renders exactly `0  Date` … `6  Close the day` with row 13 empty and the `<- Previous day`, `Tab:Config` and `1/22 activities` lines in place; `1`, `5`, `6` open Weight, Timer, Close the day, `7`–`9` do nothing; Up from Date lands on Close the day and Down wraps; cursor stays on Kanban when Weight is hidden, moves to Christian Rosary when Kanban is hidden, to Close the day when Timer is hidden, and stays put when all are shown again; Reminders hidden from Config opened over the Meal Planner returns Home on Timer; with Timer hidden, `7` is Close the day in French; with Activities hidden, an activity ticked before still closes the day and shows in the archive (`1  Activities`, `2  Weight tracker`); a running timer completes after its entry is hidden.
  - Firmware object for 32-bit ARM: +64 bytes over CHG03; BSS +8 bytes (entry in the two return points). All fourteen host suites pass; ARM check warning-free. No UF2 built.
  - Previews: `/workspace/lps/previews/115_home_filtered.png` (EN) and `115_home_filtered_fr.png` (FR), Activities and Daily review hidden, cursor on Kanban; @2x copies.
- LPS-115-CHG05: save home menu entries.
  - `"homeHidden"` is written at the end of `state`, after `"meals"`, as an array of stable ids from `HomeEntryIds` (`date`, `activities`, `weight`, `kanban`, `rosary`, `daily_review`, `reminders`, `timer`, `close_day`), in menu order, locked ids never written: e.g. `"meals":{"played":[]},\n"homeHidden":["activities","daily_review"]},\n"crc32":"…"`. Always written (`[]` when everything is shown); `crc32` stays last; schema stays 2.
  - Reading: key absent (v1.14 and older) = all shown; unknown, overlong or differently-cased ids ignored; `date`/`close_day` ignored; duplicates harmless; a non-array or a non-string element (number, null, array) is damage, like other malformed keys. `resetState` clears the mask.
  - Written only through the existing Config Save (language + entries in one commit); a failed save leaves the old file, the old state and the pending choice.
  - `home_entries_test` (real JSON store): first save holds `"homeHidden":[]` right before `crc32`; hiding Activities and Daily review writes `["activities","daily_review"]` and a restart shows `0  Date` … `6  Close the day`; failed save keeps file/state/pending, the retry writes `["daily_review"]` and the next restart shows it; a save with the key removed loads all 9 entries; all seven ids → `0x0fe`; unknown/upper-case/empty/300-char ids ignored; locked ids ignored; `"activities"`, `{}`, `[1]`, `[null]`, `[["kanban"]]` are damage; a RAM mask with locked bits writes only the seven hideable ids. `json_save_test` worst case now hides every entry (19 435 bytes) and `sameState` compares the mask.
  - Firmware object for 32-bit ARM: +446 bytes over CHG04; BSS unchanged. Full save +17 bytes. All fourteen host suites pass; ARM check warning-free. No UF2 built.
- Pending on device: Tab and Space reach the app on the PicoCalc keyboard from every screen; the timer keeps running behind Config and the Meal Planner.

# Validation v1.14 — 14 November 2026

- LPS-114-CHG03: title bar `LPS COMPANION v1.14`; header comment v1.14; language_test asserts the new title. All twelve host test suites pass; compiles warning-free for 32-bit ARM (`-Wshorten-64-to-32`). No UF2 built.

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
- LPS-114-CHG01b: Meal Planner sprites from the user's cards.
  - `tools/meal_sprites.py` reads `assets/meal_deck_sheet1.png` and `assets/meal_deck_sheet2.png` (the user's two "Daily Edible Deck" sheets, committed as-is: 2.9 MB). For each of the 13 distinct cards it crops the illustration above the name (with steam, hearts and motion lines), flood-fills the near-white background from the crop edges to transparency (white cups and bowls stay opaque), fits it into 88 × 64 with a premultiplied box filter, and quantizes it to 15 colours (median cut for 14, plus one for the worst-represented detail, usually the pink heart) with one 16-colour palette per sprite. It samples each card's border and ribbon colours from the sheet. Blank uses a taller crop of the user's Blank design (grey ribbon and burst lines). The output is deterministic (re-running gives the same header).
  - Card drawing: the sprite is drawn ×2 (176 × 128), with a per-card border/header colour and the portion on a notched ribbon in the card's ribbon colour. The Blank card centres its art and has no name or portion below it. Played cards stay entirely grey.
  - `meal_planner_test`: the border is the apple card's colour (mint tea's for the French drink); the apple illustration puts red in the card; the portion background is the card's ribbon colour; the Blank card draws only its header label and has its grey border; everything stays inside the card; played is all grey. Sprite data is 13 × 2816 bytes.
  - Memory: BSS unchanged. Flash for the sprites is about 37.5 KB (pixels 36.6 KB, palettes, border and ribbon colours), about +31 KB over CHG01 (the ARM object's `.rodata` is 57.7 KB, up from 26.9 KB). The render path's row buffer grows to 704 bytes of stack. The adapter's 576-byte row buffer holds the 176-pixel rows.
  - All twelve host test suites pass; compiles warning-free for 32-bit ARM (`-Wshorten-64-to-32`). No UF2 built.
- LPS-114-CHG02: Meal Planner CSV export.
  - `meal_planner_test`: `mealCsvName` gives `MP261114.CSV`, `MP000105.CSV` and `MP991231.CSV`. `writeMealCsv` writes the header `order,category,card,portion` and then the played cards in order of play, with English names (`1,Drinks,Mint Tea,1 cup`, `3,Blank,Blank,`). Putting a card back and playing it again moves it to the end. RFC 4180 quoting (`"a,b"`, doubled quotes, line breaks) is tested, UTF-8 passes through, and a sink failure is reported. Through the storage interface: entering the planner exports nothing. Leaving with Space or Esc passes today's date and deck to the export, including an empty deck. A failed export leaves the screen on Home with `Meal CSV export failed` (or `Export CSV repas échoué`), the deck still saved, and the next exit retries. Closing the day does not export. With a missing SD card no export is attempted.
  - `storage_test` (real `src/main.cpp` adapter on host files), on the 2028-03-02 day: leaving with nothing played creates no `MP280302.CSV`. Latte, salad and Blank give the exact three CRLF rows. Putting the latte back and leaving rewrites the file with two rows. A write error removes the file and keeps the deck, and the next exit writes it again. Putting everything back resets the existing file to its header. The adapter refuses an invalid date and creates no file for an empty deck on a new day.
  - The file is `LPS/EXPORT/MPYYMMDD.CSV` (8.3 name), rewritten whole each time with `FA_CREATE_ALWAYS`, `f_sync` and `f_close`; a failure unlinks it, as the ICS export does.
  - Flash about +0.8 KB of code; BSS unchanged; writing goes straight through FatFs with no extra buffer.
  - All twelve host test suites pass; compiles warning-free for 32-bit ARM (`-Wshorten-64-to-32`). No UF2 built.
- Pending on device: Space key reaches the app; MPYYMMDD.CSV appears in `LPS/EXPORT` and opens in a spreadsheet; card colours and sprite look on the real LCD; drawing speed of the card; save time per play.

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
