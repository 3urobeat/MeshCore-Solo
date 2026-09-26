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
- [x] The 3.x env is the default (2026-09-26, after stages 5 and 6 were
      tested on it): `Wio_Tracker_L2_companion_solo_lvgl` is Arduino 3.3.12
      and is the one published; 2.0.17 stays as
      `Wio_Tracker_L2_companion_solo_lvgl_arduino2` (not published, can't
      self-update).
- [x] Limits sized for the PSRAM, overridable per board so the nRF52
      defaults stay (the L1 build is byte-for-byte unchanged): trail 512 →
      4096 points (`TRAIL_CAPACITY`; the map drops points under 2 px apart
      when drawing, so a long trail doesn't slow redraws; 8 → 32 trail
      segments), waypoints 16 → 64, message history 48 → 256 channel and
      32 → 128 DM entries (`HIST_CH_MAX` / `HIST_DM_MAX`; stage 3 moves it
      to SD), Nearby 32 → 64; in ui-lvgl: contact list 64 → 256 rows,
      rooms 16 → 32, conversation 30 → 50 bubbles, pickers 64 → 128, WiFi
      scan 12 → 20. The sim builds with the same limits.
      Measured after: internal heap 154.1 KB free (was 147.9), PSRAM
      5.67 MB free.

## 3. Data on the SD card

- [x] Message history on SD (`HistoryStore.h`, `-D HIST_ARCHIVE`): one
      file per conversation in `/sdcard/meshcore/history` (channels keyed by a
      hash of name + secret, contacts / rooms by key prefix), a ring of the
      history entries themselves, every new message and later change (relay
      echo, delivery) written at once. After a reboot the newest entries go
      back into the RAM ring (Messages list, previews); a conversation shows
      50 at a time with "Older messages (n)" / "Newer messages" and reads the
      card, so it reaches back as far as the card keeps. Kept per chat: 100 /
      250 / 500 (default) / 1000 / 2000, ~20 KB per 100 messages.
- [x] Settings > Storage: SD card used / free with what takes it (maps,
      messages, GPX trails, other; files counted in the background), the
      internal flash, "Kept per chat", "Delete message history" (tap twice).
- [x] Saved trails on the SD card: Map tools > Save writes a new
      `/sdcard/trails/trail-YYYYMMDD-HHMM.trl` each time (the internal slot
      without a card); Load lists them newest first (plus the internal slot,
      where the low-battery auto-save goes) -- each with distance, time and
      points, Load (the map frames it), GPX, Delete.
- [x] Diagnostics > Live: GPS (off / no data / fix or no fix with satellites),
      why the device last started (crash, watchdog, low voltage...), and the
      last crash from the core dump partition (task and address).

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
- [x] The 3.x env is the `*_solo_lvgl` one (stage 2).
- [ ] With the first release that has an L2 asset: test an update end to
      end.

## 6. Theme

- [x] A consistent style, written down in `Theme.h`: accent fill = the
      primary action (Download, Install, Save, Go), dim accent fill =
      selected / on (tabs, chips, segments -- also the default theme's
      CHECKED), accent text = names, counts, modes; one card radius for
      buttons and rows, pills for chips, `RADIUS_SM` inside; slightly lighter
      surfaces. Accent colour selectable (Settings > Display & power > LOOK:
      amber, orange, coral, violet, cyan, lime; NVS `mc_ui`).
- [x] Light motion (`Anim.h`): a new screen emerges from the middle (a
      background-coloured cover fades while the content drifts 6 px), Home
      pages slide after a swipe, popups and toasts rise and fade in, buttons
      shrink a little while pressed.
- [x] Frame time measured on the device (`-D UI_PERF_TEST`: walks screens by
      itself, prints render + flush per refresh): ~48 → ~33 ms per transition
      frame with uncompressed fonts and two 120-line buffers. Internal-RAM /
      DMA buffers, `-O2`, hot code in IRAM, two draw threads, system malloc:
      no gain. The rest is LVGL's software rendering at 320x240.
- [x] Status bar icons in equal cells; muted is a speaker with a cross.
- [x] Under an own message: the age and a small mark as in L1 (✓ n repeaters
      for channel posts; ✓ / ✗ / ... for DMs) instead of the words.
- [x] The path window of a message: quote, time and hops, a path diagram
      (sender → repeaters → this device), repeaters that relayed an own post,
      an own DM's delivery in words.
- [x] Tile download popup fits the screen; its WiFi button went (WiFi is set
      up in one place, Settings).
- [x] Splash screen (`Splash.h`): MeshCore wordmark, SOLO, the Solo and the
      upstream version, build date, loading dots.

## 7. Research: LVGL for the other displays

- [x] Find out whether LVGL on every display variant gives consistency and a
      nicer look more easily. Adopt only if the result is better and every
      feature is kept (L1: 128×64 OLED, nRF52, RAM already 68% used).

Findings (2026-09-26). Solo builds, static RAM / flash used:

| Board | MCU | Display | RAM | Flash |
|---|---|---|---|---|
| Wio Tracker L1 | nRF52840 | 128×64 OLED | 68% (74 KB free) | 70% (214 KB free) |
| Wio Tracker L1 e-ink | nRF52840 | e-ink | 70% | 71% |
| T-Echo Lite | nRF52840 | e-ink | 69% | 64% |
| GAT562 Mesh Watch13 | nRF52840 | 128×64 OLED | 68% | 92% (55 KB free) |
| ProMicro, GAT562 30S | nRF52840 | 128×64 OLED | (as L1) | |
| Heltec V3 / V4 | ESP32-S3 | 128×64 OLED | 56% | 44% of 3.2 MB |
| Cardputer ADV | ESP32-S3, no PSRAM | 240×135 colour TFT, keyboard | 56% | 43% of 3.2 MB |

What LVGL costs on the L2: the library ~310 KB of code (full config, PNG
decoder and all widgets; a minimal one is ~120-150 KB), the ui-lvgl screens
~225 KB, fonts 360 KB (uncompressed, European + Cyrillic, 12-40 px). ui-new
on the L1 is ~137 KB.

- **128×64 OLED (L1, Heltec, ProMicro, GAT562): to be tried.** Colour and
  anti-aliasing don't matter there, but the v2 UI goals do: motion (Home
  carousel slide, loading-dot wave, animated splash, a bottom drawer, radar
  sweep), soft corners, graphic indicators instead of text, a bigger font
  to try out, dithering for large elements. LVGL has the animation engine,
  shapes, TTF fonts at any size and self-laying-out lists for that; rendered
  in greyscale and converted to 1 bit in the flush, a Bayer threshold there
  would turn every fade / translucent fill into dithering while 1-bpp text
  and icons stay crisp. Against it: a rewrite of ui-new's screens (~15.6k
  lines; `ui-core/` carries over), I2C caps a full frame at ~23 ms either
  way, and nRF52 memory -- estimated ~20-30 KB RAM (heap + an 8 KB L8
  buffer) and ~120-150 KB flash for a trimmed LVGL, against 74 KB RAM /
  214 KB flash free on the L1 (ui-new's drawing code would go) and only
  55 KB flash on the Watch13. **Next step, after this roadmap:** a spike on
  the L1 -- Home carousel with the slide and the bottom drawer, a message
  list with soft bubbles, the dot wave, dithering in the flush -- measured
  (RAM, flash, fps) and shown in the sim next to ui-new, then decide.
- **E-ink (L1 e-ink, T-Echo Lite): no.** Slow full refreshes rule out
  motion; LVGL's small dirty areas fit partial refresh poorly and would need
  batching. Same RAM limits as above.
- **Cardputer ADV: the one candidate.** A 240×135 colour screen now shows
  ui-new's 128×64 picture scaled up; ESP32-S3 with room to spare (flash
  43%). LVGL would use the real resolution and colour, reuse `Theme.h`,
  `Anim.h`, the fonts and much of the ui-lvgl screen code. Open points: no
  PSRAM (the L2's PSRAM buffers -- tiles, lists, keyboard maps -- need
  smaller internal ones or dropping, no raster map), no touch (keyboard
  focus navigation, LVGL groups, instead of taps), a 135 px-high layout.
  Worth a separate pilot if the Cardputer matters; otherwise skip.

So: e-ink stays on ui-new; the OLED boards get an LVGL spike (above) once
this roadmap is done; the Cardputer follows whatever the spike shows.

## Backlog (found along the way)

- [ ] USB power detection: `WioTrackerL2Board::isExternalPowered()` reads
      STATUS0 (0x40) bit 7 of the chip at I2C 0x22 and always gets 0 with
      USB plugged in (0x40 reads 0x01). Needed for a charging indicator and
      for skipping the low-battery shutdown on the cable. Find the right
      chip / register (devices on the bus: 0x14, 0x18, 0x21, 0x22, 0x2c,
      0x34, 0x48, 0x5d). Found 2026-09-26.

Ideas to come back to (2026-09-26):

- [ ] Speaker click: would keeping the amplifier on at minimum volume
      remove it?
- [ ] Import routes from the SD card (optional extra).
- [ ] Battery life without losing features, above all CPU sleep.
- [ ] Live tiles: don't keep them, or give them a bounded cache on the card
      so they never fill it.
- [ ] Vector maps and other tile sources (to think through).

