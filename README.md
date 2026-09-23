# Entropy32 Plus

![Assembled PCB](images/entropy32.png)

**Entropy32** (aka *The Universe Bifurcator*) is a dedicated Bitcoin seed phrase generator that uses radioactive decay as its entropy source. It's completely open source and designed to be simple, auditable, and buildable by anyone with readily available components.

## Why radioactive decay?

Pseudo-random number generators are deterministic — given the same seed, they always produce the same output. True randomness for something as consequential as a Bitcoin wallet's seed phrase should come from a physical process nobody can predict or manipulate. Radioactive decay is quantum-mechanically random: the timing of individual decay events is fundamentally unpredictable, making it an ideal entropy source for generating cryptographic secrets.

## How it works

1. A Geiger counter detects decay events from a radioactive source (or background) and outputs a pulse via a 3.5mm audio jack.
![GMC-320S Geiger Pulse](images/raw0.png)
2. An LM393 comparator IC then takes the 0-1.5V pulse and compares it to a bias of ~0.5V via a voltage divider such that if V>0.5V we get HIGH else LOW.
![GMC-320S Geiger Pulse](images/lm3930.png)
3. The timing between events is captured and compared in non-overlapping pairs (each inter-arrival time feeds exactly one comparison, so adjacent output bits never share an input interval). Each resulting bit passes two continuous NIST SP 800-90B health tests (Repetition Count Test and Adaptive Proportion Test) before it's fed through SHA-256 conditioning to whiten the raw entropy and remove any bias — if either test trips, the device halts rather than generating a seed from a degraded source.
4. The conditioned entropy is mapped to words from the standard BIP39 English wordlist.
5. A simple button-driven, state-machine UI walks you through generating and displaying your seed phrase — entirely offline, with no wireless connectivity, no persistent storage of the seed, and no software dependencies beyond the device itself.

## How long does it take to generate a seed?

The firmware always fills a 512-bit raw pool before conditioning — that's true whether you pick a 12-word or 24-word phrase, since the seed length only changes how many of the whitened bits get used afterward, not how much raw entropy gets collected. Because of the non-overlapping interval pairing described above, each output bit costs two Geiger pulses, so at a steady count rate the pool fills in roughly:

```
minutes ≈ 1024 / CPM
```

The device's collection screen shows live CPM and a running ETA using this same math, so you don't need to do it by hand. Actual CPM depends heavily on your specific tube, source activity, distance, and shielding — treat the table below as a rough guide, not a spec, and follow appropriate safety/legal practices for any check source you use:

| Source | Typical CPM | Time to fill the pool |
|---|---|---|
| Ambient background | ~15–25 CPM | ~40–70 min |
| Uranium glass ("vaseline glass") | ~50–100 CPM | ~10–20 min |
| Thoriated tungsten welding rod (2% ThO₂) | ~100–200 CPM | ~5–10 min |
| Am-241 foil (smoke detector ionization chamber) | ~300–800 CPM | ~1.3–3.5 min |
| Low-activity calibration/check source | ~1,000–3,000 CPM | ~20 sec–1 min |
| High-activity check source (close contact) | ~5,000–10,000+ CPM | ~6–12 sec |

## Hardware

- Custom PCB built around an [ATmega328P microcontroller](https://ww1.microchip.com/downloads/en/DeviceDoc/Atmel-7810-Automotive-Microcontrollers-ATmega328P_Datasheet.pdf), designed in [KiCad](https://www.kicad.org/)
- Geiger counter module as the entropy source (currently designed to take the 3.5mm output of the [GQ Electronics GMC-320S](https://www.gqelectronicsllc.com/comersus/store/comersus_viewItem.asp?idProduct=5787))
- [LM393 comparator IC](https://www.ti.com/product/LM393)
- Push-button interface for on-device operation
- Serveral common passives i.e. resistors, capacitors, and LEDs
- 16MHZ crystal oscillator
- 0.91" OLED display with I2C adaptor
- PJ320E 3.5mm audio jack
- USB-C receptacle 6 pin
- header pins

<p align="center">
  <img src="images/atmega328p.png" alt="ATmega328P" height="200">
  <img src="images/GMC-320S_350.png" alt="GMC-320S Geiger counter" height="200">
</p>

## 3D printed case

I have included a custom designed basic case which you can 3d print to protect your device. The design works by allowing the board to slide on tracks into the case, once inserted all the way, a small prong prevents the board from sliding back out. If you plan to use this case be sure to hold the board down while unplugging the USB and/or the 3.5mm audio cable as this would otherwise put excessive strain on the prong to the point it may break off and fail. 

![Basic Case](images/basic-case.png)

## Repository contents

| File | Purpose |
|---|---|
| `entropy32_plus.ino` | Main firmware — state machine, entropy capture, and UI logic |
| `sha256.h` / `sha256.cpp` | SHA-256 implementation used to condition raw entropy |
| `bip39_wordlist.h` | BIP39 English wordlist, compiled into firmware |
| `button.h` | Button input handling |
| `logo_bitmap.h` | Boot splash bitmap, compiled into firmware |
| `tools/english.txt` | Source BIP39 wordlist |
| `tools/generate_wordlist.py` | Script to regenerate `bip39_wordlist.h` from `english.txt` |
| `tools/generate_logo_bitmap.py` | Script to regenerate `logo_bitmap.h` from a rendered image |
| `KiCad/...` | Schematics, PCB and other files |
| `KiCad/production/...` | Fabrication data: bom, zipped fab files, and positions etc |
| `enclosures/...` | 3D print files for cases & enclosures |

## Building it yourself

![Entropy32 schematic](images/schematic.svg)

The Arduino sketch (`entropy32_plus.ino`) and its accompanying `.h`/`.cpp` files must remain in the same top-level folder for the Arduino IDE to compile correctly — it doesn't recurse into subfolders for sketch code. Open `entropy32_plus.ino` in the Arduino IDE, verify your board settings for the ATmega328P, and flash as normal.

Use the KiCad files to tailor the board to your liking before fabrication or use the premade production files to order your board and/or pick and place from your preferred fabrication plant i.e. JLCPCB or PCB Way etc.

### Programming a bare ATmega328P

A freshly fabricated board has a blank ATmega328P — there's no bootloader on it and no USB-to-serial chip to flash it through, so the usual "select a port and hit upload" Arduino flow doesn't apply until it's been programmed once via the ICSP header exposed on the board.

1. **Wire up an ISP programmer.** Either a second Arduino running the `ArduinoISP` example sketch, or a dedicated programmer such as a USBasp, connected to the ICSP header. If using a USBasp, make sure it's set to 5V logic to match this board.
2. **Burn the bootloader.** In the Arduino IDE, select **Arduino Uno** or **Arduino Nano** as the board type (either is a stand-in for a bare ATmega328P at this stage), select your ISP programmer under Tools → Programmer, and run **Tools → Burn Bootloader**. This also sets the fuses for running off the board's 16MHz external crystal rather than the internal oscillator.
3. **Flash the firmware.** With the bootloader in place and the board running at 16MHz external clock, open `entropy32_plus.ino` and upload it via **Sketch → Upload Using Programmer** (still through the same ISP connection — the board has no onboard USB-serial chip for a normal serial upload).

## Flash footprint

Geiger pulse capture, the SP 800-90B runtime health tests, SHA-256 conditioning, the full BIP39 wordlist, the OLED driver, and the entire menu/boot UI all fit on the ATmega328P's 32KB of flash — with 486 bytes to spare:

```
Sketch uses 30234 bytes (98%) of program storage space. Maximum is 30720 bytes.
Global variables use 1061 bytes (51%) of dynamic memory, leaving 987 bytes for local variables. Maximum is 2048 bytes.
```

Breakdown of where it all goes, pulled from the compiled `.elf` with `avr-size`/`avr-nm`:

| Symbol | Bytes | What it is |
|---|---|---|
| `BIP39_WORDLIST_BLOB` | 13,117 | The official 2048-word BIP39 English wordlist |
| `main` (loop + inlined UI code) | 3,062 | State machine, screen drawing, menu logic |
| `SHA256::transform` | 2,278 | The SHA-256 compression function |
| `vfprintf` | 948 | Pulled in by `snprintf`, used for CPM/ETA/progress formatting |
| `u8x8_font_5x7_r` | 764 | Built-in OLED font glyph table |
| U8x8 / TwoWire internals | ~1,100 | I2C + SSD1306 driver plumbing |
| `LOGO_BITMAP` | 384 | Boot splash bitmap |
| `K` (SHA-256 round constants) | 256 | Fixed SHA-256 round constants |

The wordlist alone is 43% of the chip. Adafruit_GFX + Adafruit_SSD1306 don't fit alongside it — see the note in `entropy32_plus.ino` — which is why the firmware talks to the display through U8x8's text-only mode instead.

## Validation

The firmware runs the two minimal continuous health tests NIST SP 800-90B requires of a noise source at runtime — a Repetition Count Test and an Adaptive Proportion Test (see `runHealthChecks()` in `entropy32_plus.ino`) — which will halt the device rather than generate a seed if the raw source looks stuck or degraded. Runtime health tests are not the same as validating the source's entropy.

### Pilot entropy assessment

A 10-hour capture was logged with [Entropy32 Recorder](https://github.com/captainchapster/Entropy32-Recorder). The recorder is a separate board that timestamps the same post-LM393 D2 rising edges the firmware reads. It captured 10,000 edges (~17 CPM average) with zero buffer overruns or transport drops. The intervals were put through the firmware's exact bit derivation from this repository at `311c859`: 200 µs minimum interval, non-overlapping pairs and ties discarded. That produced 4,999 comparison bits (2,543 zeros, 2,456 ones). Those bits were assessed with NIST's [`ea_non_iid`](https://github.com/usnistgov/SP800-90B_EntropyAssessment) (v1.1.8):

| Estimator | Min-entropy (bits per bit) |
|---|---|
| Most Common Value | 0.924 |
| Collision | **0.738** |
| Markov | 0.956 |
| t-Tuple | 0.876 |
| LRS | 0.888 |
| MultiMCW prediction | 0.970 |
| Lag prediction | 0.915 |
| MultiMMC prediction | 0.919 |
| LZ78Y prediction | 0.922 |
| Compression | not run (too few samples) |

The assessed min-entropy is the lowest of these: **H ≈ 0.738 bits per comparison bit**, set by the collision estimator. At that rate, the 512-bit raw pool holds about 378 bits of min-entropy before SHA-256 conditioning. That is above the 256 bits a 24-word phrase needs.

Treat this as a pilot, not a validation:

- **4,999 samples is far short of the 1,000,000 the NIST tool is designed for.** The estimators use 99% upper confidence bounds, and those bounds are wide at this size, so the results are pessimistic. For example, the observed 50.9% / 49.1% split is within about 1.2 standard deviations of a fair coin. A full-length capture is still needed.
- It covers one device, one Geiger tube and one environment, and the source and setup details were not recorded. It says nothing about other builds.
- It is an entropy assessment, not NIST certification or formal validation.

The raw edges, derived bits, full tool output and SHA-256 checksums are all published in the [evidence directory](https://github.com/captainchapster/Entropy32-Recorder/tree/1d47487bc94c2b7c9baa2feec54d4443b60ba87c/entropy32_sp80090b_2026-09-22a), so the result can be reproduced end to end.

Please verify the entropy source you intend to use; otherwise, understand that you are using the device at your own risk.

## Disclaimer

This is a hobbyist/educational project. If you use it to generate a real Bitcoin seed phrase, understand the risks: verify the entropy quality yourself, review the code, and never trust a seed-generating device (this one included) with significant funds without independent auditing.

## Licenses

- For firmware/software, see [`MIT LICENSE`](./LICENSE).
- For hardware, see [`CERN-OHL-S v2 LICENSE`](./LICENSE-HARDWARE).