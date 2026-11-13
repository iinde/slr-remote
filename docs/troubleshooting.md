# Troubleshooting

Most of the time the remote tells you what it needs in plain words on its own screen. This page is for the rest. Find your symptom, check the likely causes in order, try the fix. Screen words are shown `LIKE THIS`, exactly as they appear.

Two tools help with almost everything:

- **The LINK page** on either unit shows signal, speed, success rate (handheld only) and airtime used. On the handheld: mode card, `VIEWS`. On the camera unit: click PRG with the screen on.
- **The serial console.** Plug the board into a computer by USB-C, open a terminal at 115200 baud (`pio device monitor`) and type `stat`. See the [User guide](user-guide.md#serial-console-reference).

Some terms used below: **LoRa** is the long-range radio between the two units. **BLE** is Bluetooth Low Energy, the short link from the camera unit to the camera. **Spreading factor (SF)** is LoRa's speed setting, 7 (fast) to 12 (slow, long range). **Duty cycle** is the legal cap on airtime: 10% of any rolling hour per board.

## Contents

- [Boards will not link](#boards-will-not-link)
- [Stuck on SLOW](#stuck-on-slow)
- [NO REPLY](#no-reply)
- [Camera will not connect](#camera-will-not-connect)
- [Camera goes to sleep](#camera-goes-to-sleep)
- [PRESSED shows but no photo was taken](#pressed-shows-but-no-photo-was-taken)
- [Duty limit reached](#duty-limit-reached)
- [Flashing fails](#flashing-fails)
- [Screen is blank](#screen-is-blank)
- [Tally LED dead after sleep](#tally-led-dead-after-sleep)
- [Button sticks in the printed case](#button-sticks-in-the-printed-case)
- [Setup menu opens by itself](#setup-menu-opens-by-itself)
- [Still stuck](#still-stuck)

## Boards will not link

The handheld shows `NO LINK`, `NOT CHECKED`, `PAIR BOARDS` or `NOT PAIRED`, or the camera unit shows `BOARDS NOT PAIRED`.

| Symptom | Likely cause | Fix |
|---|---|---|
| Handheld shows `NOT CHECKED`, `press any button to connect` | Normal after a power-on. The handheld sends nothing until you touch the button, so a missing antenna gets noticed first. | Press the button once. |
| `PAIR BOARDS`, `Tap RST twice` on the handheld | The boards have never been paired. | Tap RST twice for the setup menu and run `Pair boards` on both. See [Flashing and pairing](flashing-and-pairing.md). |
| `NOT PAIRED` on the handheld when you press, `BOARDS NOT PAIRED` on the camera unit | The boards have never been paired, or one was factory reset. | Run `Pair boards` on both. See [Flashing and pairing](flashing-and-pairing.md). |
| Handheld has no `Pair boards` item and no `FAST`/`SLOW` word | The handheld is in Direct mode. | Setup menu, `Mode`, set `Relay`, then `Exit`. |
| Pairing shows `Pair timed out` | `Pair boards` was not started on both boards within 120 seconds, or they are too far apart. Pairing uses the lowest power setting on purpose. | Put the boards next to each other and start `Pair boards` on both at about the same time. |
| `PAIR ABORTED`, `Other pairing nearby?` | Another board was pairing at the same moment, or a stray pairing message was heard. | Wait a moment and start again on both. |
| `UNSURE: PAIR AGAIN` or `PAIR UNSURE` | The last step did not complete. One board may hold the new key and the other the old one. | Pair again on both. |
| `DUTY LIMIT`, `Pairing waits for airtime` | The board has used its airtime for the hour. | Wait and retry. See [Duty limit reached](#duty-limit-reached). |
| Paired, but `NO LINK` every time | The camera unit is off. It does not turn itself on. | Click PRG on the camera unit. Its screen should light. |
| Paired, but `NO LINK` every time | The two boards are running different firmware builds. A mixed pair will not talk. | Flash both boards from the same build. |
| Paired, but `NO LINK` every time | A fixed spreading factor is set on one board and not the other. | Set `SF` to `Auto` on the handheld, or type `sf auto` on both and reboot both. |
| Worked on the bench, fails a few metres apart | Antenna loose or missing, or the wrong kind. | Check both antennas are screwed on firmly and are 868 MHz antennas. Never power a board without one. |
| `RADIO FAILED`, `Presses not sent` | The radio chip did not start. | Restart the board. If it persists, reflash. If it still persists, suspect the board. |

After a link is lost the two boards find each other again by themselves: the handheld shows `RESCUE SF12`, then `LINK FOUND`. At the slowest setting this can take a while, because each search message is long.

## Stuck on SLOW

The top bar says `SLOW` even though the units are close together.

The firmware steps down to a faster setting only once it has seen several good pings in a row with plenty of signal in hand. Anything that stops pings, or keeps the signal marginal, keeps it on `SLOW`.

| Symptom | Likely cause | Fix |
|---|---|---|
| `SLOW`, and the LINK page says `FIXED` | A fixed spreading factor of 10 or more is set. | Setup menu, `SF`, set `Auto`. Check the camera unit too (`sf auto` on the console, then reboot). |
| `SLOW` right after moving closer | It has not had enough pings yet. On the slowest setting pings are over a minute apart unless the signal is very strong. | Keep the handheld awake and wait. With a strong signal it speeds up within a few pings. |
| `SLOW` every time you pick it up | The handheld sleeps after 60 seconds idle and does not ping while asleep. | Normal. It remembers its progress through sleep, so a few presses or a minute awake will bring it down. |
| `SLOW` with `MARGIN` under about 12 dB on the LINK page, at short range | Weak signal: a poor or loose antenna, an antenna for the wrong band, or the camera unit boxed in by metal. | Check antennas at both ends. Move the camera unit clear of metal. Use the HUNT page to find a better spot. |
| `SLOW` and a `DUTY: PINGS PAUSED` or `CAM DUTY: PINGS HELD` toast | One board is short of airtime, so pings have stopped and the link cannot be re-measured. | Wait for the airtime to recover. Presses still work meanwhile. |
| `SLOW!` shown inverted with a `!` | Rescue: the boards have lost each other and are searching. | Wait for `LINK FOUND`. If it never comes, see [Boards will not link](#boards-will-not-link). |

`SLOW` is the remote doing its job. At distance or through walls it is the setting that keeps the shutter under your thumb.

## NO REPLY

A press shows the toast `NO REPLY` and the result `UNKNOWN`, with `NO REPLY: MAY HAVE FIRED`.

This means the handheld sent the press up to four times and heard nothing back. It cannot tell whether the camera unit missed the press, or received it and the reply was lost. **Check the camera before assuming either way.**

| Symptom | Likely cause | Fix |
|---|---|---|
| `NO REPLY` at the edge of range | You are out of reach, or something solid has come between the units. | Move closer or into the clear. After three misses the units drop to the long-range setting and search (`RESCUE SF12`, then `LINK FOUND`). |
| `NO REPLY` at short range, every press | The camera unit is off or its battery is flat. | Click PRG on the camera unit. Charge it if the screen stays dark. |
| `NO REPLY` just after the camera unit was restarted | The handheld is still on the speed setting the camera unit used before. | Press again. The handheld tries the saved setting and the slowest one in turn, and will find it. |
| `NO REPLY: CAM DUTY` | The camera unit has nearly used its airtime for the hour and did not answer. | Wait. See [Duty limit reached](#duty-limit-reached). |
| `LATE: CAM GOT IT` appears afterwards | The reply was only delayed. The press did reach the camera unit. | Nothing to fix. Treat that press as delivered. |
| `NO RESULT` instead of `NO REPLY` | The camera unit confirmed it received the press, but its result message never arrived. | The handheld asks once more by itself. The camera unit never runs the same press twice. If it ends as `UNKNOWN`, check the camera. |
| `CAM BOARD RESTARTED` | The camera unit restarted and refused a press sent before the restart. | Press again. |

A retry by the handheld can never cause a second photo. Each press has an identity, and the camera unit remembers the ones it has already run.

## Camera will not connect

The camera unit shows `NO CAM`, `SCAN`, `FAIL`, or presses fail with `CAM NOT READY`.

| Symptom | Likely cause | Fix |
|---|---|---|
| `NO CAM`, `no camera saved` | No camera has been paired with this board. | Setup menu, `Pair camera`. Put the camera into Bluetooth pairing registration first. |
| `PAIR CAMERA` on a direct handheld | No camera saved. | Setup menu, `Pair camera`. |
| No `Pair camera` item on the handheld | In Relay mode the camera pairs with the **camera unit**, not the handheld. | Do the pairing on the camera unit. |
| `PAIR CAM` counts down from 60 and ends in `Camera pair failed` | The camera was not in pairing registration, or Bluetooth is off on the camera. | Turn Bluetooth on in the camera's menu, open pairing registration, then start `Pair camera` again. Approve any prompt on the camera. |
| Pairing fails repeatedly, or worked once and never reconnects | An old pairing is stored on one side and not the other. | On the board: `Forget camera` (or `forget-cam` on the console). On the camera: delete the old pairing if its menu offers that. Then pair again from scratch. |
| Stuck on `SCAN` with the camera switched on | The camera is not advertising: Bluetooth is off, or the camera is asleep. | Check the camera's Bluetooth setting and wake it. |
| Stuck on `SCAN`, started working by itself a little later | After failed attempts the camera unit waits before the next try: 2, 5, 10, then 30 seconds. After about 5 minutes of not seeing the camera it scans less often to save battery. | Send a press from the handheld. Any command puts scanning back to full speed. |
| `SECURE FAIL` or `HANDSHAKE FAIL` | The secure Bluetooth setup did not complete. | Forget the camera on the board and pair again. |
| Connects, then drops when the phone app is opened | The camera may prefer one Bluetooth partner at a time. | Close the app and give the camera unit a moment to reconnect. |
| Your camera is not an X-E5 | The firmware was built and tested on the X-E5. | Other Fujifilm bodies with Bluetooth remote release are worth a try. Tell me how you get on. |

Once paired, you should not need to do anything. The camera unit reconnects by itself whenever the camera reappears, and the CAMERA page should read `READY`.

## Camera goes to sleep

It worked, you left it for a while, and now presses fail with `CAM NOT READY` or the camera unit shows `SCAN`.

| Symptom | Likely cause | Fix |
|---|---|---|
| First press after a long wait fails with `CAM NOT READY` | The camera has powered down by its own auto power off setting, and the Bluetooth link went with it. | Wake the camera. The camera unit reconnects by itself. |
| It keeps happening | The camera's auto power off time is short. | Lengthen or disable auto power off in the camera's own menu. This costs camera battery. |
| Camera is awake again but the unit still shows `SCAN` | The camera unit had slowed its scanning after not seeing the camera for about 5 minutes. | Send a press from the handheld, or wait. A command restores full-speed scanning. |
| A hold or bulb ended early | The camera dropped the link, or the camera unit stopped hearing the handheld and released for safety (`HOLD RELEASED: NO LINK`). | Check the link and the camera's power settings before a long exposure. |

The remote presses the shutter of a camera that is awake. It cannot switch on one that has fully powered down, so for a long unattended session (a camera trap, an all-night time-lapse) set the camera's own power saving to suit before you leave it.

## PRESSED shows but no photo was taken

The handheld says `PRESSED` (or `CAM EVT`) and there is no new frame on the card.

`PRESSED` means the camera accepted the shutter press. Whether it then exposed a frame is the camera's business, and it does not report that back. So when a frame is missing, the cause is nearly always on the camera side, and the usual suspect is focus.

| Symptom | Likely cause | Fix |
|---|---|---|
| Missed frames in dim light or on low-contrast subjects | Autofocus did not lock, and the camera refused to fire. | Give it something to focus on, or switch the camera to manual focus. |
| Missed frames on taps, fine on a hold-then-release | The focus wait is too short for the lens or the light. A tap waits a fixed time between focus and shutter (500 ms by default). | Setup menu, `Focus wait`, raise to 800 or 1200 ms. Or hold to focus, check `AF HELD`, then let go. |
| Nothing ever fires, in any light | The camera itself cannot shoot: no card, card full, wrong mode, or a menu open on the camera. | Press the camera's own shutter button. If that does nothing either, fix the camera first. |
| VIDEO mode took a still instead of rolling | The camera is in a stills mode. The camera decides what a press means. | Switch the camera to movie mode. A press then starts recording and the next one stops it. |
| VIDEO shows `REC ?`, `REC STATE UNKNOWN:` | The handheld slept, left VIDEO mode, or a press went unanswered while the camera may have been recording, so its own count is no longer certain. | Look at the camera. The bar reads `TAP: PRESS`: tap once if you need to change what the camera is doing. |
| Result is `UNKNOWN` with `CAM MAY HAVE FIRED` | The Bluetooth write started but the camera never confirmed it. | Check the camera. The firmware never retries a shutter press by itself, because a retry could take two photos. |

Set the camera to AF-S (single autofocus) for your first tests. Take one test frame and look at it on the camera before you walk away, and you will know the whole chain is working.

## Duty limit reached

You see `NOT SENT: DUTY`, `DUTY: PINGS PAUSED`, `CAM DUTY: PINGS HELD`, `AIR LIMIT`, or the `AIR n%` figure is near zero.

Each board may transmit for at most 10% of any rolling hour (360 seconds). This is a legal limit for the band and the firmware enforces it. It is the price of using a band that reaches this far, and the firmware handles it for you.

| Symptom | Likely cause | Fix |
|---|---|---|
| `DUTY: PINGS PAUSED` | The handheld has used 80% of its airtime. Background pings stop so that the rest is kept for presses. | Nothing needed. Presses still go. Airtime comes back as the hour rolls on. |
| `CAM DUTY: PINGS HELD` | The camera unit is short of airtime and has stopped answering pings. | Nothing needed. It still answers presses. |
| `NOT SENT: DUTY` | The handheld has used all of its airtime. The press was refused and nothing fired. | Wait. The oldest airtime drops off minute by minute. |
| Airtime drains quickly | You are on `SLOW`. A message at the slowest setting takes more than twenty times as long as at the fastest. | Get closer or get a clearer path so the link returns to `FAST`. |
| Airtime drains quickly while setting up | The HUNT page pings several times faster than normal. | Leave the HUNT page when you have found your spot. |
| INTERVAL will not start: `needs AIR n% have n%` | The series would not fit in the airtime left. | Take one of the suggestions on screen (a longer gap or fewer shots), or use Direct mode, which has no airtime limit. |
| INTERVAL stopped with `AIR LIMIT` | Three shots in a row were skipped to stay inside the limit. | As above. |
| Airtime looks used up straight after a power cut | After losing power the board assumes the worst about airtime it had not yet saved, so it can never under-count. | Wait. This is deliberate. |
| `DUTY STORE FAIL` | The board could not save its airtime record to flash. It stops transmitting rather than risk breaking the limit. | Restart. If it returns, do a factory reset. If it still returns, suspect the board's flash. |

Idle pinging uses a small share by itself: by my calculation about a fifth of the handheld's allowance and under a third of the camera unit's, if left awake for the whole hour.

## Flashing fails

| Symptom | Likely cause | Fix |
|---|---|---|
| Upload cannot find the board | The board is not in its bootloader, or the handheld is asleep (a sleeping handheld has no USB port). | Hold **PRG**, tap **RST**, release PRG. Then run `pio run -e heltec_v4 -t upload` again. |
| Upload cannot find any port at all | A charge-only USB cable. | Use a USB-C cable that carries data. |
| Black screen after an upload or after pressing buttons | The chip is in download mode, waiting for firmware. | Tap RST once. |
| Black screen after trying to open setup with PRG | PRG was held while RST was released, which enters download mode. | Tap RST again. Next time press PRG just after tapping RST, not before. |
| Build fails on a library error | Library versions differ from the ones the project pins. | Build with the project's own `platformio.ini`, unmodified, so PlatformIO fetches the pinned versions. See [firmware/README.md](../firmware/README.md). |
| Both boards flashed, but they will not talk | They were flashed from different builds. | Flash both from the same build, one after the other. |
| Board restarts into the setup menu after flashing | A new board has no role yet. | Expected. Set `Role`, then `Exit`. |
| Serial monitor shows nothing | Wrong baud rate, or the handheld went to sleep. | Use 115200. Click PRG to wake the handheld. On a computer's USB port it then stays awake. |

A USB upload, or opening the serial monitor, restarts the board. Those restarts do not count as RST taps, so they will not open the setup menu by accident.

Full steps are in [Flashing and pairing](flashing-and-pairing.md).

## Screen is blank

| Symptom | Likely cause | Fix |
|---|---|---|
| Camera unit screen dark, unit otherwise working | Normal. Its screen turns off after 10 seconds to save power. | Click PRG. An incoming press also lights it. |
| Handheld screen dark | Normal. It sleeps after 60 seconds idle. | Press PRG. By default that press also takes a photo in SHOT mode. |
| Handheld screen went dark during an interval or bulb | Normal. The screen dims after 15 seconds and goes dark after 60 during a long run. | Any press brings it back. Be aware that the same press also stops the run. |
| Dark, and PRG does nothing | You turned it off, and the battery has since gone flat. Or the battery is simply flat. | Plug in USB-C to charge, then press PRG. |
| Dark, PRG does nothing, battery is fine | The chip is in download mode. | Tap RST once. |
| Dark, and it wakes only now and then by itself | PRG was held down for 15 seconds (`BUTTON STUCK`), so the board slept and is checking every 30 seconds for the button to be let go. | Free the button. See [Button sticks in the printed case](#button-sticks-in-the-printed-case). |
| Showed `NO ROLE`, `Sleeping: PRG wakes`, then went dark | The setup menu was left with no role chosen. | Press PRG, set `Role`, then `Exit`. |
| Never lights, even on USB, after tapping RST | Board fault, or the firmware is not on it. | Reflash. If it still shows nothing, suspect the board or its screen. |

## Tally LED dead after sleep

| Symptom | Likely cause | Fix |
|---|---|---|
| Tally LED worked, then never lit again after the unit slept or was turned off and on | Firmware older than 0.3.0. One of the tally pins stayed latched from sleep and was never freed on wake. | Update both boards to 0.3.0 or later. This was fixed in 0.3.0 and checked on the bench after an off and on. Check your version under `About` in the setup menu. |
| Tally LED has never lit | It is off by default. | Setup menu, `Tally LED`, pick the wiring you used, then `Exit`. From the console the setting needs a reboot to apply. |
| Never lit, setting is right | LED fitted backwards. | For `GPIO3+4`: positive leg through the resistor to GPIO3, negative leg to GPIO4. For a single pin: positive leg to that pin, other leg to ground. |
| Setting will not stick on the handheld | The chosen pin is in use by the aux button or the two-stage switch. | Pick a pin the buttons do not use. The menu skips clashing pins and tells you when it turns one off. |
| Does not light on a half-press (focus only) | By design. The tally lights for an accepted shot and while the shutter is held down, not for focus. | Nothing to fix. |
| Onboard LED does nothing | `LED` is set to `Off`. | Setup menu, `LED`, set `Dim` or `Bright`. |

## Button sticks in the printed case

| Symptom | Likely cause | Fix |
|---|---|---|
| Shoot button jams or stays down, and your top has a wide oval button | That was the first design. The oval cap caught in its opening on real prints, and I replaced it. | Reprint only the tops and shoot buttons from `tops_and_shoot_buttons.3mf` in [case/final/miniworks_refined/print](../case/final/miniworks_refined/print). The bottoms and reset buttons are reused. |
| Round button is tight or gritty | Brim left on the button's foot, or strings and blobs in the bore. The gap all round is only 0.25 mm. | Trim the brim off completely. Clean the bore. Check the button drops freely through the top before closing the case. |
| Button will not go in | It is being pushed in from the outside. | The buttons load from the **inside** of the top half, before the halves are joined. Their feet are wider than the hole, which is what keeps them captive. |
| Button stays pressed once the case is screwed shut | Something inside is pressing on it: a trapped battery wire, or screws done up unevenly. | Open the case, reroute the wires, and tighten the four screws a little at a time. |
| Handheld keeps showing `BUTTON STUCK` and sleeping | The firmware is seeing PRG held for 15 seconds. It is protecting the camera from a jammed button. | Fix the mechanical cause above. Nothing fires while it is stuck. |
| Setup menu or download mode on every power-up | PRG is being held down at power-on by the stuck button. | As above. Tap RST once the button is free. |

The round knurled button is the fix for all of this. More on the case is in [case/README.md](../case/README.md) and the [Build guide](build-guide.md).

## Setup menu opens by itself

| Symptom | Likely cause | Fix |
|---|---|---|
| Menu opens after you tap RST twice in quick succession | That is the shortcut: two RST taps within 3 seconds. | Tap RST once only for a plain restart. Or turn the shortcut off: setup menu, `Reset taps`, `Off` (console: `rstgest off`). |
| Menu opens on every power-on | PRG is held down at power-on, usually by a sticking case button. | Free the button. See [Button sticks in the printed case](#button-sticks-in-the-printed-case). |
| Menu opens on every start and `Role` shows `--` | The board has no role, either because it is new or because it was factory reset. | Set `Role`, then `Exit`. With no role, `Exit` refuses with `Pick a role first`. |
| Menu appears, then leaves after 30 seconds with `NO INPUT` | It was opened by RST taps and never touched. | Nothing to fix. It exits by itself and starts normally. |
| Handheld restarts into the menu after a mode card pick | `SETUP` was chosen on the mode card and confirmed with a hold. | Choose `Exit` to go back. A tap at the `SETUP?` question answers no. |

## Still stuck

1. Plug the board into a computer and type `stat` at 115200 baud. Copy the output.
2. Note exactly what each screen says, word for word.
3. Note the firmware version from `About`. Both boards must show the same.
4. Open an issue on the project's GitHub page with those three things and a description of what you did.

Related pages: [User guide](user-guide.md), [FAQ](faq.md), [How it works](how-it-works.md), [Safety and legal](safety-and-legal.md).
