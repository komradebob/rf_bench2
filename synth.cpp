#include "config.h"

uint32_t adfRegisters[6];

/*
  ADF4350/ADF4351 register bit fields.  Every register word is 30 bits
  wide; the register address occupies the three least significant bits,
  so a word built without its address is written to the wrong register.
*/
static const uint32_t ADF_REG0_INT     = 0x7FFF8000UL;
static const uint32_t ADF_REG0_FRAC    = 0x00007FF8UL;
static const uint32_t ADF_REG1_MOD     = 0x00007FF8UL;
static const uint32_t ADF_REG1_PHASE   = 0x07FF8000UL;
static const uint32_t ADF_REG1_PR1_8_9 = 0x08000000UL;
static const uint32_t ADF_REG2_RCNT    = 0x03FFC000UL;
static const uint32_t ADF_REG2_DB      = 0x00002000UL;
static const uint32_t ADF_REG2_CP      = 0x00001E00UL;
static const uint32_t ADF_REG2_LDF     = 0x00000100UL;
static const uint32_t ADF_REG2_PD_POL  = 0x00000040UL;
static const uint32_t ADF_REG2_MUXOUT  = 0x1C000000UL;
static const uint32_t ADF_REG3_BANDCLK_HIGH = 0x00800000UL;
static const uint32_t ADF_REG3_CSR         = 0x00040000UL;
static const uint32_t ADF_REG4_BANDCLK = 0x000FF000UL;
static const uint32_t ADF_REG4_RFDIV   = 0x00700000UL;
static const uint32_t ADF_REG4_FB_FUND = 0x00800000UL;
static const uint32_t ADF_REG4_PWR     = 0x00000018UL;
static const uint32_t ADF_REG4_RF_EN   = 0x00000020UL;
static const uint32_t ADF_REG4_MUTE_TILL_LOCK = 0x00000400UL;
static const uint32_t ADF_REG5_LD_DIG  = 0x00400000UL;



static const double VCO_MIN_MHZ = 2200.0;
static const double VCO_MAX_MHZ = 4400.0;
static const uint32_t MAX_PFD_MHZ = 32.0;
static const uint16_t MAX_MODULUS = 4095;
static const uint16_t MAX_R_COUNTER = 1023;
static const uint16_t PHASE_VALUE = 1;
static const uint32_t CHARGE_PUMP_INDEX = 7;

static uint16_t gcd16(uint16_t a, uint16_t b) {
  while (b != 0) {
    uint16_t t = a % b;
    a = b;
    b = t;
  }

  return a;
}

void showSynthesizerInfo() {
  if (synthType == SYNTH_ADF4350) {
    printLine(F("Selected synthesizer: ADF4350"));
    printLine(F("Frequency range: 137.5 to 4400 MHz"));
  } else {
    printLine(F("Selected synthesizer: ADF4351"));
    printLine(F("Frequency range: 35 to 4400 MHz"));
  }

  printLine(F("RF power range: codes 0 to 3"));
  printLine(F("Nominal power: approximately -4, -1, +2, +5 dBm"));
}

void writeADFRegister(uint32_t value) {
  digitalWrite(ADF_LE_PIN, LOW);

  for (int8_t bit = 31; bit >= 0; bit--) {
    digitalWrite(ADF_CLK_PIN, LOW);

    digitalWrite(
      ADF_DATA_PIN,
      (value & (1UL << bit)) ? HIGH : LOW
    );

    delayMicroseconds(1);
    digitalWrite(ADF_CLK_PIN, HIGH);
    delayMicroseconds(1);
  }

  digitalWrite(ADF_CLK_PIN, LOW);
  delayMicroseconds(1);

  digitalWrite(ADF_LE_PIN, HIGH);
  delayMicroseconds(1);
  digitalWrite(ADF_LE_PIN, LOW);
  delayMicroseconds(1);
}

void printMuxoutModeName(uint8_t mode) {
  switch (mode) {
    case MUXOUT_THREE_STATE: printLine(F("Three-state (high impedance)")); break;
    case MUXOUT_DVDD:        printLine(F("DVDD (logic high)")); break;
    case MUXOUT_DGND:        printLine(F("DGND (logic low)")); break;
    case MUXOUT_R_DIV_OUT:   printLine(F("Reference divider output (fREFIN / R)")); break;
    case MUXOUT_N_DIV_OUT:   printLine(F("Feedback divider output (PFD rate)")); break;
    case MUXOUT_ANALOG_LD:   printLine(F("Analog lock detect (DC voltage)")); break;
    case MUXOUT_DIGITAL_LD:  printLine(F("Digital lock detect (logic high when locked)")); break;
    default:                 printLine(F("Reserved")); break;
  }
}

/*
  Expected frequency on MUXOUT in the two divider output modes.

  R divider output is exactly fREFIN / R, so it is a direct check on
  the reference path: if this reads correctly and the RF output does
  not, the fault is in the N divider programming, not the reference.

  N divider output is the feedback path signal the phase detector
  compares against the reference.  It runs at the PFD rate while
  locked, so fREFIN / R is the expected reading.  Under fractional-N
  the true rate is fPFD * (1 + FRAC / (MOD * INT)), a few hundred ppm
  above the PFD rate, which a scope cannot resolve but a counter can.
*/
double getMuxoutExpectedFrequencyHz() {
  if (muxoutMode != MUXOUT_R_DIV_OUT && muxoutMode != MUXOUT_N_DIV_OUT) {
    return 0.0;
  }

  uint32_t rCounter = (adfRegisters[2] & ADF_REG2_RCNT) >> 14;

  if (rCounter == 0) {
    rCounter = 1;
  }

  return (referenceMHz * 1000000.0) / (double)rCounter;
}

void printRegister(uint8_t number, uint32_t value) {
  Serial.print(F("R"));
  Serial.print(number);
  Serial.print(F(" = 0x"));

  char buffer[9];
  snprintf(buffer, sizeof(buffer), "%08lX", (unsigned long)value);
  Serial.println(buffer);
}

void calculateRegisters() {
  double minimumOutputMHz =
    synthType == SYNTH_ADF4350 ? 137.5 : 35.0;

  if (outputMHz < minimumOutputMHz || outputMHz > 4400.0) {
    Serial.print(F("Frequency outside selected device range: "));
    Serial.print(minimumOutputMHz, 1);
    Serial.println(F(" to 4400 MHz"));
    return;
  }

  uint8_t outputDividerSelect = 0;
  uint16_t outputDivider = 1;
  uint8_t maxDividerSelect =
    synthType == SYNTH_ADF4350 ? 4 : 6;

  while (outputMHz * outputDivider < VCO_MIN_MHZ &&
         outputDividerSelect < maxDividerSelect) {
    outputDivider *= 2;
    outputDividerSelect++;
  }

  double vcoMHz = outputMHz * outputDivider;

  if (vcoMHz > VCO_MAX_MHZ) {
    printLine(F("Error: VCO frequency exceeds 4400 MHz."));
    return;
  }

  uint16_t rCounter = 1;
  double pfdMHz = referenceMHz / rCounter;

  while (pfdMHz > MAX_PFD_MHZ && rCounter < MAX_R_COUNTER) {
    rCounter++;
    pfdMHz = referenceMHz / rCounter;
  }

  /*
    4/5 prescaler: INT range 23 to 65535, VCO up to 3000 MHz.
    8/9 prescaler: INT range 75 to 65535, used above 3000 MHz.
  */
  uint16_t prescaler = (vcoMHz > 3000.0) ? 8 : 4;
  uint16_t minimumInt = (prescaler == 8) ? 75 : 23;

  while (vcoMHz / pfdMHz < minimumInt && rCounter < MAX_R_COUNTER) {
    rCounter++;
    pfdMHz = referenceMHz / rCounter;
  }

  double totalN = vcoMHz / pfdMHz;

  if (totalN < minimumInt || totalN > 65535.0) {
    printLine(F("Error: N divider ratio out of range."));
    return;
  }

  uint16_t integerPart = (uint16_t)floor(totalN);
  uint16_t modulus = MAX_MODULUS;

  double fractionalPart = totalN - integerPart;
  uint32_t frac = (uint32_t)round(fractionalPart * modulus);

  if (frac >= modulus) {
    integerPart++;
    frac = 0;
  }

  if (frac == 0) {
    modulus = 2;
  } else {
    uint16_t divisor = gcd16((uint16_t)frac, modulus);

    if (divisor > 1) {
      frac /= divisor;
      modulus /= divisor;
    }
  }

  if (frac >= modulus) {
    integerPart++;
    frac = 0;
  }

  uint16_t phase = (modulus > PHASE_VALUE) ? PHASE_VALUE : 0;

  /*
    R0: INT, FRAC, control bits, address 0
  */
  adfRegisters[0] =
    (((uint32_t)integerPart << 15) & ADF_REG0_INT) |
    (((uint32_t)frac << 3) & ADF_REG0_FRAC) |
    0x00000000;

  /*
    R1: MOD, PHASE, prescaler, address 1
  */
  adfRegisters[1] =
    (((uint32_t)modulus << 3) & ADF_REG1_MOD) |
    (((uint32_t)phase << 15) & ADF_REG1_PHASE) |
    (prescaler == 8 ? ADF_REG1_PR1_8_9 : 0UL) |
    0x00000001;

  /*
    R2: 10-bit reference counter, charge pump, lock detect, address 2.

    Bit 6 sets the charge pump polarity and bit 13 would enable double
    buffering. Both follow the known-good bench implementation in
    ../4350/adf4350, which locks on this hardware where the datasheet
    defaults do not: it sets bit 6 and leaves double buffering off, so R0
    is written twice in programSynthesizer() rather than relying on it to
    latch R1 and R4. Setting bit 6 is what lets the loop pull the VCO onto
    its target; with it clear the VCO parks above the top of its band.
  */
  adfRegisters[2] =
    (((uint32_t)rCounter << 14) & ADF_REG2_RCNT) |
    ADF_REG2_PD_POL |
    ((CHARGE_PUMP_INDEX << 9) & ADF_REG2_CP) |
    (frac == 0 ? ADF_REG2_LDF : 0UL) |
    ((muxoutMode << 26) & ADF_REG2_MUXOUT) |
    0x00000002;

  /*
R3 is written as its address alone.

    The band select clock is taken from R4: the 8-bit divider in bits19:12
    with bit 23 clear, which is the low mode the ADF4350 uses. R3 also
    carries a legacy 12-bit clock divider whose mode field is bits 16:15.
    Writing the wrong value there stops the VCO calibrating into its band,
    so the default of all zeroes is used.
  */
  adfRegisters[3] = 0x00000003;

  /*
    R4: RF output enable, RF power, band select clock divider,
    output divider, feedback from VCO, address 4

    The band select divider is a fixed 250, paired with the R3 band select
    clock mode high bit, exactly as the known-good implementation has it.
    Choosing it from the reference instead, as this used to, produced a band
    select clock of tens of MHz that would not calibrate the VCO.
  */
  adfRegisters[4] = ADF_REG4_FB_FUND;
  adfRegisters[4] &= ~ADF_REG4_PWR;
  adfRegisters[4] |= ((uint32_t)(rfPower & 0x03) << 3) & ADF_REG4_PWR;

  adfRegisters[4] &= ~ADF_REG4_RF_EN;

  if (rfOutputEnabled) {
    adfRegisters[4] |= ADF_REG4_RF_EN;
  }

  adfRegisters[4] &= ~(ADF_REG4_BANDCLK | ADF_REG4_RFDIV);
  adfRegisters[4] |= (250UL << 12) & ADF_REG4_BANDCLK;
  adfRegisters[4] |= ADF_REG4_MUTE_TILL_LOCK;
  adfRegisters[4] |= ((uint32_t)outputDividerSelect << 20) & ADF_REG4_RFDIV;
  adfRegisters[4] |= 0x00000004;

  /*
    R5: lock detect pin mode = digital, address 5

    Bits 17:15 carry 4 in the known-good implementation. They are not
    documented as a control field on the ADF4350, but the bench locks with
    them set, so they are reproduced rather than assumed harmless.
  */
  adfRegisters[5] = ADF_REG5_LD_DIG | (4UL << 15) | 0x00000005;

  Serial.print(F("Output: "));
  Serial.print(outputMHz, 3);
  Serial.println(F(" MHz"));

  Serial.print(F("Reference: "));
  Serial.print(referenceMHz, 6);
  Serial.println(F(" MHz"));

  Serial.print(F("PFD: "));
  Serial.print(pfdMHz, 3);
  Serial.println(F(" MHz"));

  Serial.print(F("INT="));
  Serial.print(integerPart);
  Serial.print(F(" FRAC="));
  Serial.print(frac);
  Serial.print(F(" MOD="));
  Serial.println(modulus);

  for (int i = 5; i >= 0; i--) {
    printRegister(i, adfRegisters[i]);
  }

  double predictedHz = getProgrammedOutputFrequencyHz();

  Serial.print(F("Register decode predicts: "));
  Serial.println(predictedHz / 1000000.0, 6);
  Serial.println();
}

void programSynthesizer() {
  printLine(F("Calculating synthesizer registers..."));
  calculateRegisters();

  printLine(F("Writing registers..."));

  for (int i = 5; i >= 0; i--) {
    writeADFRegister(adfRegisters[i]);
    delay(2);
  }

  // R0 again, matching the known-good implementation. Double buffering is
  // off, so this is belt and braces rather than the latch itself.
  writeADFRegister(adfRegisters[0]);
  delay(2);

  printLine(F("Synthesizer programming complete."));
}

void reportLockStatus() {
  Serial.print(F("Lock detect: "));

  if (digitalRead(ADF_LD_PIN) == HIGH) {
    Serial.println(F("LOCKED"));
  } else {
    Serial.println(F("NOT LOCKED"));
  }
}

/*
  Reference self-check.

  MUXOUT in reference divider mode outputs fREFIN / R. At R = 1 that is the
  full 10 MHz, far past what the ESP8266 can count from a GPIO interrupt, so
  the divider is pushed to its maximum of 1023 to bring the tap down to about
  9.8 kHz. The reference divider output does not depend on the N counter, so
  the synth cannot lock at that divider setting and its tuning registers are
  untouched; only R2 is rewritten, and only R2 is put back afterwards.

  Two windows are counted. A dropped edge would bias a single window low, but
  it cannot bias two independent windows the same way, so a large disagreement
  between them means the count is unreliable rather than the reference being
  wrong.
*/
static const uint16_t MEASURE_R_COUNTER = 1023;
static const uint32_t MEASURE_WINDOW_MS = 500;
static const uint32_t MEASURE_WINDOWS = 2;

static volatile uint32_t muxoutEdgeCount = 0;
static volatile uint32_t muxoutLastStamp = 0;
static volatile uint32_t muxoutMinPeriod = 0xFFFFFFFF;

static void IRAM_ATTR countMuxoutEdge() {
  uint32_t now = micros();
  uint32_t previous = muxoutLastStamp;

  muxoutLastStamp = now;

  if (previous != 0) {
    uint32_t period = now - previous;

    if (period < muxoutMinPeriod) {
      muxoutMinPeriod = period;
    }
  }

  muxoutEdgeCount++;
}

/*
  Drive MUXOUT to a static level and read it back. DGND and DVDD are the two
  sources that hold a fixed voltage, so they test the wire and the input pin
  without needing a frequency to be counted. Returns true when both levels
  read back correctly, which means anything wrong after this point is in the
  counting rather than the connection.
*/
static bool verifyMuxoutWire() {
  uint8_t savedMode = muxoutMode;

  muxoutMode = MUXOUT_DGND;
  adfRegisters[2] = (adfRegisters[2] & ~ADF_REG2_MUXOUT) |
                    ((MUXOUT_DGND << 26) & ADF_REG2_MUXOUT);
  writeADFRegister(adfRegisters[2]);
  delay(5);
  bool lowReads = digitalRead(MUXOUT_MEASURE_PIN) == LOW;

  muxoutMode = MUXOUT_DVDD;
  adfRegisters[2] = (adfRegisters[2] & ~ADF_REG2_MUXOUT) |
                    ((MUXOUT_DVDD << 26) & ADF_REG2_MUXOUT);
  writeADFRegister(adfRegisters[2]);
  delay(5);
  bool highReads = digitalRead(MUXOUT_MEASURE_PIN) == HIGH;

  muxoutMode = savedMode;
  adfRegisters[2] = (adfRegisters[2] & ~ADF_REG2_MUXOUT) |
                    (((uint32_t)muxoutMode << 26) & ADF_REG2_MUXOUT);
  writeADFRegister(adfRegisters[2]);

  Serial.print(F("MUXOUT wired to P8: DGND reads "));
  Serial.print(lowReads ? F("LOW") : F("HIGH"));
  Serial.print(F(", DVDD reads "));
  Serial.print(highReads ? F("HIGH") : F("LOW"));
  Serial.println();

  return lowReads && highReads;
}

void measureReferenceFromMuxout() {
  Serial.print(F("MUXOUT must be connected to Oak P8 (GPIO"));
  Serial.print(MUXOUT_MEASURE_PIN);
  Serial.println(F(")."));

  pinMode(MUXOUT_MEASURE_PIN, INPUT);

  if (!verifyMuxoutWire()) {
    printLine(F("MUXOUT is not reaching P8, so the reference cannot be measured."));
    printLine(F("Check the wire from the ADF4350 MUXOUT pin to Oak P8."));
    return;
  }

  uint32_t savedR2 = adfRegisters[2];

  adfRegisters[2] =
    (((uint32_t)MEASURE_R_COUNTER << 14) & ADF_REG2_RCNT) |
    ADF_REG2_DB |
    ((CHARGE_PUMP_INDEX << 9) & ADF_REG2_CP) |
    ADF_REG2_PD_POL |
    ((MUXOUT_R_DIV_OUT << 26) & ADF_REG2_MUXOUT) |
    0x00000002;

  writeADFRegister(adfRegisters[2]);

  Serial.print(F("R divider set to "));
  Serial.print(MEASURE_R_COUNTER);
  Serial.println(F(", counting MUXOUT edges..."));

  uint8_t interruptPin = digitalPinToInterrupt(MUXOUT_MEASURE_PIN);
  double measured[MEASURE_WINDOWS];
  double total = 0.0;

  /*
    Probe the rate with a handful of edges before counting anything. If the R
    divider is not taking effect the tap is the full reference rate, and
    counting that would trip the watchdog within milliseconds and take the
    board down with no output. Sixteen edges is enough to know, and the
    shortest period seen is the one to trust: a missed edge can only make an
    interval look longer, never shorter.
  */
  muxoutEdgeCount = 0;
  muxoutLastStamp = 0;
  muxoutMinPeriod = 0xFFFFFFFF;
  attachInterrupt(interruptPin, countMuxoutEdge, RISING);

  uint32_t probeStart = micros();
  while (muxoutEdgeCount < 16 &&
         (uint32_t)(micros() - probeStart) < 200000UL) {
    optimistic_yield(1000);
  }

  uint32_t probePeriod = muxoutMinPeriod;
  detachInterrupt(interruptPin);

  if (probePeriod == 0xFFFFFFFF) {
    adfRegisters[2] = savedR2;
    writeADFRegister(adfRegisters[2]);
    printLine(F("The reference divider output produced no edges at all."));
    printLine(F("R divider in R2 may not be reaching the ADF4350."));
    return;
  }

  Serial.print(F("Shortest edge interval: "));
  Serial.print(probePeriod);
  Serial.println(F(" us"));

  if (probePeriod < 20) {
    adfRegisters[2] = savedR2;
    writeADFRegister(adfRegisters[2]);
    printLine(F("That is faster than 50 kHz, which the Oak cannot count, and"));
    printLine(F("much faster than fREFIN / 1023 should be. The reference"));
    printLine(F("divider is not being applied."));
    return;
  }

  attachInterrupt(interruptPin, countMuxoutEdge, RISING);

  for (uint8_t i = 0; i < MEASURE_WINDOWS; i++) {
    muxoutEdgeCount = 0;
    uint32_t start = micros();
    uint32_t window = MEASURE_WINDOW_MS * 1000UL;

    while ((uint32_t)(micros() - start) < window) {
      optimistic_yield(1000);
    }

    uint32_t edges = muxoutEdgeCount;
    double windowSeconds = (double)(micros() - start) / 1000000.0;
    measured[i] = (double)edges / windowSeconds;
    total += measured[i];

    Serial.print(F("  window "));
    Serial.print(i + 1);
    Serial.print(F(": "));
    Serial.print(edges);
    Serial.print(F(" edges in "));
    Serial.print(windowSeconds, 4);
    Serial.print(F(" s, "));
    Serial.print(measured[i] / MEASURE_R_COUNTER, 6);
    Serial.println(F(" MHz reference"));
  }

  detachInterrupt(interruptPin);

  if (measured[0] == 0.0 && measured[1] == 0.0) {
    printLine(F("No edges from the interrupt path; retrying by polling."));
    printLine(F("(interrupts not firing; GPIO16 cannot be used for this.)"));

    for (uint8_t i = 0; i < MEASURE_WINDOWS; i++) {
      uint32_t start = micros();
      uint32_t transitions = 0;
      uint8_t level = digitalRead(MUXOUT_MEASURE_PIN);

      while ((uint32_t)(micros() - start) < MEASURE_WINDOW_MS * 1000UL) {
        uint8_t now = digitalRead(MUXOUT_MEASURE_PIN);

        if (now != level) {
          transitions++;
          level = now;
        }
      }

      double windowSeconds = (double)(micros() - start) / 1000000.0;
      measured[i] = (double)transitions / 2.0 / windowSeconds;
      total += measured[i];

      Serial.print(F("  polled window "));
      Serial.print(i + 1);
      Serial.print(F(": "));
      Serial.print(transitions);
      Serial.print(F(" transitions in "));
      Serial.print(windowSeconds, 4);
      Serial.print(F(" s, "));
      Serial.print(measured[i] / MEASURE_R_COUNTER, 6);
      Serial.println(F(" MHz reference"));
    }
  }

  adfRegisters[2] = savedR2;
  writeADFRegister(adfRegisters[2]);

  double spread = fabs(measured[0] - measured[1]) /
                  ((measured[0] + measured[1]) / 2.0);

  Serial.println();

  if (measured[0] == 0.0 || measured[1] == 0.0) {
    printLine(F("No edges counted at all. The wire reads correctly at both DC"));
    printLine(F("levels, so check that P8 is not loaded and that the reference"));
    printLine(F("divider output is enabled."));
    return;
  }

  if (spread > 0.005) {
    Serial.print(F("Windows disagree by "));
    Serial.print(spread * 100.0, 2);
    Serial.println(F("%, so edges were probably dropped. Treat as unreliable;"));
    printLine(F("disconnect Wi-Fi and run again."));
    return;
  }

  double meanMHz = (total / MEASURE_WINDOWS) / MEASURE_R_COUNTER;
  double errorMHz = meanMHz - referenceMHz;

  Serial.print(F("Reference measured:  "));
  Serial.print(meanMHz, 6);
  Serial.println(F(" MHz"));

  Serial.print(F("Assumed reference:   "));
  Serial.print(referenceMHz, 6);
  Serial.println(F(" MHz"));

  Serial.print(F("Difference:          "));
  Serial.print(errorMHz * 1000.0, 3);
  Serial.print(F(" kHz, "));
  Serial.println(errorMHz / referenceMHz * 100.0, 4);

  printLine(F("-----------------------------------"));

  if (fabs(errorMHz / referenceMHz) > 0.001) {
    printLine(F("Reference is NOT within 0.1% of the assumed value."));
    printLine(F("Correct the reference with 'r' or calibrate with 'c'."));
  } else {
    printLine(F("Reference path checks out within 0.1%."));
    printLine(F("A wrong output frequency is not a reference problem."));
  }
}

double getProgrammedOutputFrequencyHz() {
  uint32_t integerPart = (adfRegisters[0] & ADF_REG0_INT) >> 15;
  uint32_t frac = (adfRegisters[0] & ADF_REG0_FRAC) >> 3;
  uint32_t mod = (adfRegisters[1] & ADF_REG1_MOD) >> 3;
  if (mod == 0) mod = 2;

  uint32_t rCounter = (adfRegisters[2] & ADF_REG2_RCNT) >> 14;
  if (rCounter == 0) rCounter = 1;

  uint32_t divSel = (adfRegisters[4] & ADF_REG4_RFDIV) >> 20;
  uint32_t outputDivider = 1UL << divSel;

  for (uint8_t i = 0; i <= 5; i++) {
    if ((adfRegisters[i] & 0x7UL) != i) {
      Serial.print(F("Warning: register "));
      Serial.print(i);
      Serial.println(F(" has the wrong address bits."));
      break;
    }
  }

  double pfdHz = (referenceMHz * 1000000.0) / (double)rCounter;
  double vcoHz = pfdHz * ((double)integerPart + (double)frac / (double)mod);
  return vcoHz / (double)outputDivider;
}
