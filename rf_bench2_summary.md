# RF Bench ADF4350/ADF4351 Controller

## Project layout

The sketch directory is:

    rf_bench2/

Files:

    rf_bench2.ino
    config.h
    synth.cpp
    menu.cpp
    Makefile

The main Arduino sketch name must match the directory name:
`rf_bench2.ino`.

All source filenames are lowercase. Therefore, source files must include:

    #include "config.h"

not `Config.h`.

## Hardware

Arduino Oak connections:

    ADF4350/ADF4351 DATA -> P7
    ADF4350/ADF4351 CLK  -> P9
    ADF4350/ADF4351 LE   -> P6
    ADF4350/ADF4351 LD   -> P5
    ADF4350/ADF4351 PDR  -> 3.3 V
    ADF4350/ADF4351 MUX  -> not connected
    RF detector output   -> A0 / P11

The Oak UART is connected to a TTL-to-USB adapter.

UART settings:

    Baud: 115200
    Data: 8 bits
    Parity: none
    Stop bits: 1

UART wiring:

    Oak TX/P4 -> USB adapter RX
    Oak RX/P3 -> USB adapter TX
    Oak GND   -> USB adapter GND

The RF synthesizer control signals use 3.3 V logic.

## Oak silkscreen labels are not GPIO numbers

The Oak variant `pins_arduino.h` defines aliases `P0`..`P11` that do
**not** match the ESP8266 GPIO numbers. The mapping in use:

    label  GPIO   note
    P0       2    free, boot strapping
    P1       5    free, drives the onboard LED
    P2       0    free, boot strapping
    P3       3    Oak UART RX, in use
    P4       1    Oak UART TX, in use
    P5       4    ADF lock detect, in use
    P6      15    ADF LE, in use
    P7      13    ADF DATA, in use
    P8      12    free, boot strapping
    P9      14    ADF CLK, in use
    P10     16    free, not strapping
    P11     17    A0 RF power detector, in use

Only `P0`..`P11` exist as aliases. Referencing `P14` or similar does
not compile. `ADF_MUXOUT_PIN` in `config.h` uses a raw GPIO number for
this reason.

Avoid GPIO0, GPIO2 and GPIO12. They are boot strapping pins and must
be in a defined state at reset.

## Board identifier

The correct board identifier is:

    esp8266:esp8266:oak

## Makefile

The Makefile uses:

    FQBN ?= esp8266:esp8266:oak
    PORT ?= /dev/tty.usbserial-3
    BAUD ?= 115200

Targets:

    make compile
    make upload
    make monitor
    make board-list
    make clean

If macOS exposes the adapter as a `cu` device, use:

    make PORT=/dev/cu.usbserial-3 upload

If `arduino-cli` fails with a ctags temporary-file error, point
`TMPDIR` at a writable directory instead of `/tmp/arduino-tmp`, which
is not always removable on a locked-down macOS account:

    mkdir -p "$TMPDIR/arduino-tmp"
    chmod 700 "$TMPDIR/arduino-tmp"
    TMPDIR="$TMPDIR/arduino-tmp" make compile

## Wi-Fi

The controller connects to:

    SSID and password: set in secrets.h, not committed
    Hostname: rfbench

No network server or other network functionality is required.

## Serial menu

The menu commands are:

    s - Select ADF4350 or ADF4351
    f - Set output frequency
    r - Set reference frequency
    c - Calibrate reference from counter
    z - Reset calibration to nominal
    F - Factory settings (erase EEPROM)
    p - Set RF power
    e - Enable RF output
    d - Disable RF output
    a - Read RF detector A/D
    l - Read lock status
    u - Select MUXOUT monitor source
    i - Show current settings
    m - Show this menu

The menu is displayed again after every command, and is prefixed with
the current output power, output frequency, and reference frequency.

`F` is matched as uppercase before `tolower()` is applied, so it does
not collide with the lowercase `f` used to set the output frequency. It
requires typing `FACTORY` to confirm; Enter on its own cancels.

## Factory settings

`F` restores these defaults:

    Synthesizer    : ADF4350, 137.5 to 4400 MHz
    Reference      : 10.000000 MHz nominal, correction factor 1.00000000
    Output         : 1000.000000 MHz
    RF power code  : 3 (~+5 dBm), RF output ON
    MUXOUT monitor : 6, digital lock detect

The calibration area of the EEPROM is overwritten with `0xFF` and
committed. On the next boot the magic check fails, so
`loadCalibrationFromEEPROM()` returns false and the defaults above are
used. There is no separate "written" flag.

The reference reverts to the 10 MHz default. Use `r` if the actual
reference differs.

Note that the reset does not change the module's solder jumpers. Many
ADF4350/ADF4351 modules carry their own 10 MHz or 25 MHz oscillator and
need a jumper moved before an external reference reaches the chip.

## Synthesizer frequency ranges

ADF4350:

    137.5 MHz to 4400 MHz

ADF4351:

    35 MHz to 4400 MHz

The menu displays the active device's frequency limits.

Both parts use a 2200 to 4400 MHz VCO. The ADF4350 offers divide by 1,
2, 4, 8, and 16; the ADF4351 also offers 32 and 64, which is how it
reaches down to 35 MHz. `calculateRegisters()` stops increasing the
output divider once it reaches the selected device's maximum divider
select value.

## Reference frequency

The reference is held as:

    const double DEFAULT_REFERENCE_MHZ = 10.0;

    extern double referenceMHz;        // in use, may be corrected
    extern double nominalReferenceMHz; // as entered by the user

The `r` command accepts MHz, in the range 1 to 100, for example:

    10

It sets both `referenceMHz` and `nominalReferenceMHz`, clears the
correction factor to 1.0, and saves to EEPROM.

`z` restores `referenceMHz` to `nominalReferenceMHz` and clears the
correction factor, without touching the nominal value. This is why `z`
restores 10 MHz rather than the compiled-in default: the nominal value
is what the user last entered with `r`.

## RF power settings

The four RF power codes are:

    0 - approximately -4 dBm
    1 - approximately -1 dBm
    2 - approximately +2 dBm
    3 - approximately +5 dBm

Actual output depends on the module, frequency, supply voltage, matching network, and load.

## Register programming

Each ADF4350/ADF4351 register is 30 bits wide, and **the register
address occupies the three least significant bits**. A word built
without its address is written to the wrong register.

`calculateRegisters()` builds all six registers with the address bits
in place, and `programSynthesizer()` writes them in the datasheet's
order, R5 first and R0 last, since the write to R0 latches the
double-buffered fields.

Field masks used in `synth.cpp`:

    R0  INT   DB30:DB15   mask 0x7FFF8000
    R0  FRAC  DB14:DB3    mask 0x00007FF8
    R1  MOD   DB14:DB3    mask 0x00007FF8
    R1  PHASE DB26:DB15   mask 0x07FF8000
    R1  PR1   DB27        prescaler select, 8/9
    R2  R     DB23:DB14   mask 0x00FFC000
    R2  MUXOUT DB28:DB26  mask 0x1C000000
    R4  RF divider       DB22:DB20
    R4  band sel divider  DB19:DB12
    R5  LD pin mode      DB23:DB22

FRAC and MOD are 12 bits at DB14:DB3, so the mask is `0x7FF8`, not
`0xFFF8`. Getting this wrong pulls in bit 15 and corrupts the value.

The N counter calculation:

    totalN = vcoMHz / pfdMHz
    INT    = floor(totalN)
    FRAC   = round((totalN - INT) * MOD)
    MOD    = 4095

FRAC and MOD are then reduced by their greatest common divisor, and
MOD is set to 2 when FRAC is 0, which gives clean integer-N operation
and sets the integer-N lock detect bit in R2.

Prescaler selection follows the N value: 4/5 above VCO 3000 MHz and
8/9 below it, with the R counter increased as needed to keep INT above
the prescaler minimum of 23 or 75.

## Reading registers back

The ADF4350 and ADF4351 are write only. The serial interface is a
single input shift register: data is clocked in on the rising edge of
CLK and transferred to one of six latches on the rising edge of LE.
There is no data output pin and no read command, so no SPI sequence can
return register contents, and no wiring change will enable it.

`getProgrammedOutputFrequencyHz()` therefore decodes the register
image the sketch built in RAM. It warns if any register's address bits
do not match its index, which catches a mis-built word before it is
sent.

`calculateRegisters()` prints the frequency decoded from the register
image after printing the registers themselves. That decoded value is
what the chip should produce, and comparing it against a counter
reading is how the programming is verified.

## MUXOUT

MUXOUT is pin 30 on the ADF4350/ADF4351. It is a diagnostic tap
selected by R2 bits DB28:DB26:

    value  output
    0      three-state (high impedance)
    1      DVDD, a hard logic high
    2      DGND, a hard logic low
    3      reference divider output, fREFIN / R
    4      feedback divider output
    5      analog lock detect, a DC voltage
    6      digital lock detect, high when locked

The `u` command selects the mode, reprograms, and prints the frequency
MUXOUT should be showing.

Mode 3 is the useful one for bench work. It is exactly fREFIN / R and
is independent of the N divider, so it separates a bad reference from
bad N programming in a single reading.

Mode 4 is the feedback path signal the phase detector compares against
the reference. It runs at the PFD rate while locked, so fREFIN / R is
the expected reading. Under fractional-N the true rate is
fPFD * (1 + FRAC / (MOD * INT)), a few hundred ppm above the PFD rate.
A scope cannot resolve that; a counter can.

The sketch never reads MUXOUT, so wiring it to an Oak pin is optional.
`ADF_MUXOUT_PIN` in `config.h` is GPIO16, silkscreen P10, and is
currently not connected. The cleanest measurement is to probe MUXOUT
directly on the ADF4350 module header and leave the Oak unconnected.
The Oak pin matters only if the run to the counter is long enough to
want a termination point away from the module.

MUXOUT is an output, so the Oak side must be input only, with no
pull-ups and nothing that can back-drive it. Note that MUXOUT is
tristated until R2 is written, so it floats during reset.

## Reference calibration and EEPROM storage

Menu option `c` calibrates the reference frequency against an external
frequency counter reading with 1 Hz accuracy:

1. The user enters the complete frequency counter reading in Hz (e.g.
   `1000000023` or with commas `1,000,000,023`) or in MHz (e.g.
   `1000.000023`). Entering `0` resets to the nominal reference.
2. The controller computes the exact correction factor from the
   synthesized hardware-programmed output frequency:
   `factor = measuredHz / programmedHz`. This prevents fractional-N
   quantization error from corrupting the reference calibration.
3. The reference frequency is updated: `referenceMHz *= factor`, and
   `refCorrectionFactor = referenceMHz / nominalReferenceMHz`.
4. The synthesizer is reprogrammed so that the actual output shifts to
   match the commanded target frequency.
5. The calibration data is saved to EEPROM.

Calibration aborts if the measured frequency deviates from the
programmed output by more than 20 percent, and if the recalculated
reference falls outside 1 to 100 MHz.

The EEPROM record is:

    struct CalibrationData {
      uint32_t magic;                  // 0x41444643, 'ADFC'
      double correctionFactor;
      double referenceMHz;
      double nominalReferenceMHz;
      uint32_t checksum;               // CRC-32 over the preceding bytes
    };

The record is validated on load: magic, then CRC-32, then range checks
on the factor (0.5 to 2.0) and both reference values (1 to 100 MHz).
Any failure means the defaults are kept.

The record grew a `nominalReferenceMHz` field so that `z` can restore
the reference the user entered rather than the compiled-in default.
Data written by firmware predating that field fails the checksum, which
is the intended behaviour: a calibration from an older build is
discarded rather than half-read.
