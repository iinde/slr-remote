# FAQ

The questions a photographer will ask first, and then the ones for people who want to build one.

- [Basics](#basics)
- [Range and reliability](#range-and-reliability)
- [Cameras](#cameras)
- [Building it](#building-it)
- [Legal and safety](#legal-and-safety)
- [The project](#the-project)

## Basics

### What is it?

A camera remote unlike any other camera remote: one that works for miles. A pocket handheld with one button talks by long-range radio to a small unit that sits with the camera, and that unit presses the shutter over Bluetooth using the camera's own remote-release feature. Nothing plugs into the camera and there is no phone in the chain. I built it for my Fujifilm X-E5.

### What does "SLR" stand for?

Super Long Range. The full name is Super Long Range Remote. Not single-lens reflex this time.

### What can I do with it on a shoot?

Everything you would do with a cable release, from much further away. Half-press to hold focus, single shots, focus lock for the decisive moment, a self-timer for group shots and self-portraits, interval shooting for time-lapse and camera traps, video start and stop, and timed burst and bulb with the handheld talking straight to the camera. Leave the camera on a tripod by the water, in a hide or on the far side of a venue, and shoot from where you actually want to be. The [User guide](user-guide.md) walks through each mode.

### Why not just use the phone app?

Because Bluetooth runs out a few metres from the camera, and because a phone is a poor shutter button. You have to unlock it, find the app, wait for it to connect and then look at a screen instead of your subject. This is one real button you can press without looking, from a distance no phone remote can touch.

### Can it record video?

Yes. Put the camera in movie mode, pick VIDEO on the handheld, and a press starts recording. Press again and it stops. In a stills mode the same press takes a photo, so the camera's mode dial decides which you get.

### Why LoRa and not Wi-Fi?

LoRa is a slow, long-reaching radio system built for sending small messages a very long way on very little power. A shutter press is a few dozen bytes, so it is a perfect fit. Wi-Fi and Bluetooth are fast and short. LoRa is the opposite, and it gets through obstacles far better at the same power.

### Why two boards?

The camera listens for a remote over Bluetooth, and Bluetooth is the short link. So one unit stays near the camera, hears the long-range radio and turns it into a Bluetooth shutter press. The other goes in your pocket. If you only need Bluetooth range, a single board in Direct mode works by itself.

### What do FAST and SLOW mean on the screen?

They show which gear the radio is in. LoRa has a setting called the spreading factor, from 7 to 12: low numbers are quick, high numbers are slower and reach further. `FAST` is 7 to 9 and `SLOW` is 10 to 12. The remote measures the link and changes gear by itself, so `SLOW` just means it has shifted into its long-range setting to keep you connected.

## Range and reliability

### How far does it reach?

It is built to work for miles with a clear view. LoRa is the radio technology used for links of several miles in open country, and this remote runs it at the legal power limit. My own test so far was a deliberately awkward one: about 90 metres with no line of sight, the camera unit shut inside a parked car, small stock antennas on both ends, and it was still firing at the far point of the walk. I ran out of walk before it ran out of range. If you take yours somewhere open and beat that, I want to hear about it. More in [Range](range.md).

### Is there lag?

Less than you would think. On the bench at the fast setting, a press took about 180 to 225 ms to get from the handheld to the camera unit accepting it, and each Bluetooth command to the camera takes about 140 ms. A plain tap then gives the lens half a second to focus before it shoots, which you can change. For timing that matters, hold to focus first and let go to shoot, or use AF LOCK so the shutter goes the instant you tap. On the slowest long-range setting each radio message spends over a second on air, which suits landscapes better than sprint finishes.

### Does it know the photo was actually taken?

It knows the camera accepted the press, and it tells you so with the word `PRESSED`. The X-E5 does not send anything back about the exposure itself, so the remote does not claim more than it has been told. In practice: take one test frame, check it on the camera, then go and shoot.

### What if the handheld dies in the middle of a held press or a long exposure?

The camera unit lets go by itself. During a hold it expects to keep hearing from the handheld, and if the handheld goes quiet it releases the shutter. I tested this by pulling the power on the handheld mid-hold: on the fast setting the camera unit let go after about 10 seconds. You will not come back to a camera that has been firing for an hour.

### What happens when I walk out of range?

The handheld tells you: `NO REPLY`. Then both units shift to the longest-reaching setting and find each other again by themselves, and the screen shows `LINK FOUND` when they do. A press that could not be sent in time is dropped, so the camera never fires late and by surprise.

### How long does the battery last?

Each unit runs from one LP603449 LiPo cell. The handheld sleeps after 60 seconds idle and wakes the instant you press, and the camera unit stays on to listen. Charge both over USB-C before a day out, the camera unit first. Timed figures will go in [Range](range.md) once I have them.

### Can several presses pile up and fire the camera twice?

No. Every press has its own identity, and the camera unit remembers the ones it has already run. If the handheld has to retry because a reply went missing, the camera unit recognises the retry and answers it without pressing the shutter again.

## Cameras

### Does it work with my camera?

It was built for and tested on the Fujifilm X-E5. The Bluetooth side builds on the open-source Furble project, and the firmware looks for any Fujifilm camera offering Bluetooth remote release, so other recent Fujifilm bodies are well worth a try. I have only had an X-E5 on my bench, so if you get it going on something else, tell me and I will add it to the list.

### Do I need to modify the camera or plug anything into it?

No. Nothing is opened, soldered or plugged in, and the hot shoe and ports stay free. The camera unit pairs through the camera's own Bluetooth pairing menu, the same way a phone would.

### Does autofocus work?

Yes. A hold on the button is a half-press: the camera focuses and holds, and letting go takes the shot. A quick tap does focus then shutter in one go. There is also an AF LOCK mode that holds focus for 10, 30 or 60 seconds while you wait for the moment.

### Where does the camera unit go?

Anywhere within Bluetooth reach of the camera. It has a flat back, so it sits happily in a tripod pouch, a bag pocket or on a strip of hook-and-loop tape on a tripod leg. Give its antenna a clear view in your direction and it will do the rest.

## Building it

### What do I need to build one?

Two Heltec WiFi LoRa 32 V4 boards, two LP603449 LiPo cells, two 868 MHz antennas on SMA connectors, eight M2 x 16 mm screws and access to a 3D printer. A GPS module for geotagging and a tally LED are optional extras for the camera unit. The [Shopping list](shopping-list.md) has search terms for every part, and the [Build guide](build-guide.md) covers assembly.

### Do I need to be able to code?

No. You flash the same firmware to both boards with one PlatformIO command, then choose each board's role and pair them from a menu on the screen. [Flashing and pairing](flashing-and-pairing.md) has every step.

### Do I need to solder?

A little, most likely. The boards, screens and radios come ready made. The battery lead may need a different plug to suit the board, as mine did, and a tally LED is two wires to the board's pins.

### Does the case need supports? What material?

No supports at all. I reworked every part so it prints clean: bottoms flat on the bed, tops face down, and no overhang that leans more than 45 degrees from vertical. I slice for PETG at 0.20 mm layers on a 0.4 mm nozzle, and the slicer puts a full set at about 1 hour 45 minutes.

### Can I use different LoRa boards?

The firmware and the case are both made for the Heltec V4. The core logic is kept separate from the hardware code, so a port to another ESP32 and SX1262 board is a realistic project if you fancy it.

## Legal and safety

### Is it legal to use?

In the UK and EU, the firmware is written to keep inside the rules for the band it uses: 869.525 MHz, no more than 500 mW radiated, and on air for no more than 10% of any hour (that last rule is called the duty cycle). It counts its own airtime, remembers the count across restarts and sets its power from the antenna gain you give it. It is a home-built project and not a certified product, so the responsibility for using it properly is yours. [Safety and legal](safety-and-legal.md) has the detail.

### Can I use it outside Europe?

Not as it stands. It transmits on 869.525 MHz, which is a UK and EU allocation, and other regions use different frequencies and rules. I have built and tested the EU 868 MHz version only.

### Can someone else trigger my camera?

No. When you pair the two units they agree a secret key, and you confirm it by checking the same 6-digit code on both screens. From then on every message is signed with that key, anything unsigned is ignored, and a recording of an old press cannot be played back later to fire the camera. The messages are signed and not scrambled, so someone with the right radio could tell that you pressed, but they cannot press for you.

### Can it damage my camera?

It talks to the camera through Fujifilm's own Bluetooth remote-release feature and asks for nothing a phone could not ask for. The one thing I designed hard against is a stuck shutter, which is why the camera unit lets go by itself if the handheld goes quiet. It is an unofficial accessory with no connection to Fujifilm.

### Is there anything I should be careful of when building it?

One thing above all: screw the antenna on before you power a board. A radio transmitting into nothing can damage its own amplifier. The firmware helps by waiting two seconds after power-on and showing `ANTENNA ON?`, and you should treat the LiPo cells with the usual care.

### Can I sell it?

No. The case is adapted from a design by Chem_Kid that is shared for non-commercial use, and my case files carry the same terms ([LICENSE-CASE.md](../LICENSE-CASE.md)). The firmware is MIT licensed ([LICENSE](../LICENSE)), so do what you like with the code. Build one for yourself, build one for a friend, and please do not turn it into a product.

## The project

### Who made it, and how long did it take?

I did: Aaron Phull (GitHub: iinde). It went from one board firing the shutter on my desk to a cased, reviewed pair of units between 7 and 13 November 2026. The full account is in the [Design story](design-story.md).

### Is this Meshtastic?

No. The boards are popular for Meshtastic and the case began life as a Meshtastic case, but the firmware is my own and the two units talk only to each other.

### How well tested is the firmware?

Firmware 0.3.0 came out of a line-by-line review of roughly 13,000 lines. It found 39 issues, none of them in the security code or the legal airtime limit, and all are fixed apart from three minor ones I left open on purpose. There are 147 automated unit tests that run on a PC, plus the bench sessions and the walk test with the real camera.

### What is it built on?

The Bluetooth camera protocol builds on the open-source [Furble](https://github.com/gkoh/furble) project (MIT licence). The firmware uses RadioLib, NimBLE-Arduino, U8g2 and TinyGPSPlus on the Arduino framework for ESP32, built with PlatformIO. The case is adapted from the "Meshtastic Heltec V4 Miniworks case" by Chem_Kid (Thingiverse thing 7367243). Full credits are in [CREDITS.md](../CREDITS.md).

### What is next?

A proper open-country range day, timing the batteries, and trying other Fujifilm bodies. If you get there before me, send me your results.

### Something is not working. Where do I look?

[Troubleshooting](troubleshooting.md) is organised by symptom. If the screen shows a word you do not recognise, the [User guide](user-guide.md) lists every one.
