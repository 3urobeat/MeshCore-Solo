# Wio Tracker L2 (ui-lvgl) — hardware checklist

Things verified only in the browser simulator so far. Tick on the device;
note anything odd next to the item. Branch `wio-tracker-l2`.

## Feedback round 1 (2026-09-25) — fixed, check on the device
Confirmed on the device: live share sends and receives positions.
- [ ] Map: ☰ tools moved to the left column (back, pin, ☰); nothing overlaps the crosshair
- [ ] Target ring sits exactly on the marker (all markers were drawn ~3 px low)
- [ ] A person sharing on a channel, picked as target, is followed as they move (was a fixed point where they were); bar shows the person icon
- [ ] Settings: "Trail, live share, alerts" subtitle no longer covers the title (all list rows)
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
- [ ] Clock > Timer: H / M / S rollers, Start → big countdown, Stop; when it ends a full-screen "Timer done" card with Dismiss appears on any screen (also wakes the display; the user button dismisses too). Silent: no speaker driver yet
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

## New: Diagnostics, auto-advert, compass (2026-09-25)
- [ ] Settings > SYSTEM > Diagnostics: tabs Live / System / Font; Live counters (uptime, rx/tx, heap, noise floor, RSSI/SNR, queue, errors) tick every second
- [ ] Live > Reset → confirm popup → "Counters reset", rx/tx and forwarded go back to 0
- [ ] System shows firmware, build date, board, node name, frequency / SF / BW / CR, TX power; Font shows Polish, Greek and Cyrillic samples without boxes
- [ ] Settings > Trail, live share, alerts > LIVE SHARE > Auto-advert 30 s: another node sees your advert (with position) every ~30 s; Off stops it; survives a reboot
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
- [ ] Settings > Trail, live share, alerts > "Show others' positions" on → someone's `[LOC]` share appears on the Map and in the list; tap → navigate to them
- [ ] Node detail (a node with a position) has the compass button → Map with that node as target
- [ ] Settings > … > "Arrival alert" on + target set → toast "Arrived: …" when you get within the radius

## Positions in messages
- [ ] A received message with `[WAY]lat,lon name`, `[LOC]lat,lon` or plain "lat, lon" shows Go / Save under the bubble
- [ ] Save → waypoint named from the [WAY] label (else the sender), Polish letters not cut in half
- [ ] Go → Map with that spot as the target

## Trail & live share (Map > ☰ tools button, left column)
- [ ] Record → pill "REC 0 m" at the top; walk: blue trail line follows, distance grows
- [ ] Auto-pause (Settings > Trail, live share, alerts): standing still → "PAUSED", walking resumes
- [ ] Stop / Save / Load / Reset (second tap confirms) behave; a trail saved on L2 loads (same /trail file as L1)
- [ ] Export GPX → toast "Saved trails/trail-YYYYMMDD-HHMM.gpx"; the file opens in a GPX viewer (track + waypoints)
- [ ] Live share: pick "Send to" (channel or favourite), Share live → pill "LIVE 1h00"; another node sees the [LOC] updates while you move; stops by itself after the chosen time
- [ ] Send once: with sharing on → "Position sent"; with it off → Messages with `[LOC]lat,lon` waiting in the compose field
- [ ] Track back (with a recorded trail): bar "Back: n pt" with distance / bearing, ring on the breadcrumb; walking advances it; at the start toast "Back at the trail start"; ✕ stops it
- [ ] ☰ > Download this area opens the download popup
- [ ] Waypoint averaging 5 s/10 s/30 s: pin → "Averaging GPS… n s" pill (tap cancels) → waypoint saved at the mean

## Settings > Trail, live share, alerts (schema-driven)
- [ ] Every row changes and survives a reboot; "Imperial units" relabels point spacing and distances
- [ ] Changing "Stop sharing after" during a session restarts its clock
- [ ] Radius / Alert on (arrive, leave, both) change when the arrival toast fires

## Same data on L1 (ui-new) after the Core changes
- [ ] L1: waypoints saved before the update are still listed (Tools › Trail › Waypoints)
- [ ] L1: add / rename / delete waypoint, navigate, ETA line still shows
- [ ] L1: Home GPS toggle still works
- [ ] L1: Tools › Trail Save / Load / Reset still work (now via TrailEngine)
- [ ] L1: Mark with averaging (Trail › Settings › Mark avg) still counts down and marks
- [ ] L1: Track back still advances along the trail and ends at the start
