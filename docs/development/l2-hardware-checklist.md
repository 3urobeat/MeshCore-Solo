# Wio Tracker L2 (ui-lvgl) — hardware checklist

Things verified only in the browser simulator so far. Tick on the device;
note anything odd next to the item. Branch `wio-tracker-l2`.

## Feedback round 1 (2026-09-25) — fixed, check on the device
Confirmed on the device: live share sends and receives positions.
- [ ] Map: ☰ tools moved to the left column (back, pin, ☰); nothing overlaps the crosshair
- [ ] Target ring sits exactly on the marker (all markers were drawn ~3 px low)
- [ ] A person sharing on a channel, picked as target, is followed as they move (was a fixed point where they were); bar shows the person icon
- [ ] Settings: subtitles no longer cover the titles (all list rows)
- [ ] Map credit is a small "©" (tap: full line in a toast); full text also in Settings > About; long toasts wrap
- [ ] Home: Messages, Nearby, Map, Settings — "Map" is the navigation map; the nodes map opens from Nearby's header (map icon), back returns to Nearby
- [ ] Home tiles are wider; more apps will go on further swipeable pages (dots appear once there are two)
- [ ] Overzoom limited to 2 levels (x4); past that "No map detail here at this zoom" — download more
- [ ] Panning: no tile decoding while the finger moves (smooth drag, tiles fill in once you stop); tiles around the view are decoded ahead while idle; newly revealed area shows the coarser tile until the sharp one is ready
- [ ] Note how panning feels now: ______ (next step if still slow: decode on the second core / JPEG tiles)

## Feedback round 2 (2026-09-25) — fixed, check on the device
Confirmed: settings rows OK, map pans much faster, ring centred.
- [ ] Target ring is wider (30 px) so the green / flag marker shows inside it; a target with no marker on it (map point, message position, stale live share) gets an orange centre dot
- [ ] "↓ Resume?" pill sits top right beside +, clear of the zoom pill; tapping it opens the download popup
- [ ] Pin button → Waypoints list (distance, tap → Go / Name / Share / Delete) with "Here (GPS)" and "Coordinates"
- [ ] Coordinates: digits keyboard, "50.06142, 19.93721 Name" or with a space instead of the comma; bad input keeps the field with a hint; the map centres on the new waypoint
- [ ] Holding the map no longer adds a waypoint straight away: "This spot" popup with Add waypoint / Go here

## Feedback round 3 (2026-09-25) — fixed, check on the device
Confirmed: green dot inside the ring, waypoints much better, download works, settings OK.
- [ ] Ring exactly on the marker at every zoom (it was computed in float: pixels off at z16+). The trail line had the same error and is fixed too
- [ ] Home: swipe left / right anywhere (clock or tiles) turns the page; a swipe starting on a tile doesn't open it; tapping a dot also switches
- [ ] Settings > Display & power: Brightness is a slider (5-100 %), changes live while dragging, kept after a reboot and after the screen sleeps / wakes
- [ ] After the update: prefs load normally (new field, schema sentinel 0xC0DE002F) — brightness starts from the old level, nothing else reset

## New: Clock, settings pages, radio (2026-09-25)
- [ ] Home has two pages now: swipe left → Clock (dots under the tiles follow; Home remembers the page)
- [ ] Clock > Alarm: hour / minute rollers (setting a time arms it), On switch, Repeat (Once / Daily / Weekdays / Weekends); rings at the local time
- [ ] Clock > Timer: H / M / S rollers, Start → big countdown, Stop; when it ends a full-screen "Timer done" card with Dismiss appears on any screen (also wakes the display; the user button dismisses too). Rings with a melody since the Sound round
- [ ] Clock > Stopwatch: Start / Stop / Reset with tenths
- [ ] Settings > Display & power: Screen off after; Wake on message
- [ ] Settings > Display & power: Battery shutdown (not while on USB), GPS power saving, Time zone (clock + alarm follow)
- [ ] Settings > Messages & contacts: Resend direct messages, Contact expiry + "Remove inactive contacts now" (first tap shows the count, second removes; favourites kept), Favourites first
- [ ] Settings > Radio: Preset list (built-ins + your saved ones), Frequency (tap, type), SF, Bandwidth, Coding rate, TX power, Auto power; changes apply at once — check messages still flow after switching back to your usual preset
- [ ] L1 (ui-new): Settings > Radio TX power / Auto power / presets still apply (now through the shared RadioControl)

## New: Channels (2026-09-25)
- [ ] Messages: favourite channels first with a star, muted ones with a speaker icon
- [ ] Hold a channel row (or ⚙ in an open channel) → options: Alerts (Default / Muted / Always, one tap), Scope (only if regions exist), Fav, Read (only with unread), Edit, Delete (second tap confirms)
- [ ] "+ Add channel" → Public (re-adds the open channel; "already exists" if it's there), Hashtag (topic → "#topic"), Private (name + passphrase, or Hex key switch → exactly 32 hex chars)
- [ ] A hashtag / private channel made on L2 talks to the same channel made on L1 or in the phone app
- [ ] Edit: rename keeps the key (shown at the top); typing a new passphrase changes it
- [ ] Deleting a channel that was the live share / bot target turns those off; a channel re-added in that slot starts with default notifications / no favourite (also when deleted from the phone app)
- [ ] L1 (ui-new): Messages > Channels add / edit / delete / mute / pin still work (now through the shared ChannelControl)
- [ ] L1: deleting a contact from the app still unpins it from the dial and clears its mute / melody (cleanup moved into the Core)

## New: Repeater admin (2026-09-25)
- [ ] Nearby > a repeater or room server you have in contacts > "Admin" (the Fav button is just a star there)
- [ ] First time: password field + keyboard; wrong password → "Login failed", back on the node; right one → tabs System / Radio / Routing / Actions
- [ ] Next time: logs in by itself with the saved password (no keyboard); a node you just used opens straight away
- [ ] Out of range: "No answer to the login" after a while, and the saved password is forgotten (asks again next time)
- [ ] Name / Owner info: "Reading..." then a text field with the current value; ✓ sends it, reply popup "OK"
- [ ] Admin password: set a new one → the saved password follows (next login works without typing)
- [ ] Routing > Repeat switch; Advert interval / Flood advert / Max hops with − / +; Radio > SF / Bandwidth / Coding rate choices, Frequency typed, TX power − / +
- [ ] Actions: Send advert / zero-hop / Sync clock give a reply; Reboot and Start OTA ask first (red button)
- [ ] Custom command: e.g. `ver`, `neighbors` (long replies scroll)
- [ ] "Reading... (tap to stop)" stops waiting when tapped
- [ ] Room server login from the phone app / L1 still works while this exists (only admin logins go to the new session)
- [ ] L1 (ui-new): Tools > Admin works as before (login, saved password, typed values, confirm reboot); it now also skips the login for the node you just used

## Roadmap stage 2: Arduino-ESP32 3.3.12 / IDF 5.5 (2026-09-26)
Env `Wio_Tracker_L2_companion_solo_lvgl_v3` (flash with `L2_ENV=Wio_Tracker_L2_companion_solo_lvgl_v3`). Bluetooth runs on NimBLE now, the speaker on the new I2S driver. Internal heap at runtime: 147.9 KB free on 3.x vs 147.8 KB on 2.0.17 (large UI buffers moved to PSRAM; static RAM 109 → 71 KB).
- [ ] Boots, screen and touch as before; WAKE and USER buttons
- [ ] Bluetooth: the app pairs with the PIN (asks for it: the link is encrypted), connects, syncs contacts and messages, sends; reconnects after the app is closed / reopened; the Bluetooth switch still works
- [ ] Sound: startup chime, message sounds, melody editor, volume; no new knocks
- [ ] LoRa: send / receive on a channel and a DM, adverts
- [ ] GPS fix, map, trail drawn on the map (its buffers moved to PSRAM)
- [ ] WiFi: scan, map download over HTTPS; the WiFi switch
- [ ] SD card: maps load, trail GPX export
- [ ] Settings / contacts / channels survive the update from the 2.0.17 build (same flash layout)

## Roadmap stage 1: quick fixes (2026-09-26)
- [ ] Keyboard: the keyboard key (bottom left) closes it; in a chat the typed text stays, in a dialog nothing is saved
- [ ] Symbols page ("1#"): "_" is in the third row now
- [ ] Bot > Reply: every placeholder in the row above the field ({name} {hops} {loc} {time} {batt}, sensors); swipe it sideways
- [ ] WAKE button (side): screen off / on; silences a ringing alarm
- [ ] USER button: back (answers at once, no double-click wait); held 1 s, sound off / on with a toast and the mute icon; wakes the screen when it's off
- [ ] Settings > CONNECTIVITY > WiFi: a switch as on Bluetooth (toast, row says "Off" / the network); tapping the row opens the network settings (editable while off; Scan asks to turn it on); off: map download says "WiFi is off"; kept across a reboot
- [ ] Shorter descriptions on settings rows, Bot, Repeater, Send advert, Bluetooth ("PIN 123456" / "App connected")

## New: Quick messages, placeholders, advert, Bluetooth (2026-09-26)
- [ ] Chat "+" (left of the text field): Insert {loc} / {time} / {batt} (+ sensors) into the message; they are filled in when sent (a reply's "@[name] " stays as typed)
- [ ] Chat "+" > a quick message sends it at once (the popup shows what will go out, filled in); "Edit quick messages" opens the list
- [ ] Settings > Messages & contacts > Quick messages: 10 slots, tap to edit (field, placeholder chips, keyboard); empty clears; "OK" is there on a fresh device; the same slots as L1's Settings > Messages
- [ ] Nearby > advert button (tower icon) > Nearby only (zero-hop) / Everyone (flood through repeaters); another node sees you
- [ ] Settings > CONNECTIVITY > Bluetooth: off / on; the row shows the pairing PIN while waiting, "the app is connected" when paired; USB keeps working with Bluetooth off; after a reboot Bluetooth is on again (as on L1)
- [ ] Messages: "Read all" in the header while anything is unread
- [ ] Nearby > a node with a position: flag button saves it as a waypoint; pin button puts a contact on the favourites dial; with five or more buttons they show icons only; delete asks with "trash?" and a toast
- [ ] L1: quick messages, placeholders and the sensor placeholder list work as before (now shared through ui-core/MessageText.h)

## New: Sound (2026-09-26)
The speaker plays through the ES8311 codec (I2S MCLK 10 / BCK 11 / WS 12 / DOUT 16, amp on expander P12). Pins come from Seeed's Meshtastic port; nothing was heard on the device yet.
- [ ] A short startup chime after boot (sound On); a goodbye sound on Power off
- [ ] Settings > Sound: On / Off / Auto (Auto: silent while the app is connected); Off shows a muted-speaker icon in the status bar
- [ ] Volume slider (5 steps): a beep on release at the new level; the quietest is still audible, the loudest doesn't distort or rattle: ______
- [x] No knock between sounds, however fast or slow the taps (fixed: notes fade in / out on a raised cosine, a cut note fades over 15 ms, and the DMA queue is kept full of silence between sounds -- a sound starting into a queue that had run dry was played from a half-written buffer)
- [x] Known, kept: one soft knock on the first sound after >3 s of silence -- the class-D amp's own power-up pop (a longer codec settle didn't change it, only delayed the sound). Kept the amp off between sounds for battery / GNSS; alternatives if it bothers: amp on while the screen is on, or always on
- [ ] Direct message, channel message and advert each play their sound (Built-in / Melody 1 / Melody 2 / None under PLAYS FOR); "Advert sound for: Direct only" skips adverts that came through repeaters
- [ ] Hold a chat in Messages: Alerts (Default / Muted / Always) and Sound (Default / Melody 1 / Melody 2) apply to that chat only; Always plays even with sound Off
- [ ] Clock alarm / timer rings with a repeating melody until Dismiss (or the user button); sound Off doesn't silence it
- [ ] Arrival alert plays a rising / falling triple; Proximity beeper ticks faster closer to the target
- [ ] Settings > Sound > Melodies: + adds a note (a copy of the selected one), tap a note to pick it; pitch / octave / length change it with a short preview; trash deletes; BPM; Play / Stop in the header, the note sounding lights up in full colour (the strip scrolls along); saved on Back or when switching Melody 1 / 2 ("Melody 1 saved"); the same melodies play on L1 if the prefs are shared
- [ ] Timing stays even while the map redraws (notes are counted in samples by the audio task, not by the UI loop)
- [ ] GPS keeps its fix while sounds play (the amp is off between sounds to keep its switching noise away from the antenna)

## New: Repeater mode (2026-09-25)
- [ ] Settings > CONNECTIVITY > Repeater (row shows Off / On and what it relays on): switch on → "Repeater on", the loop icon appears in the status bar; off → it goes away
- [ ] Relay on Custom: the radio moves to the profile's frequency while on and back to the chat frequency when off (Settings > Radio row / another node on each frequency); Current: stays on the chat frequency
- [ ] Custom profile: Preset dropdown, Frequency (typed, out-of-range refused), SF / BW / CR apply at once; while relaying on it the change is live
- [ ] While on: Settings > Radio > Auto power is greyed ("Off while repeating") and TX runs at the set power
- [ ] Another node two hops away receives messages through this device; Skip adverts stops relaying adverts only; Max hops / Min SNR / Yield / Skip duplicates / Scope only take effect (compare with L1 Tools › Repeater showing the same values)
- [ ] Extra scopes popup: a switch per scope, the row counts "N of M relayed"; with no scopes it points to Settings > Radio
- [ ] Fresh device (erased flash): Min SNR starts at Off, not 0 dB (fixed default, also on L1)

## New: My presets, scopes, battery display, clock seconds (2026-09-25)
- [ ] Settings > Radio > MY PRESETS > Save current settings: name popup, Enter → "Preset saved", the row shows freq / SF / BW / CR with a tick while in use, and it is in the Preset dropdown
- [ ] Tap a saved preset → Use (radio switches) / Delete (confirm, red) → "Preset deleted"; a 5th name replaces the oldest (hint says so when all 4 are used); presets survive a reboot
- [ ] L1: Settings › Radio › Preset lists the presets saved on L2 and vice versa (same slots)
- [ ] Settings > Radio > SCOPE > Scopes: "* (no scope)" is the default; + Add → name → listed; tap → Default / Rename / Delete; the Radio screen row shows the default
- [ ] Deleting the default scope makes * the default; a channel set to the deleted scope goes back to none (channel options > Scope); the app shows the same default scope after a sync
- [ ] Settings > Display & power > Battery display: Icon / Percent / Voltage change the status bar at once; percent follows the L1 curve (empties at the Battery shutdown voltage)
- [ ] Settings > Display & power > TIME > Clock seconds off: Home and lock screen clocks show HH:MM; on: HH:MM:SS

## New: Diagnostics, auto-advert, compass (2026-09-25)
- [ ] Home > Diagnostics (page 3): tabs Live / System / Font; Live counters (uptime, rx/tx, heap, noise floor, RSSI/SNR, queue, errors) tick every second
- [ ] Live > Reset → confirm popup → "Counters reset", rx/tx and forwarded go back to 0
- [ ] System shows firmware, build date, board, node name, frequency / SF / BW / CR, TX power; Font shows Polish, Greek and Cyrillic samples without boxes
- [ ] Nearby > advert button > AUTOMATIC > Auto-advert 30 s: another node sees your advert (with position) every ~30 s; Off stops it; survives a reboot
- [ ] L1: Tools › Auto-Advert shows the value set on L2 (same pref), Tools › Diagnostics unchanged
- [ ] Home page 2 > Compass: without a fix "Waiting for a GPS fix" (GPS off: hint to turn it on); standing still "Move to set the heading"; walking: dial turns so your course is under the amber pointer, degrees + cardinal on the right

## New: Node name, reboot, lock screen, favourites dial (2026-09-25)
- [ ] Settings > NODE > Name: popup with the current name, Enter saves; Home and Settings show the new name; another node sees it after your next advert
- [ ] Settings > SYSTEM > Reboot / Power off: confirm popup (red button); reboot comes back normally, messages and settings kept
- [ ] Settings > Display & power > Lock screen on; > TIME > 12-hour clock changes status bar ("2:05 PM"), Home and lock screen (AM/PM before the date)
- [ ] With Lock screen on: screen off (timeout or the button on Home) → wake → clock card with "N new messages"; tapping anywhere does nothing; the knob follows the finger; sliding to the right end unlocks; letting go early springs it back
- [ ] Locked: a new message still wakes the screen and shows the toast; the button turns the screen off again; alarm ring shows over the lock
- [ ] In the pocket for a while with Lock screen on: nothing changed / sent
- [ ] Home page 2 > Favourites: 6 slots; tap an empty one → pick a channel / contact / room; filled slot shows # / person / house icon and unread badge; tap opens it (a room logs in first); hold → Change / Remove
- [ ] Channel options and conversation options: pin icon → "Pin to favourites" with six slots (current one highlighted, tap it again to unpin)
- [ ] L1 (ui-new): favourites dial and Pin to dial work as before

## New: Rooms, conversation options, message actions (2026-09-25)
- [ ] Messages: sections CHANNELS / DIRECT / ROOMS; "All" / "★ Fav" pill on CHANNELS and ROOMS shows only favourites (and survives a reboot)
- [ ] ROOMS lists room servers ("Tap to log in" / last post); "ROOMS - N new" when posts arrived
- [ ] Tap a room never logged into: password popup (empty is fine for public rooms) → "Logging in..." → "Logged in" → thread opens
- [ ] Wrong password: "Login failed - wrong password?" and the popup comes back; no answer: "No answer from the room"
- [ ] Reboot, tap the same room: logs in with the saved password without asking, then opens
- [ ] Room thread: other people's posts show their name above the text; your post gets "delivered"
- [ ] Room ⚙ / hold the row: "Logged in" status, Fav, Login (new password), Logout (forgets the password; next tap asks again)
- [ ] DIRECT: hold a conversation (or ⚙ in the thread): Alerts Default / Muted / Always, Fav (star in the list), Read; muted shows the mute icon
- [ ] New message: CONTACTS "All" / "★ Fav" pill
- [ ] Hold someone's message bubble: path ("Path (2 hops): A > B" or "Heard directly"), Reply puts "@[name] " in the field with the keyboard up, Set target (message with a position) sets the navigation target
- [ ] Hold your own channel post: "Relayed by: ..." once a repeater echoed it
- [ ] L1 (ui-new): Room Servers login / logout / saved password work as before (now in the shared RoomSessions)

## New: Bot (2026-09-25)
- [ ] Home page 2: Clock, Bot
- [ ] Bot: tabs Channel / Room / Direct / Other; header shows "N sent" once it has replied
- [ ] Switches (Enable, Commands, Actions) change and survive a reboot; Direct > DM allow All / Fav
- [ ] Channel / Room: list popup (current one highlighted), picking closes it and shows the name
- [ ] Trigger: text popup ("*" = any message, shown as "(any msg)"); Reply: {name} {hops} {loc} {time} buttons insert at the cursor
- [ ] Other > Quiet from / to: − / + hour, Done; both the same = "Off"
- [ ] With Channel enabled + trigger "!hi" + reply "Hi {name}, {hops}": another node writing "!hi" on that channel gets the reply; `!ping` answered when Commands is on
- [ ] L1 (ui-new): Tools > Bot looks and works as before (now from the shared BotConfig table)

## Basics after the_mesh moved to PSRAM (`MESH_IN_PSRAM`)
- [ ] Boots normally, contacts and channels are all there
- [ ] Messages arrive and send (channel + DM), delivery ticks work
- [ ] Companion app over BLE connects and syncs
- [ ] A few hours of uptime without a reboot

## WiFi map download
- [ ] Settings > WiFi: Scan lists networks, pick one, password, Save
- [ ] Map > download button: popup shows tile count / size, Download starts
- [ ] Pill on the map: "Connecting..." then "↓ n / total"
- [ ] Popup while downloading: IP, dBm, heap (internal free / largest block) — note the numbers: ______
- [ ] Mesh messages still arrive during a download; BLE stays connected
- [ ] Stop → popup shows "Unfinished: z…-…, N tiles" with Resume / trash
- [ ] Power off mid-download → power on → map shows "↓ Resume?" (top right; tap it) → Resume continues (count jumps past the tiles already on the card)
- [ ] Trash discards the unfinished job
- [ ] Download finishes: toast "Map: Done, N new tiles", new tiles appear on the map

## Overzoom (map past the downloaded detail)
- [ ] Zoom in beyond the highest downloaded level: map stays visible (magnified), zoom pill reads e.g. "z17 (map z15)"
- [ ] Panning over magnified tiles is smooth enough (note if it stutters)
- [ ] From a magnified view, download a higher zoom for that area — the sharp tiles replace the magnified ones

## GPS
- [ ] Settings > Navigation shows a "GPS" switch; toggling it turns the module on/off (and survives a reboot)
- [ ] Status bar: GPS icon grey while searching, green with a fix, gone when off
- [ ] Map crosshair with GPS off: turns GPS on ("GPS on, waiting for a fix"), centres once a fix arrives
- [ ] Bot `!gps on` / `!gps off` works on L2

## Two maps
- [ ] Nodes map (Nearby > map icon): contacts with a position as markers, tap → node detail, back → map
- [ ] Map: hold the map → "This spot" → Add waypoint → "WPn" at the finger
- [ ] Map: pin button → Waypoints → Here (GPS) → waypoint at the GPS position
- [ ] Bar (bottom) → "Navigate to" list: waypoints with distance, Trail start (if a trail exists), people sharing live
- [ ] Tap a waypoint → target: ring, dashed line from you, bar with distance / bearing / your course / ETA; view frames you + target
- [ ] ✕ on the bar clears the target
- [ ] Waypoint menu (pencil): Go / Name (Polish letters, 11-byte limit) / Share / Delete (second tap confirms)
- [ ] Share → Messages → pick a conversation → compose holds `[WAY]lat,lon name` → send; a Solo L1 receiving it can save it
- [ ] Map > ☰ > Options > "Show others' positions" on → someone's `[LOC]` share appears on the Map and in the list; tap → navigate to them
- [ ] Node detail (a node with a position) has the compass button → Map with that node as target
- [ ] Map > ☰ > Options > "Arrival alert" on + target set → toast "Arrived: …" when you get within the radius

## Positions in messages
- [ ] A received message with `[WAY]lat,lon name`, `[LOC]lat,lon` or plain "lat, lon" shows Go / Save under the bubble
- [ ] Save → waypoint named from the [WAY] label (else the sender), Polish letters not cut in half
- [ ] Go → Map with that spot as the target

## Trail & live share (Map > ☰ tools button, left column)
- [ ] Record → pill "REC 0 m" at the top; walk: blue trail line follows, distance grows
- [ ] Auto-pause (Map > ☰ > Options): standing still → "PAUSED", walking resumes
- [ ] Stop / Save / Load / Reset (second tap confirms) behave; a trail saved on L2 loads (same /trail file as L1)
- [ ] Export GPX → toast "Saved trails/trail-YYYYMMDD-HHMM.gpx"; the file opens in a GPX viewer (track + waypoints)
- [ ] Live share: pick "Send to" (channel or favourite), Share live → pill "LIVE 1h00"; another node sees the [LOC] updates while you move; stops by itself after the chosen time
- [ ] Send once: with sharing on → "Position sent"; with it off → Messages with `[LOC]lat,lon` waiting in the compose field
- [ ] Track back (with a recorded trail): bar "Back: n pt" with distance / bearing, ring on the breadcrumb; walking advances it; at the start toast "Back at the trail start"; ✕ stops it
- [ ] ☰ > Download this area opens the download popup
- [ ] Waypoint averaging 5 s/10 s/30 s: pin → "Averaging GPS… n s" pill (tap cancels) → waypoint saved at the mean

## Map > ☰ > Options (schema-driven)
- [ ] Every row changes and survives a reboot; Settings > Display & power > "Imperial units" relabels point spacing and distances
- [ ] Changing "Stop sharing after" during a session restarts its clock
- [ ] Radius / Alert on (arrive, leave, both) change when the arrival toast fires

## Same data on L1 (ui-new) after the Core changes
- [ ] L1: waypoints saved before the update are still listed (Tools › Trail › Waypoints)
- [ ] L1: add / rename / delete waypoint, navigate, ETA line still shows
- [ ] L1: Home GPS toggle still works
- [ ] L1: Tools › Trail Save / Load / Reset still work (now via TrailEngine)
- [ ] L1: Mark with averaging (Trail › Settings › Mark avg) still counts down and marks
- [ ] L1: Track back still advances along the trail and ends at the start

## Roadmap stage 4: UI layout (2026-09-26)
- [ ] Home page 3: Repeater, Admin, Diagnostics tiles open their screens; back (arrow or USER button) returns to Home
- [ ] Home > Admin: repeaters and room servers listed, favourites (star) first; tap → login / admin tabs; back leaves to the list; with none heard yet a short note instead
- [ ] Settings order like L1: DISPLAY (Display & power), SOUND, RADIO (Radio, Bluetooth, WiFi), SYSTEM (Name, GPS, Reboot, Power off), KEYBOARD, CONTACTS & MESSAGES, ABOUT; no Repeater / Diagnostics / Send advert / trail rows left there
- [ ] Settings > Display & power: UNITS section with "Imperial units"
- [ ] Map > ☰ "Map tools": TRAIL, LIVE SHARE, ARRIVAL ALERT (On/Off with mode and radius), MAP; "Options: trail, sharing, alert" opens Map options; back returns to the map
- [ ] Nearby header: back, title, advert (tower), map, sort, scan (refresh icon) -- nothing overlaps; advert popup sends Nearby only / Everyone and sets Auto-advert
- [ ] Status bar, right to left next to the battery: Bluetooth (grey on, white with the app), GPS (grey searching, green fix), bell (alarm set), mute, then amber: auto-advert (tower), trail (route), live share (pin), repeater (loop), arrival alert (flag, with a target); each appears / disappears within a second of switching it
- [ ] All icons at once still leave the clock readable on the left

## Roadmap stage 5: screen PIN (2026-09-26)
- [ ] Settings > Display & power > SECURITY > Screen PIN "Off"; tap → keypad; 1234 ✓, 1234 ✓ → toast "PIN set", row "On", list stays at the bottom
- [ ] A different second entry → "Didn't match - new PIN again"; fewer than 4 digits + ✓ → "At least 4 digits"
- [ ] Screen off (WAKE or timeout) and on → PIN card (clock, unread count, keypad) instead of the slider, even with "Lock screen" off
- [ ] Right PIN unlocks as soon as the last digit is in; wrong one → "Wrong PIN - n tries left"; 5 misses → "Too many tries - wait 30 s" counting down, keys ignored meanwhile
- [ ] USER button does nothing on the PIN card; a ringing alarm is still dismissed by the buttons
- [ ] Reboot → PIN card straight after boot
- [ ] Change PIN (row tap with a PIN set) works; "Remove" in the popup's header → "PIN removed", slider lock / no lock as before
- [ ] PIN survives a reboot and a firmware update (NVS "mc_lock"), not stored in NodePrefs

## Roadmap stage 5: live map tiles (2026-09-26)
- [ ] Map > ☰ > MAP > "Live tiles" on by default; with WiFi on and a network saved, zoom into an area without tiles → pill "WiFi Connecting..." then "live n", tiles appear one by one, the "(map zN)" magnification goes away
- [ ] The fetched tiles are on the card (/maps/z/x/y.png): reopen the map offline → still there
- [ ] Leave the map: WiFi drops ~30 s later; back within 30 s → no reconnect
- [ ] Live tiles off → nothing fetched, toast "Live tiles off"; on without a saved network → "Pick a WiFi network first"
- [ ] WiFi off in Settings → no live fetching; an area download still works as before (and takes over from live tiles)
- [ ] No SD card map folder at all + live tiles → the folder is created and the map fills in

## Roadmap stage 5: firmware update (2026-09-26, Arduino 3.x build only)
- [ ] Settings > SYSTEM > Firmware update shows the installed version; 2.0.17 build: "Not available in this build"
- [ ] Check for update: connects WiFi, asks GitHub; before a release with an L2 image: "vX has no image for this device" (proves TLS + the API work)
- [ ] With a release carrying solo-vX-Wio-Tracker-L2-ota.bin: "vX is available" + Install (size in MB); install shows progress, screen stays on, back is refused ("Updating - wait")
- [ ] After "restarting..." the device boots the new version (Settings shows it); contacts, messages, settings, PIN and WiFi kept
- [ ] Up to date → "Up to date - the latest release is vX"; WiFi off / no network / map download running → toasts instead
- [ ] Wrong network in the middle (unplug router) → "Download stalled", old firmware still boots

## Roadmap stage 6: motion and look (2026-09-26)
- [ ] Boot: splash (amber MESHCORE, SOLO, Solo version, "MeshCore x - built date", three dots rising in turn) for ~2.5 s, then fades into Home (or the PIN card); a tap skips it
- [ ] Changing screens: the new one fades in from the background with its content drifting into place (up going in, down coming back); rebuilding the same screen (settings toggles, a new message) doesn't animate
- [ ] Selected things look the same everywhere (dim accent + light text): Nearby chips, Clock / Bot / Diagnostics tabs, Repeater segments; primary actions are full accent with dark text (map Download / Resume, Firmware update, WiFi / channel Save, Clock Start, alarm Dismiss, New message)
- [ ] Settings > Display & power > LOOK > Accent colour: tap a swatch → the page, status icons, chips, names recolour at once; other screens in the new colour; kept after a reboot (splash too)
- [ ] Home page swipe / dot tap: the tile row slides in from the swipe side
- [ ] Popups (map tools, Nearby advert, scan, radio frequency, map download): backdrop fades, panel rises; toasts rise in and fade out
- [ ] Buttons shrink slightly while pressed
- [ ] Status bar icons evenly spaced, muted shows a speaker with a cross (also next to muted conversations)
- [ ] Own channel post: "1s ✓ 2" (green, number of repeaters heard relaying it) under the bubble, nothing before an echo; DM: ✓ delivered, ✗ (red) not delivered, "..." sending
- [ ] Hold a message: quote, time + hops, path diagram sender → repeaters → this device; own post: "Relayed by n repeaters" + names; own DM: Delivered / Not delivered / Sending in words
- [ ] Map download popup: no WiFi button, fits the screen (scrolls if the unfinished-job row is shown); no network saved → toast pointing to Settings > WiFi
- [ ] Unlock (slider or right PIN): the lock fades away while the screen underneath drifts up into place, no instant jump
- [ ] Lock slider: knob snaps to the end near it but only unlocks on letting go; let go earlier → it springs back smoothly
