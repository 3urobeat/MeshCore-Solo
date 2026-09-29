# Screen lock

The lock keeps pocket presses from doing anything. It locks the screen only:
messages still arrive, alarms still ring and the phone app still connects.

## Locking and unlocking

**Hold Back and press Enter three times** within 3 seconds; the same locks
and unlocks. A hint on the lock screen counts the presses. With a CardKB,
**Fn+Esc** does it in one press.

**Settings › Display › Lock screen** locks the device whenever the screen
turns off by itself.

The lock screen shows the time, the date and the first two of the Clock
page's fields, but not the device's name.

> [!NOTE]
> **Wio Tracker L2:** with **Lock screen** on (Settings › Display), waking
> the screen shows a clock card with **slide to unlock**. Settings › Display
> › **Tap to wake** decides whether a tap wakes it or only the top button.

## PIN

**Settings › Display › Lock PIN** adds a PIN to unlocking, asked also right
after power-up.

- Enter on the row opens a number pad. Type the PIN (at least 4 characters),
  confirm, then type it again. The pad's keyboard key switches to letters.
- A wrong PIN shows how many tries are left; after 5 in a row, entry pauses
  for 30 seconds.
- Enter on the row again removes the PIN.

The PIN is stored as a salted hash, never as the PIN itself.

> [!NOTE]
> **Wio Tracker L2:** **Settings › Display › Screen PIN**, digits only. With a PIN, the lock card comes up every time the screen wakes, and
> the SD card isn't offered as a USB drive until it's unlocked.

## Magnetic cover

A Hall or reed sensor wired to a free pin (`PIN_HALL_SENSOR`, see
[Build flags](./developer/build-flags.md)) locks and blanks the screen when a
magnetic cover closes and unlocks it when it opens (asking for the PIN, if
one is set).
