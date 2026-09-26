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

- [x] Screen-lock PIN: 4-8 digits in NVS (`lvport::loadPin`), asked on
      every wake and after a reboot, 5 misses pause entry for 30 s;
      Settings > Display & power > SECURITY.
- [x] Map tiles fetched live while online: the map queues tiles it is
      missing, `TileDownloader` fetches them one at a time over WiFi (connected
      on the first miss, dropped 30 s after leaving the map) and saves them to
      the card; Map > ☰ > Live tiles (NVS, on by default).
- [x] One-button OTA from GitHub Releases: Settings > System > Firmware
      update (`OtaScreen.h`). The latest release's `-Wio-Tracker-L2-ota.bin`
      (app image; `build-solo-firmwares.yml` now publishes `*_solo_lvgl`) is
      streamed over TLS verified with the framework's CA bundle into the idle
      slot of `default_16MB.csv` (two 6.25 MB app slots, no layout change),
      chip id checked, then restart. Arduino 3.x builds only.
- [ ] Before the first release with L2: make the 3.x env the
      `*_solo_lvgl` one (stage 2's last item), then test an update end to end.

## 6. Theme

- [ ] Develop the current look into a consistent, recognisable style
      (colours, corners, type, icons, states), after the layout of stage 4.

## 7. Research: LVGL for the other displays

- [ ] Find out whether LVGL on every display variant gives consistency and a
      nicer look more easily. Adopt only if the result is better and every
      feature is kept (L1: 128×64 OLED, nRF52, RAM already 68% used).

## Backlog (found along the way)

- [ ] USB power detection: `WioTrackerL2Board::isExternalPowered()` reads
      STATUS0 (0x40) bit 7 of the chip at I2C 0x22 and always gets 0 with
      USB plugged in (0x40 reads 0x01). Needed for a charging indicator and
      for skipping the low-battery shutdown on the cable. Find the right
      chip / register (devices on the bus: 0x14, 0x18, 0x21, 0x22, 0x2c,
      0x34, 0x48, 0x5d). Found 2026-09-26.

