# Getting started

## Controls

On joystick devices:

| Input | Does |
| ----- | ---- |
| **Up / Down** | Move through a list |
| **Left / Right** | Switch home pages; change the value of a setting |
| **Enter** | Open, select, confirm |
| **Hold Enter** | Options for the selected item (a message, contact, channel…) |
| **Back** | One step back; from a screen, back to the home screen |

Lists wrap around at both ends. Any key wakes a dark screen without acting on it.

Keyboard devices use the same controls, from the arrow keys (or their Fn
combinations), Enter and Esc. [Hardware](./hardware.md) has the key maps.

> [!NOTE]
> **Wio Tracker L2:** tap to open, hold for options, swipe sideways between
> pages. The top button turns the screen off and on. The side button goes
> back to the home screen; hold it and let go to mute or unmute the sound.
> Holding the side button while pressing the top one takes a screenshot.

## Home screen

The home screen is a row of pages. On joystick devices, **Left / Right** steps
through them and **Enter** opens the one shown:

Clock, Recent, Radio, Bluetooth, Advert, GPS, Sensors, Tools, Shutdown,
Settings, Messages, Favourites, Map.

Settings › Home Pages sets their order and hides the ones you don't use;
Settings and Messages are always shown.

> [!NOTE]
> **Wio Tracker L2:** pages you swipe between: favourite chats, the clock
> (where it starts), a minimap, then the apps, six to a page. Hold an app to
> arrange or hide them.

## Connecting the phone app

Every build serves the MeshCore app over **Bluetooth and USB**, one at a time:
while Bluetooth is connected, USB is ignored. To use USB, disconnect Bluetooth
first or turn it off on the device.

The Bluetooth pairing PIN is shown on the Bluetooth home page until the phone
is paired.

> [!NOTE]
> **Wio Tracker L2:** the PIN is under Settings › Bluetooth.

Everything you do on the device and in the app stays in sync: contacts,
channels and messages are the same data.

## Updating

Download the new file from the [releases page](https://github.com/MarekZegare4/MeshCore-Solo/releases)
and flash it the way you did the first time (see the [main README](../../README.md#flashing)).
Settings, contacts and messages are kept.

> [!NOTE]
> **Wio Tracker L2:** Settings › Firmware update checks GitHub for a newer
> release and installs it over WiFi. Set up a network under Settings › WiFi
> first.
