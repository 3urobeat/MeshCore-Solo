## Screen Lock

[Go back](../../../README.md)

### Overview

|           OLED            |           E-Ink           |
| :-----------------------: | :-----------------------: |
| ![](./overview_oled.png) | ![](./overview_eink.png) |

Screen lock prevents accidental keypresses. While locked the display turns off and all input is ignored.

---

### Locking and unlocking

**Hold Back** and press **Enter** three times within 3 seconds. The sequence works in both directions — the same combination locks and unlocks.

On boards with a CardKB attached, **Fn+Esc** does the same thing in one press. Esc rather than the adjacent Backspace — those two keys sit next to each other on CardKB's layout and would be too easy to hit by accident.

If the display is off when the sequence begins, it turns on automatically so the hint is visible. Each press in the physical sequence extends the display-on timer by 5 seconds.

The hint popup at the bottom of the lock screen guides through the physical sequence:

| Step           | Hint                                                        |
| -------------- | ------------------------------------------------------------ |
| Not started    | _Hold Back + 3×Enter_ (_Back+3xEnter/Fn+Esc_ with CardKB attached) |
| 1 press done   | _Enter ×2 more…_                                              |
| 2 presses done | _Enter ×1 more…_                                              |

If no press is made for 3 seconds, the counter resets.

---

### Lock screen

|           OLED            |           E-Ink           |
| :-----------------------: | :-----------------------: |
| ![](./screen_oled.png) | ![](./screen_eink.png) |

A brief press of any button wakes the display and shows the lock screen. It displays:

- **Title bar** — battery and status icons, same as every other home page — but never the device name, so a locked device doesn't announce whose it is at a glance
- **Time** — same format as the Clock page (24 h / 12 h from Settings), left-aligned
- **Date** — day-of-week, day, month
- **Two sensor values** — the first two Dashboard Config fields (same values configured for the Clock page); shown side by side if both are set

The display turns off again automatically after 5 seconds of inactivity (or 2 seconds immediately after locking).

---

### Auto-lock

Enable **Auto-lock** in **Settings › Display** to lock the device automatically whenever the display turns off due to auto-off timeout.

---

### Magnetic cover (Hall sensor)

Optional, user-supplied hardware — no board in this repo has one built in. Wire a Hall-effect or reed sensor to any free GPIO, then set `PIN_HALL_SENSOR` (and `HALL_ACTIVE_HIGH=1`, if your module pulls the pin high rather than low when the magnet is present) as `build_flags` in your own env. No-op entirely unless `PIN_HALL_SENSOR` is defined.

Fully autonomous, independent of Auto-lock and of any key combo:

- **Magnet near (cover closed)** — locks and blanks the display immediately, no wake grace.
- **Magnet away (cover opened)** — unlocks and wakes the display right away.

---

### Lock PIN

**Settings › Display › Lock PIN** asks for a PIN before the screen unlocks,
also right after power-up, and when the magnet cover opens.

- Enter on the row opens a number pad: type the PIN (at least 4 characters),
  confirm with ✓, then type it again. The pad's keyboard key switches to the
  normal keyboard for a PIN with letters.
- Unlocking (Back + Enter three times, or opening the cover) shows the pad
  with the input masked. A wrong PIN says how many tries are left; after 5 in
  a row, entry pauses for 30 seconds.
- Enter on the row again removes the PIN.

The PIN is kept as a salted SHA-256 hash, never as the PIN itself. It locks
the screen only: messages still arrive and the phone app still connects.
On the Wio Tracker L2 it's **Settings › Display & power › Screen PIN**
(digits only).
