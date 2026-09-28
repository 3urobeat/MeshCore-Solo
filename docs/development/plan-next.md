# Plan: from here to the merge into dev (2026-09-28)

Work in order; each item is implemented, checked in the sim, flashed, and
committed only after it's accepted. Tick items off as they land. Working
file: goes in the repo clean-up (stage I).

## A. Bugs

- [x] **A1. Restart on a screenshot + "340 B stack free".** Likely one cause.
      Diagnostics shows the UI loop task's lowest-ever stack headroom
      (`uxTaskGetStackHighWaterMark`); the Arduino loop stack is 8 KB, so
      340 B left is nearly an overflow. `takeScreenshot()` keeps a 960 B line
      buffer on that stack, plus the FAT write.
      1. Confirm with Diagnostics > Last crash after a screenshot restart.
      2. Move the line buffer off the stack.
      3. Raise the loop stack (internal RAM).
      4. Measure the headroom on heavy screens (map, history, OTA).
      5. Label the Diagnostics row with the task it measures.
      Done 2026-09-28: loop stack 16 KB (SET_LOOP_TASK_STACK_SIZE), line
      buffer static, row "UI stack free (lowest)". Measured 8532 B free
      after map, history and screenshots, so the UI peaks at ~7.8 KB --
      the old 8 KB left 340 B. Could go down to 12 KB if RAM gets tight.
- [x] **A2. USB popup before the splash screen is gone.** `usbPoll()` waits
      for the splash to finish; a cable plugged in at boot shows the popup
      right after it.

## B. Map

- [x] **B1. Map menu order.** List the current entries, propose groups and
      an order (view, overlays, areas & downloads, trails, test), agree on
      it before coding. Done: TRAIL / LIVE SHARE / ARRIVAL ALERT (each with
      its own options page, one section of PG_NAV), OFFLINE MAPS (Map areas,
      Live tiles; "Download an area" dropped, a running / unfinished
      download shows at the top of Map areas), LAYERS.
- [x] **B2. Downloaded areas.** Selecting an area previews it at once (no
      Show button); leaving the menu hides it and restores the previous map
      view. Fixes the area staying on the map after Show.

## C. Diagnostics: the noise tests (to think over)

Diagnostics stays as it is. Only the Noise tab is in question: the tests that
found the interference source (noise floor with the board's parts off one by
one, the 850-930 MHz and mesh-channel sweeps, the spike hunt, the 15 s states
for a second radio). They are L2-specific and block the loop for ~90 s.

- [x] Decide: remove them, or keep one universal tool (for example a noise
      floor sweep round the mesh frequency that works on any board) and drop
      the L2 part toggling and spike hunt. Done: one universal Measure (the
      floor on the mesh frequency + a +-1.1 MHz sweep, ~12 s, a legend under
      it); the L2-only tests are gone. Left for stage F: the L2 board's
      setGrovePower / setSdPower, now unused.

## D. Quiet hours in the core

- [x] A start and end time when the device is silent; in ui-core, so L1, L2
      and other boards get it. New NodePrefs fields go at the end of the
      stored layout, so an L1 keeps its settings after the update.
      To decide: the Clock alarm rings anyway (proposed yes); whether a
      message wakes the screen during quiet hours; with the clock not set,
      quiet hours are off (proposed).
      Done 2026-09-28: NodePrefs quiet_hours / quiet_from / quiet_to
      (sentinel 0x30, sizeof 2832), off by default, 22-7; the alarm rings, a
      message doesn't wake the screen, a manual mute / unmute stands until
      the window ends. hourInWindow() / localHour() shared with the bot's
      quiet hours. L2 tested; L1 built, not flashed yet.

## E. OTA end-to-end test

- [ ] After the feature changes: a real release with an L2 asset (the v3 env
      must be the `*_solo_lvgl` one before tagging), install over WiFi.
      Publishing the release needs confirmation first.

## F. The big review

- [x] Dead code; similar elements written several times, merged into one;
      places to speed things up or save RAM, flash and battery.
      First a list of findings to accept, then fixes in batches, each checked
      in the sim and on the device.
      Done 2026-09-28 (-265 lines): dead code (anim::fadeHide,
      presetCount, the L2 board's setSdPower / setGrovePower / expanderOK);
      one buttonBar() / barButton() for 7 copies of the button row;
      confirmBody() for 5 red-button popups; tapConfirmed() for 11
      "tap again" buttons, all 3 s and the label restored; noteLabel() for
      ~30 wrapped notes; localTm() in NodePrefs.h and one MONTHS table
      (lvgl, MsgExpand, the L1 clock); the sim keeps UI settings in an
      in-memory nvs::, one set of load / save helpers; asleep, the loop
      wakes every 50 ms, not 20. Left: map tables to PSRAM only if the
      internal heap gets tight; emoji 1.25 MB of flash (fits).

## G. Core parity with L1 SOLO

- [x] A feature table: L1 SOLO / ui-core / L2 (ui-lvgl). What's missing goes
      into ui-core, not into each UI separately.
      Done 2026-09-28: L1 sends through UiCore::sendDirectText /
      sendChannelText; ui-core/Telemetry.h (dashboard fields, sensor
      readings) for both; CLI rescue on L2 (side button held in the first
      8 s); L1's repeater radio through rptctl; L1 Settings built from
      SettingsSchema (short labels, SCHEMA_* placeholders per section; 20
      hand-written rows gone). Kept per device: Noise measure (L2), screen
      PIN (L2, a PR pending). Later, as settings are touched: radio switches,
      keyboard alphabets and chat filters into the schema.

## H. Before the merge into dev

- [ ] The pull request about the lock screen.
- [ ] The new issues.
- [ ] Merge into dev.
- [ ] A last review.

## I. Repo, documentation, website, firmware tiers (its own detailed plan)

- [ ] Repo: remove working files (plans etc.), keep only the project's code;
      README down to the minimum; add Buy Me a Coffee.
- [ ] Documentation written from scratch: short, describing the firmware's
      features, easy to browse; sections marking where devices differ; no
      screenshots of every screen and device. It stays in the repo, its
      official entry is the simulator website.
- [ ] Website rebuilt and polished; the simulator lets you pick which
      firmware to simulate.
- [ ] Firmware in three tiers, one shared core, extras (map tiles etc.) by
      what the hardware can do, making full use of each device:
      - minimal -- nRF52, a limited UI, small e-ink and OLED;
      - standard -- mostly ESP32, large e-ink and LCD, higher resolutions,
        a somewhat richer UI;
      - color -- colour touch screens.

Proposed: write a short tier spec (which boards, which features, what's in
the core) as a document before stage F, so the review consolidates towards
it; implement the tiers, docs and website after the merge.

## Open decisions

- I: tier spec before stage F, or after the merge.
