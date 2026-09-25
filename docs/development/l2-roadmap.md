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

- [ ] A separate L2 env on pioarduino (as `esp32c6_base`); the 2.0.17 env
      stays until the new one passes the hardware checklist.
- [ ] Port: speaker (new I2S driver), BLE, ESP-NOW, HTTPS map download,
      LovyanGFX.
- [ ] Large buffers in PSRAM: message history, trail, tile cache. Measure
      internal RAM before / after.
- [ ] Full hardware checklist, then make the new env the default.

## 3. Data on the SD card

- [ ] Message history on SD, kept across reboots (~100 per conversation;
      the newest ones cached in PSRAM).
- [ ] Trail sized to the device: thousands of points in PSRAM (now 512),
      saved to SD.

## 4. UI layout

- [ ] Every tool as its own Home tile (trail, live share, arrival alert,
      repeater, admin, diagnostics, ...), not inside Settings.
- [ ] Settings in a sensible order, as on the original (L1).
- [ ] Every status icon the original has (BT, GPS, live share, trail,
      repeater, alarm, mute, arrival alert, ...).

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
