# rf_bench2

An ADF4350/ADF4351 fractional-N synthesizer controller for an RF test
bench, driven from an Arduino Oak over a bit-banged 3-wire serial
interface.

It sets the output frequency and RF power from a serial menu, reports
PLL lock, reads an RF power detector, and reports the reference it is using
against an external frequency counter.

## Contents

- [Hardware](#hardware)
- [Wiring](#wiring)
- [Build and upload](#build-and-upload)
- [Serial menu](#serial-menu)
- [How it works](#how-it-works)
  - [Frequency programming](#frequency-programming)
  - [Register programming](#register-programming)
  - [Verification without readback](#verification-without-readback)
  - [Reference frequency](#reference-frequency)
  - [MUXOUT](#muxout)
- [Hardware gotchas](#hardware-gotchas)
- [Troubleshooting](#troubleshooting)
- [License](#license)

## Hardware

- Arduino Oak (ESP8266, FQBN `esp8266:esp8266:oak`)
- ADF4350 or ADF4351 module
- TTL-to-USB adapter for the Oak UART
- Optional RF power detector into A0
- Optional frequency counter

## Wiring

The synthesizer's serial interface is bit-banged, not SPI. Three Oak
pins drive DATA, CLK and LE:

    ADF4350/4351 DATA -> Oak P7
    ADF4350/4351 CLK  -> Oak P9
    ADF4350/4351 LE   -> Oak P6
    ADF4350/4351 LD   -> Oak P5   (lock detect input)
    ADF4350/4351 PDR  -> 3.3 V
    RF detector       -> Oak A0

UART, 115200 8N1, for the serial menu:

    Oak TX/P4 -> USB adapter RX
    Oak RX/P3 -> USB adapter TX
    Oak GND   -> USB adapter GND

The synthesizer control signals are 3.3 V logic.

### Oak silkscreen labels are not GPIO numbers

This trips people up. The Oak variant header defines aliases `P0`..`P11`
that do **not** match ESP8266 GPIO numbers:

    label  GPIO   use in this project
    P0       2    free, boot strapping
    P1       5    free, drives the onboard LED
    P2       0    free, boot strapping
    P3       3    UART RX
    P4       1    UART TX
    P5       4    ADF lock detect
    P6      15    ADF LE
    P7      13    ADF DATA
    P8      12    free, boot strapping
    P9      14    ADF CLK
    P10     16    free, not strapping
    P11     17    A0

Only `P0`..`P11` exist as aliases. Referencing `P14` does not compile.
`ADF_MUXOUT_PIN` in `config.h` uses a raw GPIO number for this reason.

Avoid GPIO0, GPIO2 and GPIO12. They are boot strapping pins and must be
in a defined state at reset.

## Build and upload

Compiling is timestamp driven: `compile` and `upload` rebuild only when a
source file is newer than the last binary, so they do nothing when nothing
has changed. Every real rebuild bumps `FIRMWARE_VERSION_MINOR` in
`version.h`, which gives each binary a version of its own. The version is
stored with the reference record, and the record carries its own layout
marker, checked on load, so a record written in a different layout is
discarded rather than half-trusted. The marker is deliberately not the
firmware version: `make compile` bumps the version on every build, so
stamping records with it would discard a saved reference on the next
recompile.

```sh
make compile
make upload
make monitor
```

The Makefile defaults:

    FQBN ?= esp8266:esp8266:oak
    PORT ?= /dev/tty.usbserial-2
    BAUD ?= 115200
    UPLOAD_SPEED ?= 460800
    ESPTOOL ?= ~/.local/bin/esptool

### Holding the chip in its bootloader

`upload` does not drive EN or GPIO0 from the adapter. The auto-reset network
on this bench does not assert them, so esptool cannot strap the chip by
itself, and which adapter line reaches which pin makes no difference. The
chip is held in the ROM bootloader by hand instead:

1. Disconnect RTS and DTR from the FTDI.
2. Jumper P2 (GPIO0) to ground.
3. Power-cycle the Oak.
4. `make upload`
5. Remove the jumper and power-cycle again to run the sketch.

Flashing then runs esptool with `--before no-reset`, which attaches to the
bootloader that is already running, and `--after no-reset`, which leaves the
chip there rather than toggling RTS for a hard reset. `arduino-cli` compiles
but never flashes, because it has no way to pass `--before`.

Flashing needs esptool 4 or newer, which the Arduino ESP8266 core does not
provide, since it bundles esptool 3:

```sh
uv tool install esptool
```

If macOS exposes the adapter as a `cu` device:

```sh
make PORT=/dev/cu.usbserial-2 upload
```

If `arduino-cli` fails on a temporary file, point `TMPDIR` somewhere
writable. `/tmp/arduino-tmp` is not always usable on a locked-down
account:

```sh
mkdir -p "$TMPDIR/arduino-tmp" && chmod 700 "$TMPDIR/arduino-tmp"
TMPDIR="$TMPDIR/arduino-tmp" make compile
```

### Wi-Fi credentials

The sketch connects to a local network at boot, but credentials are not
in the repository. Copy the template and fill it in:

```sh
cp secrets.h.example secrets.h
```

`secrets.h` is gitignored. Without it the sketch compiles and runs with
placeholder credentials; the Wi-Fi connection simply fails and the
controller carries on over serial, which is all the menu needs.

## Serial menu

Commands are single characters, case-sensitive. The menu reprints after
every command, prefixed with the current output power, output frequency
and reference frequency.

| Key | Action |
|---|---|
| `s` | Select ADF4350 or ADF4351 |
| `f` | Set output frequency |
| `r` | Set reference frequency |
| `z` | Reset reference to nominal |
| `F` | Factory settings (erase EEPROM) |
| `p` | Set RF power |
| `e` | Enable RF output |
| `d` | Disable RF output |
| `a` | Read RF detector A/D |
| `l` | Read lock status |
| `w` | Measure reference via MUXOUT (needs MUXOUT on P8) |
| `u` | Select MUXOUT monitor source |
| `i` | Show current settings |
| `m` | Show menu |

`F` is matched as uppercase before `tolower()` is applied, so it does
not collide with the lowercase `f` that sets frequency. It requires
typing `FACTORY` to confirm; Enter alone cancels.

### Frequency ranges

| Part | Output range | Output dividers |
|---|---|---|
| ADF4350 | 137.5 to 4400 MHz | ÷1, ÷2, ÷4, ÷8, ÷16 |
| ADF4351 | 35 to 4400 MHz | ÷1 … ÷64 |

Both parts have a 2200 to 4400 MHz VCO. The ADF4351 reaches 35 MHz
because it offers ÷32 and ÷64 as well.

### RF power codes

| Code | Approximate level |
|---|---|
| 0 | −4 dBm |
| 1 | −1 dBm |
| 2 | +2 dBm |
| 3 | +5 dBm |

Actual output depends on the module, frequency, supply voltage, matching
network and load.

## How it works

The sketch is a flat set of files with global state rather than classes.
`rf_bench2.ino` holds setup, the loop, and the global settings.
`synth.cpp` owns everything to do with the synthesizer chip.
`menu.cpp` owns the serial interface, the EEPROM reference record, and settings.

The global settings are:

    double referenceMHz;         // in use, programmed as entered
    double nominalReferenceMHz;  // what z restores
    double outputMHz;
    uint8_t rfPower;
    bool rfOutputEnabled;
    SynthType synthType;
    uint8_t muxoutMode;

### Frequency programming

Every settings change funnels through `programSynthesizer()`, which
calls `calculateRegisters()` and then writes all six registers. There is
no partial update, so the chip never sees a half-configured frequency.

`calculateRegisters()` works backwards from the requested output:

**1. Pick an output divider.** The VCO only tunes 2200 to 4400 MHz, so
the divider is the smallest power of two that lifts the output into that
range. 432.090 MHz becomes ÷8 and a 3456.72 MHz VCO.

**2. Pick the PFD frequency.** The reference divider R divides REFIN.
R starts at 1 and only increases if the PFD would exceed the 32 MHz
maximum, so 10 MHz in gives 10 MHz at the phase detector.

**3. Choose a prescaler.** The 4/5 prescaler needs INT ≥ 23 and the 8/9
prescaler needs INT ≥ 75. Above a 3000 MHz VCO the code uses 8/9. If the
resulting N is too small, R is increased until INT clears the minimum.

**4. Split into INT, FRAC and MOD.**

    totalN = vcoMHz / pfdMHz
    INT    = floor(totalN)
    FRAC   = round((totalN - INT) * MOD)
    MOD    = 4095

FRAC and MOD are then reduced by their greatest common divisor, and MOD
is forced to 2 when FRAC is 0. Both are deliberate: reduction shrinks the
modulus, which widens the fractional-N spur spacing, and an integer
frequency with MOD=2 avoids the sigma-delta modulator entirely. Setting
MOD=2 also sets the integer-N lock detect bit in R2.

Worked example, 432.090 MHz from a 10 MHz reference:

    ÷8 output divider, VCO = 3456.72 MHz
    PFD = 10 MHz, R = 1
    8/9 prescaler (VCO > 3000 MHz)
    totalN = 345.672
    INT = 345, FRAC = 2752, MOD = 4095   (gcd of 2752 and 4095 is 1)
    output = 10 MHz * 345.672 / 8 = 432.090049 MHz

That +49 mHz is the quantization floor for MOD=4095 at this PFD. It is
not an error to fix; a smaller MOD would trade it for spurs.

### Register programming

Each ADF4350 register is 30 bits wide and **the register address
occupies the three least significant bits**. A word built without its
address is silently written to the wrong register, which is exactly the
kind of fault that produces a locked PLL at the wrong frequency.

`calculateRegisters()` therefore ends each register with its address:

    R0 = 0x00500002   INT=160, FRAC=2,              address 0
    R1 = 0x08008011   MOD=1365, PHASE=1, 8/9,       address 1
    R2 = 0x18004F42   R=1, PD polarity, CP, MUXOUT, address 2
    R3 = 0x00840003   band clk mode high, CSR,      address 3
    R4 = 0x00AFA43C   ÷4, band sel divider 250,     address 4
    R5 = 0x00420005   LD pin in digital mode,       address 5

(1000 MHz out, 24.9998 MHz reference.)

Two of those fields are not what a datasheet reading alone suggests, and
both were wrong here until they were compared against a working
implementation in `../4350/adf4350`:

  R2 DB6, charge pump polarity, must be **set**. Left at the datasheet
  default of 0 the pump will not pull the VCO onto its target and it parks
  above the top of its band instead.

  R3 DB23, band select clock mode, must be **set**, paired with a fixed
  R4 band select divider of 250. That gives a band select clock near
  100 kHz. Deriving the divider from the reference instead, as this code
  once did, gives a band select clock of tens of MHz that will not
  calibrate the VCO into its band.

R0 is also written a second time after the R5 to R0 sequence.

Field masks are declared as named constants in `synth.cpp` rather than
written as bare shifts, so the bit positions are visible in one place:

    R0  INT   DB30:DB15   mask 0x7FFF8000
    R0  FRAC  DB14:DB3    mask 0x00007FF8
    R1  MOD   DB14:DB3    mask 0x00007FF8
    R1  PHASE DB26:DB15   mask 0x07FF8000
    R1  PR1   DB27        8/9 prescaler select
    R2  R     DB23:DB14   mask 0x03FFC000
    R2  CP    DB12:DB9    mask 0x00001E00
    R2  MUXOUT DB28:DB26  mask 0x1C000000
    R4  RF divider       DB22:DB20
    R4  band sel divider  DB19:DB12
    R5  LD pin mode      DB23:DB22

FRAC and MOD are 12 bits at DB14:DB3, so the mask is `0x7FF8`. `0xFFF8`
would be one bit too wide and would fold bit 15 into the value.

`programSynthesizer()` writes the registers in the datasheet's order,
R5 first and R0 last. R2 bit 13 enables double buffering, so the R1 and
R4 changes are held until the final write to R0 latches them. The chip
never transitions to an intermediate frequency.

`writeADFRegister()` bit-bangs the transfer: LE low, 32 bits MSB first
with a rising CLK per bit, then LE pulsed high to latch.

### Verification without readback

The ADF4350 and ADF4351 are **write only**. The serial interface is a
single input shift register: data is clocked in on rising CLK and moved
to one of six latches on rising LE. There is no data output pin and no
read command, so no SPI sequence can return register contents, and no
amount of rewiring will change that.

Verification is therefore done by decoding the register image the code
just built, in `getProgrammedOutputFrequencyHz()`. It inverts the build:
pull INT and FRAC back out of R0, MOD out of R1, R out of R2, and the
divider out of R4, then recompute the frequency.

That function also checks each register's low three bits against its
index and warns on a mismatch, which catches a mis-built word before it
is ever sent.

`calculateRegisters()` prints this decoded value as
`Register decode predicts:`. Comparing it against a counter reading is
how the programming is verified. If the prediction matches the target and
the counter disagrees, the discrepancy is real and the reference or the
loop is at fault, not the register math.

### Reference frequency

The reference is a single number, entered with `r`, and it is programmed
exactly as entered. There is no frequency correction applied on top of it.

That is deliberate. Scaling the reference to absorb a measured output error
looks like a calibration, but the reference feeds straight into the PFD
rate in `calculateRegisters()`, so a correction feeds a wrong PFD back into
the loop and asks it for an N it cannot lock to. A wrong output is a fault
to diagnose, not an offset to compensate away: with a GPS-locked reference
there is nothing to correct, and an output that is off is the loop telling
you something. `z` restores the reference to nominal.

The record in EEPROM is a struct with a magic value, a layout marker, a
CRC-32 over the preceding bytes, and range checks on load:

    struct CalibrationData {
      uint32_t magic;                  // 0x41444643, 'ADFC'
      double referenceMHz;
      double nominalReferenceMHz;
      uint32_t format;                 // record layout marker
      uint32_t checksum;               // CRC-32
    };

Any failed check means the compiled-in defaults are kept, so a corrupted,
truncated, or stale record degrades to a known state rather than a wild
frequency.

### MUXOUT

MUXOUT is pin 30, a diagnostic tap selected by R2 bits DB28:DB26:

| Value | Output |
|---|---|
| 0 | three-state, high impedance |
| 1 | DVDD, a hard logic high |
| 2 | DGND, a hard logic low |
| 3 | reference divider output, fREFIN / R |
| 4 | feedback divider output |
| 5 | analog lock detect, a DC voltage |
| 6 | digital lock detect, high when locked |

`u` selects the mode, reprograms, and prints the frequency MUXOUT should
be showing.

**Mode 3 is the useful one for bench work.** It is exactly fREFIN / R and
is completely independent of the N divider, so it separates a bad
reference from bad N programming in a single reading. If MUXOUT reads
10.000000 MHz and the RF output does not, the reference path is clean and
the fault is in the N programming.

Mode 4 is the feedback signal the phase detector compares against the
reference. It runs at the PFD rate while locked, so fREFIN / R is the
expected reading. Under fractional-N the true rate is
fPFD × (1 + FRAC / (MOD × INT)), a few hundred ppm above the PFD rate. A
scope cannot resolve that; a counter can.

Note that mode 1 drives MUXOUT to a hard logic high rather than to a
lock signal. Earlier revisions of this code set that mode while labelling
it digital lock detect, which would have put a solid high on the pin
regardless of lock state.

Wiring MUXOUT to an Oak pin is optional: the sketch only reads it for the
`w` reference self-check. That check uses `MUXOUT_MEASURE_PIN`, which is
**GPIO12, silkscreen P8**, and not `ADF_MUXOUT_PIN` (GPIO16, silkscreen
P10). GPIO16 cannot be used because the ESP8266 core's `attachInterrupt()`
is guarded by `if (pin < 16)`, so an interrupt on GPIO16 is accepted and
then silently ignored and the edge count comes back zero. Probing MUXOUT
directly on the ADF4350 module header remains the cleanest measurement.

MUXOUT is tristated until R2 is written, so it floats during reset, and
it is an output, so nothing may back-drive it. Note that it is also a
3.3 V digital output, so feeding it to a frequency counter's 50 ohm RF
input will overload the counter and produce a confident but wrong reading.

## Hardware gotchas

**Many modules carry their own oscillator.** ADF4350 breakout boards
frequently include a 10 MHz or 25 MHz crystal and a solder jumper or
similar that must be moved before an external reference reaches the
chip. The software setting must match whatever is actually connected.

**MUXOUT is tristated until programmed.** Not an issue for measuring,
but it means the pin is not a valid reference level at power-up.

**Boot strapping.** Do not attach MUXOUT to GPIO0 or GPIO2. GPIO12 is
used for the reference self-check despite being a strapping pin, which is
safe only because it is read as an input after boot and MUXOUT is
tristated until the sketch programs R2.

## Troubleshooting

**Output frequency is a clean binary fraction but not the target.** A
frequency like 431.875 MHz when 432.090 was asked for means the PLL
locked correctly and the N value is wrong, not that the reference is off.
431875000 × 8 is exactly 3455000000, so N was 345.5 instead of 345.672.
Check the decoded prediction first; if it matches the target and the
counter does not, the register math is fine and the reference is the
suspect.

**Not locking.** Check `l` and the module's own lock LED, which is
independent of this firmware and of the LD wiring, then the module's
reference jumper. The most informative single test is to change the charge
pump setting and see whether the output frequency moves: a locked loop's
output is fixed by N x fPFD / divider regardless of loop gain, so an
output that responds to a charge pump change is not locked. If the
register prediction looks right, set MUXOUT to mode 3 and compare
fREFIN / R against the reference.

**Locking, but the output sits above the target by a constant ratio.**
Check that the reference in use is the reference actually connected, via
`i`. Asking for a 4000 MHz VCO against a 25 MHz reference while the
firmware assumes 10 MHz drives the VCO past the top of its band, where it
sits with the loop open. This looks like a proportional tracking error
and is not one; an unlocked VCO does not park in proportion to the
request.

**The counter reads high or drifts, especially below 1 GHz.** The counter
input overloads at the default power code 3 in that range. Set RF power to
code 0 with `p` before measuring. The same applies to probing MUXOUT: a
3.3 V digital output into a 50 ohm RF input will overload it.

**Reading the reference self-check resets the Oak.** `w` deliberately
aborts when the tap is faster than 50 kHz rather than counting it into a
watchdog reset, but if the reference is far from what is configured the
tap can still be uncountable. Confirm the reference with `i` and a scope
before trusting `w`.

**`l` reports locked but the counter is empty.** Check the RF output is
enabled with `e` and that RF power is not at code 0.

**Nothing over serial.** The Oak UART needs a ground connection between
the Oak and the USB adapter, and the adapter needs TX and RX crossed.

## License

GPL-3.0. See [LICENSE](LICENSE).

The ADF4350 and ADF4351 are Analog Devices parts; this project contains
no vendor code.