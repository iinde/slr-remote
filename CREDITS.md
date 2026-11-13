# Credits

SLR Remote (Super Long Range Remote) was designed and built by **Aaron Phull** (GitHub: iinde) between 7 and 13 November 2026: the idea, the firmware, the reworked case and this documentation.

It stands on a lot of other people's work. This page says whose, and what each piece does.

## The case

The printed case is adapted from the **"Meshtastic Heltec V4 Miniworks case"** by **Chem_Kid**, Thingiverse thing 7367243, released under Creative Commons Attribution-NonCommercial.

Chem_Kid's design supplied the starting geometry: the fit around the Heltec V4 board, the screen opening, the button positions, the antenna connector mount and the GPS bay. I reworked the exterior and the split so every part prints without supports, and made a short handheld and a longer camera unit from it. The full list of changes is in [case/README.md](case/README.md).

The original files are kept unedited in [case/vendor/miniworks/](case/vendor/miniworks/). Licence terms and the credit wording to use are in [LICENSE-CASE.md](LICENSE-CASE.md).

## The camera's Bluetooth protocol

The code that talks to the camera builds on **Furble** by **Guo-Rong Koh** (github.com/gkoh/furble), released under the MIT licence.

Furble is an open-source Bluetooth remote for Fujifilm and other cameras. Its work on how Fujifilm cameras pair and accept remote-release commands over Bluetooth LE is the groundwork for `firmware/src/fuji.cpp`, which follows Furble's pairing and connection steps closely. It saved me a great deal of protocol detective work, and I am grateful for it.

The parts of `firmware/src/fuji.cpp` and `firmware/src/fuji.h` derived from Furble are used under Furble's MIT licence, the same terms as the text in [LICENSE](LICENSE), and carry this notice:

> Copyright (c) 2020 Guo-Rong Koh

## Firmware libraries

| Library | Author | Role here |
|---|---|---|
| RadioLib | Jan Gromeš | Drives the SX1262 LoRa radio: settings, transmit and receive |
| NimBLE-Arduino | h2zero | The Bluetooth LE stack used to connect to the camera |
| U8g2 | olikraus | Draws everything on the OLED screen |
| TinyGPSPlus | Mikal Hart | Reads position and time from the GPS module |

The exact versions used and tested are pinned in [firmware/platformio.ini](firmware/platformio.ini). Each library is under its own licence.

Cryptography (the key exchange for pairing the boards and the signature on each radio message) uses mbedTLS as shipped with the ESP32 platform.

## Firmware tools

| Tool | Role here |
|---|---|
| PlatformIO | Builds the firmware, flashes the boards and runs the tests |
| Arduino framework for ESP32 (Arduino-ESP32, by Espressif) | The base the firmware is written on |
| Unity | The test framework for the 147 unit tests |

## Case tools

| Tool | Role here |
|---|---|
| Python | The language the case is written in |
| manifold3d | Solid modelling: the cuts, joins and offsets that shape each part |
| trimesh | Loads the original design, checks meshes, writes the STL and 3MF files |
| NumPy | The arithmetic underneath |
| pyvista | Renders the pictures of the case |
| Bambu Studio | Slices the print plates and confirms they print without supports |

## Hardware

| Part | Maker | Role here |
|---|---|---|
| WiFi LoRa 32 V4 board | Heltec | The board in both units |
| ESP32-S3 | Espressif | The processor and Bluetooth radio on that board |
| SX1262 | Semtech | The LoRa radio chip on that board |
| L76K GPS module | Quectel | Optional time and position in the camera unit |
| X-E5 camera | Fujifilm | The camera I built and tested the remote on |

## Other acknowledgements

- The **Meshtastic** project's published measurement of this board's radio amplifier gain is the figure the firmware uses to stay inside the legal power limit, and its battery voltage correction is used for the battery reading.
- **Semtech's** SX1262 datasheet supplies the time-on-air formula behind the airtime budget.

## Not affiliated

SLR Remote is an independent personal project. It is not affiliated with, endorsed by or supported by Fujifilm, Heltec, Semtech, Espressif, Quectel, Bambu Lab, the Meshtastic project, the Furble project or Chem_Kid. All trademarks belong to their owners. See [docs/safety-and-legal.md](docs/safety-and-legal.md).

## Licences

- Firmware (everything in `firmware/`): MIT, [LICENSE](LICENSE)
- Case design, print files, documentation and images: CC BY-NC 4.0, [LICENSE-CASE.md](LICENSE-CASE.md)
