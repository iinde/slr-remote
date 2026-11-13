# Changelog

What changed in each version of the SLR Remote firmware, newest first. Each entry starts with what you get on a shoot.

For what the remote does, see the [README](README.md). For how the link works, see [docs/how-it-works.md](docs/how-it-works.md).

## 0.3.0 (November 2026)

The trustworthy one. I went through all of the firmware line by line, about 13,000 lines, and fixed what I found: 39 issues, none of them in security or in the legal airtime limit. Three minor ones are left open on purpose. The fixes marked "bench checked" were then confirmed on the real boards.

### Safety

- A held half-press lets go quickly if the handheld dies. Lose power or the link while you are holding focus and the camera unit releases the camera by itself. Bench checked: about 10 seconds on the fast link setting.
- That release time now follows the link speed the handheld was last heard on, so it stays short even when the camera unit has dropped to its slowest setting to go looking for the handheld.

### Link reliability

- Both units start cleanly every time: screens up, radio up. Bench checked.
- The tally LED works after switching off and on again. A pin that stayed latched from sleep is now freed on every start. Bench checked.

### Speed

- Wake and shoot with no warm-up wait. The press that wakes the handheld from sleep goes on air straight away. The two second pause before the first transmission now only happens after a full power-on, where it gives you a moment to notice a missing antenna. Bench checked.

### Smaller fixes

- The rest of the review findings, tidying edge cases across the firmware.
- 147 automated unit tests run on a PC, with no hardware needed.

## 0.2.0 (November 2026)

The project became SLR Remote (Super Long Range Remote). It was called XE5 Remote before.

- A proper identity. The new name is on the start-up screen and the About card, with the role (handheld or camera unit) and the firmware version.
- Animations. An iris opens on the start-up screen, and a small iris snaps shut and open on the handheld when a shot is accepted, so you can see it land without reading anything.
- LED control. The board LED can be off, dim or bright. On the handheld it blinks to confirm focus, a sent shot, a stop, and which mode you picked.
- Tally LED. An optional LED on the camera unit lights while the shutter is down and briefly on each accepted shot, so your subjects can see the moment. You choose the pin in the setup menu.
- Shared GPS time. The camera unit passes its GPS clock to the handheld over the radio link, so the handheld needs no GPS of its own.
- Fairer airtime after a break. Once the real time is known from GPS, airtime used before a power cut is aged by the real gap, so you get your full allowance back as soon as it is due.
- Faster return to the fast setting. When the link is on a slow, long range setting and the signal is strong, it checks more often and steps back to fast in seconds.

## 0.1 (November 2026)

The proof of concept, and the week it stopped being an idea.

- It presses the shutter. One board talking Bluetooth LE straight to the Fujifilm X-E5, using the camera's own remote release. No phone, no app.
- Then from much further away. A second board joined as a relay: handheld to camera unit over LoRa, camera unit to camera over Bluetooth.
- Everything worked on the bench. Half-press holds focus. Single shot, burst, bulb, timer and interval all did what they should.
- Video works too. With the camera in movie mode, a press starts recording and another press stops it.
- First walk test. About 90 metres with no line of sight, the camera unit shut inside a parked car, small stock antennas, and still firing at the far point of the walk.
- The camera acknowledges each press, so the remote shows `PRESSED` the moment the camera has accepted it.
