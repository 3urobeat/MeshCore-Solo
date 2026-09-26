# Wio Tracker L2 (ui-lvgl) roadmap

The L1 → L2 feature port and its audit are done (2026-09-26). This is the plan
for what comes next, in order. Tick items off as they land.

Decisions already made:

- **Buttons:** WAKE (side, expander P00) turns the screen off / on. USER/BOOT
  goes back; holding it mutes / unmutes the device.
- **Placeholders:** the "+" popup next to a text field is the placeholder UI;
  it must offer every placeholder and appear at every field that sends text.
- **Message history:** kept on the SD card, about 100 per conversation to
  start with.
- **OTA:** firmware comes from this fork's GitHub Releases.
- **PIN:** screen lock only, for now.

## 1. Quick fixes

- [x] Keyboard: a key that closes it (so far a message could only be sent).
- [x] "+" placeholders at every field that sends text (compose, quick
      messages, bot replies).
- [x] Short, terse descriptions on buttons and rows.
- [x] WiFi off switch.
- [x] Buttons as decided above.

## 2. Arduino-ESP32 3.x, PSRAM

- [x] A separate L2 env on pioarduino 55.03.312-1 (Arduino-ESP32 3.3.12,
      IDF 5.5): `Wio_Tracker_L2_companion_solo_lvgl_v3`; the 2.0.17 env stays
      until the new one passes the hardware checklist.
- [x] Port: speaker (new I2S driver), BLE (NimBLE), ESP-NOW (IDF 5 callbacks);
      HTTPS map download and LovyanGFX build unchanged (check on the device).
- [x] Large buffers in PSRAM (`psramBuf`): trail drawing, tile cache, map
      marks, list rows, keyboard maps, message metadata. Internal heap free at
      runtime: 2.0.17 147.8 KB; 3.x 128.2 KB before, 147.9 KB after (static RAM
      109 → 71 KB). Measured with `-D UI_HEAP_REPORT`. History and a larger
      trail come to PSRAM with stage 3.
- [ ] Full hardware checklist, then make the new env the default.

## 3. Data on the SD card

Postponed (2026-09-26): the user may extend this stage first.

- [ ] Message history on SD, kept across reboots (~100 per conversation;
      the newest ones cached in PSRAM).
- [ ] Trail sized to the device: thousands of points in PSRAM (now 512),
      saved to SD.

## 4. UI layout

L1 splits its tools into many small screens because of the joystick and the
128x64 display; the L2 groups them where they are used instead.

- [x] Tools: trail, live share and arrival alert stay in the map (its tools
      popup, with their options behind "Options"); the advert (send now,
      auto-advert) is in Nearby; Repeater, Admin (a list of repeaters and
      room servers) and Diagnostics are Home tiles next to Favourites,
      Compass, Clock and Bot. The melody editor stays in Sound.
- [x] Settings in L1's order: display, sound, radio (with Bluetooth, WiFi),
      system, keyboard, contacts & messages.
- [x] Every status icon L1 has (Bluetooth, GPS, alarm, mute, auto-advert,
      trail, live share, repeater) plus arrival alert; background modes in
      the accent colour instead of L1's blinking.

## 5. Security and internet

- [ ] Screen-lock PIN.
- [ ] Map tiles fetched live while online.
- [ ] One-button OTA from GitHub Releases (release asset + partition layout
      check).

## 6. Theme

- [ ] Develop the current look into a consistent, recognisable style
      (colours, corners, type, icons, states), after the layout of stage 4.

## 7. Research: LVGL for the other displays

- [ ] Find out whether LVGL on every display variant gives consistency and a
      nicer look more easily. Adopt only if the result is better and every
      feature is kept (L1: 128×64 OLED, nRF52, RAM already 68% used).
