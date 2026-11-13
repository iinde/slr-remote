# Safety and legal

SLR Remote is a remote that works for miles, so it is worth knowing how it shares the airwaves, how to look after its batteries, and how to use a far-away camera considerately. The firmware does most of the radio housekeeping for you. This page explains what it does and the few things that are yours to look after.

I built this for myself as a hobby project and I am sharing it so other photographers can build one too. Nothing here is legal advice.

## Radio rules

### How it is set up

| Setting | Value |
|---|---|
| Frequency | 869.525 MHz |
| Band | UK/EU 869.40 to 869.65 MHz |
| Power limit in that band | 500 mW ERP (27 dBm) |
| Airtime limit in that band | 10% duty cycle |
| Bandwidth | 125 kHz |

Two terms worth knowing. ERP (effective radiated power) is what the antenna actually radiates in its strongest direction, once the antenna's gain is counted. That is the figure the rule limits. Duty cycle is the share of time a transmitter may be on air: 10% means up to 6 minutes of transmitting in any hour.

This slice of the 868 MHz band is the one that allows the most power, which is exactly why I chose it for a long-range remote.

### What the firmware looks after

**Power.** The firmware works out the highest radio setting that keeps ERP at or under 27 dBm for the antenna gain you have set, starts there, and will not go above it. With the three menu options that comes out at about 24.9 dBm for a 2 dBi antenna, 25.9 dBm for 3 dBi (the default) and 26.9 dBm for 5 dBi. Those figures are calculated from the board's amplifier gain as published by the Meshtastic project.

**Airtime.** Each board keeps a running total of its own transmit time over a rolling hour and stays inside 360 seconds (10%).

- Routine check-ins pause at 80% of the budget, so the last fifth is kept for your presses and releases.
- Every transmission that starts is counted, and airtime is always rounded up.
- The count carries on through sleep, restarts and power cuts, so swapping a battery does not hand the board a fresh hour.
- The handheld shows what is left as `AIR n%`. A press that would go over shows `NOT SENT: DUTY`.

On a normal shoot you will never get near it. A single shot on the fast setting uses a fraction of a second.

**Pairing.** The two boards pair at the radio's lowest power setting on purpose, to keep the exchange local. Pair them side by side on the table.

**Start-up.** Nothing is transmitted in the first 2 seconds after power-on, and the screen shows `ANTENNA ON?` as a reminder.

### Your part

- Set the `Antenna` item in the setup menu to match the antenna you fitted. The power limit is worked out from that figure. The console asks for `force` before it will lower the figure, because lowering it raises transmit power.
- Use an 868 MHz antenna and fit it before powering a board. Transmitting into nothing can damage the amplifier.
- Check the rules where you live. The settings follow the UK/EU allocation as I understand it, and radio rules differ by country.
- The firmware is set up for EU 868 MHz only. Other regions use other bands with other rules, so outside the UK and EU the frequency needs changing in the source, and getting that right for your country is your job.
- If you change the radio settings in the source, the compliance is yours too.

### A hobby build

SLR Remote is a home-built radio project made from off-the-shelf development boards. It has not been through conformity testing and carries no approval marks of its own. Build it for yourself and enjoy it.

The 868 MHz band is shared and licence-free. Plenty of other devices live there and nobody has priority, so use the remote for photographs and for nothing where a missed press would matter to anyone's safety.

## Your link is yours

When you pair the two boards they agree a secret key, and you confirm it by checking the same 6-digit code on both screens. From then on every message between them is signed with that key and carries a counter that only goes up. A board ignores anything signed by a different pair and anything recorded and played back later. Somebody else's SLR Remote cannot fire your camera.

The messages are signed, and they travel unencrypted. They contain housekeeping (battery, signal figures) and, whenever the camera unit's GPS is running and has a fix, its position. The GPS runs when `Geotag` is on, and also while the handheld has its GPS, GPS view or RANGE view in use, because the handheld asks for the position then. If you would rather not broadcast where the camera is, leave `Geotag` off and stay off the GPS and RANGE views, or leave the GPS module out.

## Battery safety

Each unit runs on a single-cell lithium polymer (LiPo) cell. They are light, they hold a lot of energy, and they reward a little care.

- Check polarity before you plug a cell in. Cells are sold with different plugs and wire orders, and mine had to be re-terminated to fit the board's socket. Check with a meter against the board's markings. A reversed cell can destroy both the board and the cell.
- Never let the two leads touch. If you are re-terminating, cut and strip one wire at a time.
- Keep the cell flat and unpinched. When you close the case, make sure no wire is trapped in the seam and nothing presses on the cell.
- Give it the bay to itself. Cells swell a little with age and the bay allows for that.
- Retire a cell that is puffy, dented, hot, or smells sweet or chemical. Take it to a battery recycling point.
- Charge where you can see it, on a surface that will not burn.
- Keep both units out of hot cars and off sunny dashboards. The camera unit is the one that gets left behind.
- Buy cells with a built-in protection circuit from a seller you trust.
- Flying? Check the airline's rules on lithium cells first.

The firmware helps here too. It shows battery level for both units on the handheld, will not start a long interval or bulb exposure below 15%, and stops any running mode at 5%.

## Shooting from a distance

A remote that works from miles away means you can be well out of sight of your own camera. That is the fun of it, and it comes with some manners.

- Photograph people the way you would if you were standing behind the camera. Privacy law varies by country and can be strict about cameras people cannot see an operator for.
- Get the permissions you would need in person: private land, events, protected sites.
- With wildlife, set up without disturbing nests or protected species, then use the range to stay well back. That is one of the best things this remote does.
- A camera left alone can be knocked over, walked off with or tripped on. Put it somewhere sensible and keep an eye on it where you can.

### The remote lets go by itself

If the handheld loses power or the link while you are holding a press (a half-press on focus, say, or a bulb exposure), the camera unit releases the camera's button by itself. I tested this on the fast link setting and it let go about 10 seconds after I pulled the handheld's power. On slower settings it waits longer, in step with the check-in interval.

The first thing either board sends on a new Bluetooth connection to the camera is always a release, and long holds over direct Bluetooth are capped at 30 minutes. The whole design is there so your camera is not left with its shutter held down.

## Trademarks and affiliation

- Fujifilm and X-E5 are trademarks of FUJIFILM Corporation.
- Heltec is a trademark of its owner.
- LoRa is a trademark of Semtech Corporation.
- Bluetooth is a trademark of Bluetooth SIG, Inc.
- Meshtastic, Bambu Lab, Quectel, Espressif and any other names used in this repository belong to their respective owners.

These names appear only to say what the project works with. SLR Remote is my own independent project. It is not made, endorsed, sponsored or supported by Fujifilm, Heltec, Semtech or anyone else named here.

The remote talks to the camera through the camera's own built-in Bluetooth remote-release feature. It changes nothing on the camera.

## No warranty

The firmware is released under the MIT licence ([LICENSE](../LICENSE)). The case design, documentation and images are released under CC BY-NC 4.0 ([LICENSE-CASE.md](../LICENSE-CASE.md)). Both are provided as they are, with no warranty of any kind.

Put plainly: I built this, I use it and I am proud of it, and I am sharing it freely. If you build and use one, you do so at your own risk. I accept no liability for damage to cameras, boards, batteries, property or people, for missed shots, or for any breach of radio or other rules where you live.

## More reading

- [range.md](range.md): what shapes range, and the link settings
- [build-guide.md](build-guide.md): assembly, including the battery
- [how-it-works.md](how-it-works.md): pairing, signing and the airtime budget in more detail
- [faq.md](faq.md)
