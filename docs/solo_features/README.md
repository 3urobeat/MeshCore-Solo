# MeshCore Solo — documentation

## Documents

| Document                                                                   | Description                                                           |
| -------------------------------------------------------------------------- | --------------------------------------------------------------------- |
| [Messages Screen](./message_screen/message_screen.md)   | Sending messages, context menus, reply, navigate to / save shared locations, Notif/Melody overrides |
| [Favourites Dial](./favourites_dial/favourites_dial.md) | Pinned contacts grid, unread badges, pin/unpin                        |
| [Clock Screen](./clock_screen/clock_screen.md)          | Clock page, date, configurable data fields, alarm / timer / stopwatch  |
| [Settings Screen](./settings_screen/settings_screen.md) | All settings sections with values and interactions                    |
| [Screen Lock](./screen_lock/screen_lock.md)             | Lock/unlock sequence, lock screen, auto-lock                          |
| [Tools Screen](./tools_screen/tools_screen.md)          | GPS trail & waypoints, compass, navigation, nearby nodes, ringtone editor, remote bot, auto-advert, live location sharing, locator, diagnostics, repeater, remote admin |
| [External Keyboard & Joystick](./external_keyboard.md)  | CardKB shortcuts, Full vs Compact mode, wired joystick, Heltec V3/V4 wiring |
| [Build Flags](./build_flags.md)                        | Every optional `-D` build flag a solo build understands — GPIO, Hall sensor, buzzer/vibration, GPS switch, display/battery tuning |
| [Solo UI framework](../developer/ui-framework.md)                    | **Developer guide** — the reusable building blocks (screens, lists, popups, mini-icons, geo/persistence helpers) and how to add a new feature |
| [UI Core](../developer/ui-core.md) | **Developer guide** — the frontend-independent layer (models, settings schema, events) shared by the OLED/e-ink UI and the LVGL touch UI |

## Upstream MeshCore

| Document                                           | Description                                      |
| -------------------------------------------------- | ------------------------------------------------ |
| [FAQ](../faq.md)                               | Frequently asked questions                       |
| [CLI Commands](../cli_commands.md)             | Commands for repeaters, room servers and sensors |
| [Terminal Chat CLI](../terminal_chat_cli.md)   | Commands for the terminal chat client            |
| [Companion Protocol](../companion_protocol.md) | Serial/BLE frame protocol between device and app |
| [Packet Format](../packet_format.md)           | LoRa packet structure                            |
| [QR Codes](../qr_codes.md)                     | Channel and contact QR code formats              |

## Features

- Extended language support — one unified 6×9 font (Latin, Greek, Cyrillic) plus on-screen keyboard alphabets for Cyrillic, Greek, Polish, Czech, Slovak, German, French, Spanish, Portuguese and Nordic. Pick two in Settings › Keyboard (**Main**/**Additional**) and switch between them while typing

- Enabled sensor screens with support for onboard sensors (temperature, humidity, pressure, luminosity, CO₂) and GPS data

- **GPS navigation** — a full navigation suite that needs no extra hardware (details in the [Tools Screen](./tools_screen/tools_screen.md) docs):

  - **Waypoints** — mark a spot (car, camp, water…) with a short label, see it on the trail map, and get live bearing + distance back to it; the list always offers a one-tap backtrack to where your trail started
  - **GPS compass** — heading derived from course-over-ground (no magnetometer needed), shown as a clear scrolling heading tape with a large degrees + cardinal readout
  - **Navigate to anything** — a saved waypoint, a node straight from Nearby Nodes, or a location someone shares with you in a message
  - **Share & save locations** — send a waypoint to a contact or channel; on the other end, navigate to or save any shared location with one menu
  - **Live location sharing** — broadcast your position over the mesh as you move (movement-gated, to a channel or contact) and see others who share theirs as pins on the map and live distance/bearing in Nearby
  - **Locator** — arm a geofence around a waypoint or a person, get alerted on arrive/leave or near/far, with an optional homing beeper that speeds up as you close in. Set from the Locator screen, Nearby Nodes, or Waypoints; target shown as a flag on the map
  - **GPS trail** — background route recording with an auto-fit map (waypoints + live position), summary stats, auto-pause on stops, and [GPX export](../../README.md#documentation)
  - **Metric or imperial** — one global Units setting drives every distance and speed across the UI

- [Messages Screen](./message_screen/message_screen.md) — view and send messages, open message details, reply with quick messages or custom text, navigate to / save locations shared in a message, per-channel notification and melody overrides, add/edit/delete channels on-device

- [Favourites Dial](./favourites_dial/favourites_dial.md) — pin up to six contacts for quick access from the home screen

- [Settings Screen](./settings_screen/settings_screen.md) — configure display, sound, home page order, radio and system settings

- [Clock Screen](./clock_screen/clock_screen.md) — view time and date plus up to three configurable data fields, with built-in clock tools (one-shot alarm, countdown timer, stopwatch)

- [Screen Lock](./screen_lock/screen_lock.md) — lock the device to prevent accidental keypresses, with a lock screen showing time and sensor data

- [Tools Screen](./tools_screen/tools_screen.md) — GPS trail & waypoints, compass, nearby nodes (with ping & navigate), ringtone editor, remote bot, auto-advert, live location sharing, locator, diagnostics, repeater, remote admin

- [External Keyboard & Joystick](./external_keyboard.md) — optional, auto-detected: **CardKB** for typing without the on-screen grid (Fn+Enter submits, Fn+letter picks an accent, Tab is Hold-Enter, Fn+Esc locks), plus a **wired joystick** for boards without one. Compact mode makes CardKB-only operation practical

- **Auto pwr** (Settings › Radio) — Adaptive Power Control: trims actual TX power on strong links (from ACK SNR) and ramps back up to the configured ceiling on weak/lost links; the home screen shows the live power

## E-ink Display (Wio Tracker L1)

The e-ink variant targets the Wio Tracker L1 fitted with a 2.13″ GxEPD2 panel (250 × 122 px). Every screen is adapted for it:

- **Adaptive layout** — every screen reflows correctly in both landscape (250 × 122) and portrait (122 × 250) orientations
- **Display rotation** — configurable in Settings › Display; applied immediately and persisted across reboots
- **Joystick rotation** — independent of display rotation; useful for custom enclosures
- **Full refresh interval** — configurable in Settings › Display; reduces ghosting on long sessions
- **Clock seconds suppressed by default** — seconds are hidden to reduce per-second panel refreshes and extend display lifetime; re-enable in Settings › Display
