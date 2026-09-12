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
3. The timing between events is captured and fed through SHA-256 conditioning to whiten the raw entropy and remove any bias.
4. The conditioned entropy is mapped to words from the standard BIP39 English wordlist.
5. A simple button-driven, state-machine UI walks you through generating and displaying your seed phrase — entirely offline, with no wireless connectivity, no persistent storage of the seed, and no software dependencies beyond the device itself.

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
| `entropy32.ino` | Main firmware — state machine, entropy capture, and UI logic |
| `sha256.h` / `sha256.cpp` | SHA-256 implementation used to condition raw entropy |
| `bip39_wordlist.h` | BIP39 English wordlist, compiled into firmware |
| `button.h` | Button input handling |
| `english.txt` | Source BIP39 wordlist |
| `generate_wordlist.py` | Script to regenerate `bip39_wordlist.h` from `english.txt` |
| `KiCad/...` | Schematics, PCB and other files |
| `KiCad/production/...` | Fabrication data: bom, zipped fab files, and positions etc |
| `enclosures/...` | 3D print files for cases & enclosures |

## Building it yourself

![Entropy32 schematic](images/schematic.svg)

The Arduino sketch (`entropy32.ino`) and its accompanying `.h`/`.cpp` files must remain in the same top-level folder for the Arduino IDE to compile correctly — it doesn't recurse into subfolders for sketch code. Open `entropy32.ino` in the Arduino IDE, verify your board settings for the ATmega328P, and flash as normal.

Use the KiCad files to tailor the board to your liking before fabrication or use the premade production files to order your board and/or pick and place from your preferred fabrication plant i.e. JLCPCB or PCB Way etc.

## Validation

Entropy quality is yet to be validated against the [NIST SP 800-90B](https://csrc.nist.gov/publications/detail/sp/800-90b/final) methodology for entropy sources used in random bit generation. Please verify the entropy source you intend to use otherwise understand that you will be using the device at your own risk.

## Disclaimer

This is a hobbyist/educational project. If you use it to generate a real Bitcoin seed phrase, understand the risks: verify the entropy quality yourself, review the code, and never trust a seed-generating device (this one included) with significant funds without independent auditing.

## Licenses

- For firmware/software, see [`MIT LICENSE`](./LICENSE).
- For hardware, see [`CERN-OHL-S v2 LICENSE`](./LICENSE-HARDWARE).