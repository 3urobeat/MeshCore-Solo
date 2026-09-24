# UI Core + frontends — design

Status: 📋 draft for review (2026-09-24). Branch: `wio-tracker-l2`.

## Why

The Wio Tracker L2 (320×240 colour touch LCD, no joystick) gets a new LVGL 9
interface. Rather than a second, parallel copy of everything Solo does, the
application logic moves out of `ui-new` into a hardware-independent **UI Core**
that every frontend shares:

| Frontend | Devices | Toolkit | Input |
|---|---|---|---|
| `ui-lvgl` (rich) | Wio Tracker L2 first; later other ESP32-S3 colour boards | LVGL 9 | pointer (touch) + focus (buttons, CardKB) |
| `ui-new` (lite) | OLED / e-ink / nRF52 (Wio L1, T-Echo Lite, Heltec, …) | `DisplayDriver` | focus (joystick, buttons, CardKB) |

A feature written once in the Core (live share, locator, unread tracking, a new
setting) then shows up on both. `ui-new` is not rewritten: it is re-pointed at
the Core and stays the lite frontend.

Non-goals: changing the mesh protocol, `MyMesh`, persistence formats
(`NodePrefs` layout, `/scopes1`, trail files), or the companion app protocol.

## Current state

`ui-new` is 18.5k lines. `UITask` implements `AbstractUITask` (the interface
`MyMesh` calls into) *and* hosts the screens, so logic and drawing are
interleaved. Classified:

**Models — already UI-free, move as-is**
- `MessageHistory.h` — channel (48) / DM (32) rings, unread counters + overflow flags. ✅ moved to `ui-core/`.
- `Trail.h` / `TrailStore`, waypoints (`WaypointsView` storage half), `ScopeList.h`.

**Engines — logic living inside `UITask.cpp` / `UITask.h`**
- Unread tracking — DM unread table ✅ (`ui-core/DmUnreadTable.h`), room unread (still in `UITask`).
- Notifications — `showAlert`, `notify`, `SoundNotifier`, LED (`userLedHandler`), wake-on-message (`checkDisplayOn`, auto-off).
- Live share — session timer, movement/heartbeat gate, `sendLocationShare`, scope guard, `onSharedLocation`/live-track expiry.
- Locator — geofence state machine, proximity beeper (`evaluateLocator`, `fireLocator`, targets).
- Trail — sampling, auto-pause, low-battery auto-save.
- Course over ground — `pushCogFix`, `currentCourse`, `currentLocation`.
- Clock tools — alarm / countdown / ring ✅ `ui-core/ClockEngine.h`.
- Ping — `startPing`, `handlePingResult`.
- Device controls — GPS on/off, GPIO (`setGpioMode`, bot GPIO), buzzer mode/volume, brightness, radio apply (`applyTxPower`, `applyApc`, `applyRadioParams`, …).
- Bot hooks — `botSetGPS`, `botBuzz`, `botSetGPIO`, …

**Screen-embedded logic (harder to extract)**
- `SettingsScreen.h` (1.3k lines) — ~40 items, each with inline get/format/step/apply code.
- `NearbyScreen.h` — contact discovery/sorting; `AdminScreen.h` — repeater admin session; `BotScreen.h`; `RepeaterScreen.h`; `MessagesScreen.h` (2.6k lines) — list ordering, favourites, context-menu actions mixed with rendering.

**Pure view / widgets — stay in `ui-new`**
- Rendering of every screen, `KeyboardWidget`, `PopupMenu`, `AccordionList`, `TabBar`, `icons.h`, `GfxUtils.h`, marquee, badges.

## Architecture

```
 MyMesh ──(AbstractUITask callbacks)──► UiCore ──events──► Frontend (ui-new | ui-lvgl)
   ▲                                      │  ▲                  │
   └──────────── mesh actions ────────────┘  └──── actions ─────┘
                                   engines, models, settings schema
 Platform services (per board): Sound, DisplayPower, Input sources, Led
```

### 1. `UiCore` owns the mesh-facing interface

`MyMesh` talks to the UI only through `MyMesh::Listener` (set with
`setListener()`), ported from upstream's "Abstract UI overhaul" (PR #3431 and
follow-ups `3caf033d`, `5c3d9281`, `30dd723c`, `b3b17025`, `64434c53`) with
identical names/signatures. A second block in the same interface holds this
fork's extensions (own-send mirroring, relay echoes, room login/admin replies,
`[LOC]` shares, contact/channel removal, bot device actions,
`requestShutdown`), each defaulting to a no-op.

Today `AbstractUITask` is the Listener and carries the glue `MyMesh` used to
run for the UI (display filter, room-post labelling, notifications). In the
target design `UiCore` becomes the Listener instead: it updates models/engines
and emits events, and the frontends no longer implement it at all. This is the
single point that makes both frontends receive identical behaviour.

### 2. Engines

One class per engine from the inventory above, each with `begin(prefs, …)`,
`loop(now_ms)` and a small public API. No drawing, no `DisplayDriver`, no
screen pointers. Engines that need to show something emit an event
(`AlertRequested{text, ms}`, `LocShareEnded`, `AlarmFired`, …) instead of
calling `showAlert()` directly.

Constraints (the lite frontend runs on nRF52 with ~215 KB flash / ~65 KB RAM
free): no heap allocation, no RTTI/exceptions, no STL containers, fixed-size
buffers — the same rules `ui-new` follows today. Extraction must be roughly
size-neutral on `WioTrackerL1_companion_solo_dual`.

### 3. Events: Core → frontend

A small fixed-size queue of tagged events plus coarse dirty flags
(`DIRTY_MESSAGES`, `DIRTY_CONTACTS`, `DIRTY_STATUS`, …). The frontend drains
the queue in its `loop()` and redraws what is dirty. No callbacks into the
frontend from inside mesh processing — which also keeps the e-ink busy-wait
pump rule ("never re-enter UI code from the radio path") trivially true.

### 4. Actions: frontend → Core

Plain methods on the Core facade: `sendDM`, `sendChannelMsg`, `markRead`,
`setLiveShare`, `setLocatorTarget`, `toggleGps`, `startPing`, … Both frontends
call the same ones, so e.g. "marking a channel read" has one implementation.

### 5. Declarative settings schema

Replaces the per-item code in `SettingsScreen`:

```cpp
struct SettingDef {
  const char* id;          // stable key, also used for search / CLI
  const char* label;
  uint8_t     section;     // Radio / System / Display / …
  SettingType type;        // Bool, Enum, Int, Text, Action
  // accessors over NodePrefs; enum labels; min/max/step
  int  (*get)(const NodePrefs&);
  void (*set)(NodePrefs&, int);
  const char* (*label_for)(int v);        // Enum/Int formatting
  bool (*visible)(const NodePrefs&);       // build- or state-dependent rows
  bool (*locked)(const NodePrefs&, const char** why); // "Off while repeating"
  void (*apply)(UiCore&);                  // side effect after set (radio, display…)
};
```

`ui-new` renders it as today's accordion list; `ui-lvgl` as switches, dropdowns
and sliders. Build-time gating (`FEAT_*`) stays in the table via `#if`, so a
hidden row costs no flash.

### 6. Platform services

Per-board implementations behind small interfaces: `Sound` (buzzer PWM today;
I2S ES8311 tone generator on L2), `DisplayPower` (auto-off, brightness), `Led`,
and input sources. Input reaches frontends in two forms: **focus keys** (the
existing `KEY_*` codes — joystick, buttons, CardKB) and **pointer events**
(`down/move/up` with coordinates — touch). LVGL consumes both natively; `ui-new`
consumes keys only.

## `ui-lvgl` specifics

- LVGL 9 (pinned), memory in a PSRAM pool; partial-render buffers in internal RAM, flush over QSPI via LovyanGFX.
- Theme tokens (colours, spacing, typography) in one place — the first chance to realise the "Amber Trace" direction in colour.
- Fonts with full Latin/Polish/Cyrillic coverage.
- Runs in the WASM sim at 320×240 with mouse as touch, so most UI work needs no flashing.
- PR #3381's LVGL UI is a reference for platform glue (flush, GT911, PSRAM pool) only; its screens are not adopted.

## Migration plan

Each step keeps `WioTrackerL1_companion_solo_dual` behaviour identical and is
checked in the sim plus on L1 hardware before the next one.

0. **Listener boundary** ✅ (upstream `MyMesh::Listener` ported; `MyMesh` has no UI calls left).
1. **Skeleton** ✅. `examples/companion_radio/ui-core/` with `UiCore.h` (facade), `MessageHistory.h`, `DmUnreadTable.h`. Header-only for now, reached from `ui-new/UITask.cpp` by relative include, so none of the 66 variant `platformio.ini` files that build `ui-new` change. `UITask` heap-allocates one `UiCore` in `begin()` (before the screens, as `MessagesScreen` used to own the history on the heap); `MessagesScreen` binds to `core().history` by reference. When the Core grows real `.cpp` files, compile them through a unity `.cpp` inside each frontend directory.
2. **Engines, one per commit.** Clock tools ✅ (also introduced `ui-core/UiEvents.h`, the Core → frontend event queue; `UITask::tickCore()` runs `UiCore::loop()` and drains it) → ping → course-over-ground → live share → locator → trail → notifications. `UITask` shrinks to screen management + drawing.
3. **Flip the interface.** `UiCore` implements `AbstractUITask`; `ui-new`'s `UITask` becomes a frontend fed by events.
4. **Settings schema.** Convert `SettingsScreen` section by section.
5. **`ui-lvgl` skeleton** for L2 + sim target: boot, home, message list/conversation, keyboard. Then screens by priority.
6. Contacts/Nearby, Admin, Bot logic extraction as the LVGL screens for them are built.

## Decisions

- **Upstream merge cost accepted** (2026-09-24). Moving code out of `ui-new`
  will conflict with upstream edits to the same files; this fork's `ui-new` is
  already heavily diverged, and upstream is itself working toward a larger UI
  abstraction — revisit alignment when that lands.
- **Scope is UI only** (2026-09-24). The settings schema and event queue serve
  the on-device frontends; CLI and companion-app settings/push paths are
  untouched for now.
