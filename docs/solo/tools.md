# Tools

On joystick devices, the tools are in **Tools**, grouped into Location, Comms
and System. The navigation tools are on the [Navigation](./navigation.md)
page, Nodes and Admin on [Contacts](./contacts.md).

> [!NOTE]
> **Wio Tracker L2:** the tools are apps on the home screen: Messages, Nodes,
> Settings, Compass, Clock, GPS, Bot, Repeater, Admin, Diagnostics. The trail,
> live sharing and the locator are on the map (its tools button) and in
> Settings › Map.

## Clock

The **Clock** home page shows the time, the date and up to three fields you
choose (hold Enter): battery, temperature, humidity, pressure, altitude
(barometric or GPS), light, CO₂, GPS position, satellites, contacts, unread
messages. The time comes from GPS or the phone app; the time zone is in
Settings.

**Enter** on the clock opens the clock tools:

- **Alarm**: a time, repeating daily, on weekdays, at weekends or once. It
  rings with the screen off or locked, but not after a shutdown.
- **Timer**: a countdown that rings when it reaches zero, whatever screen
  you're on.
- **Stopwatch**.

Any key silences the ringing; it stops by itself after a minute.

## Remote bot

**Tools › Bot** answers messages for you. It watches your direct messages, one
channel and one room, each switched on separately:

- **Trigger and reply**: when a message contains the trigger (several can be
  separated by commas, `*` matches any message), the bot sends the reply. The
  reply can use placeholders, plus `{name}` (the sender) and `{hops}`.
- **Commands**: messages starting with `!` get live data back:

  | Command | Reply |
  | ------- | ----- |
  | `!ping` | `pong` |
  | `!batt`, `!temp`, `!time`, `!loc` | battery, temperature, time, position |
  | `!hops` | how many hops the command took |
  | `!status` | battery, position and time |
  | `!help` | the list of commands |

  Several in one message get one reply: `!batt !time` → `4.10V | 14:30`.
- **Actions** (off by default) let the command change the device: `!buzz`
  (sounds the buzzer to find it), `!gps on` / `!gps off`, `!gps fix` (turns
  the GPS on, waits for a good fix and sends it), `!advert`.

**DM allow = Fav** limits the DM bot to your favourites, and **quiet hours**
stop trigger replies at night (commands still answer). Replies are rate
limited so two bots can't answer each other forever. The room bot uses the
room's saved login.

## Auto-advert

**Tools › Auto-advert** sends an advert with your position every 30 seconds
to 1 hour, so others see you in their Nearby list. With it on at both ends
and Settings › Sound › **AD sound** set, each device beeps when it hears the
other: a hands-free "still in range".

## Repeater

**Tools › Repeater** makes the device relay other people's packets while it
keeps working as a companion. By default it relays on a separate repeater
profile (the community's repeater frequency) and switches back when you turn
it off; **Network = Current** relays on your own frequency instead.

Optional filters keep a mobile repeater from adding noise: skip adverts, a
maximum hop count, **Yield** (let fixed repeaters go first), a minimum SNR,
drop duplicates already relayed by someone else, and relay only your
**scopes**. Diagnostics shows how much it forwards.

## Ringtones

**Tools › Ringtone** composes two melodies of up to 32 notes (pitch, octave,
length, tempo). Use them for notifications in Settings › Sound, or for a
single contact or channel from its options.

> [!NOTE]
> **Wio Tracker L2:** the melody editor is under Settings › Sound.

## Diagnostics

**Tools › Diagnostics** shows live counters (packets received and sent by
type, forwarded, errors), memory, the radio's noise floor and the last
packet's signal; a **System** tab with the firmware and radio settings; and a
**Font** tab with a sample of every script the font covers. Hold Enter on the
live tab to reset the counters.

> [!NOTE]
> **Wio Tracker L2:** the Diagnostics app, with a **Noise** tab that
> measures the noise floor over time.

## GPIO

*Wio Tracker L1 only.* **Tools › GPIO** sets four spare pins as input,
output or (GPIO1–2) analog input, shows their level and switches outputs. The
bot's `!gpio1`…`!gpio4` commands read and set the same pins.

## GPS and sensors

The **GPS** home page shows the fix and satellites; **Sensors** shows the
readings of the sensors the board has. **GPS pwr** in Settings › System turns
the GPS off between fixes to save battery; anything that needs your position
(the trail, live share, the locator) keeps it on.

> [!NOTE]
> **Wio Tracker L2:** the **GPS** app draws a sky plot of the satellites and
> each one's signal strength.
