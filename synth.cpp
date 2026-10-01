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
static const uint32_t ADF_REG2_RCNT    = 0x00FFC000UL;
static const uint32_t ADF_REG2_DB      = 0x00002000UL;
static const uint32_t ADF_REG2_CP      = 0x00000E00UL;
static const uint32_t ADF_REG2_LDF     = 0x00000100UL;
static const uint32_t ADF_REG2_PD_POL  = 0x00000040UL;
static const uint32_t ADF_REG2_MUXOUT  = 0x1C000000UL;
static const uint32_t ADF_REG3_CLKMOD  = 0x00030000UL;
static const uint32_t ADF_REG4_BANDCLK = 0x000FF000UL;
static const uint32_t ADF_REG4_RFDIV   = 0x00700000UL;
static const uint32_t ADF_REG4_FB_FUND = 0x00800000UL;
static const uint32_t ADF_REG4_PWR     = 0x00000018UL;
static const uint32_t ADF_REG4_RF_EN   = 0x00000020UL;
static const uint32_t ADF_REG5_LD_DIG  = 0x00400000UL;



static const double VCO_MIN_MHZ = 2200.0;
static const double VCO_MAX_MHZ = 4400.0;
static const uint32_t MAX_PFD_MHZ = 32.0;
static const uint16_t MAX_MODULUS = 4095;
static const uint16_t MAX_R_COUNTER = 1023;
static const uint16_t PHASE_VALUE = 1;
static const uint32_t CHARGE_PUMP_INDEX = 0x0000000FUL;

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
    MUXOUT is set to digital lock detect (M3:M2:M1 = 110).
    Bit 13 enables double buffering so that the R1 and R4 changes are
    latched by the final write to R0.
  */
  adfRegisters[2] =
    (((uint32_t)rCounter << 14) & ADF_REG2_RCNT) |
    ADF_REG2_DB |
    ((CHARGE_PUMP_INDEX << 9) & ADF_REG2_CP) |
    ADF_REG2_PD_POL |
    (frac == 0 ? ADF_REG2_LDF : 0UL) |
    ((muxoutMode << 26) & ADF_REG2_MUXOUT) |
    0x00000002;

  /*
    R3: band select clock mode, address 3
  */
  uint16_t bandClockDivider = 1;
  while (referenceMHz / bandClockDivider > 125.0 &&
         bandClockDivider < 255) {
    bandClockDivider++;
  }

  adfRegisters[3] =
    (0x00030000UL & ADF_REG3_CLKMOD) |
    0x00000003;

  /*
    R4: RF output enable, RF power, band select clock divider,
    output divider, feedback from VCO, address 4
  */
  adfRegisters[4] = ADF_REG4_FB_FUND;
  adfRegisters[4] &= ~ADF_REG4_PWR;
  adfRegisters[4] |= ((uint32_t)(rfPower & 0x03) << 3) & ADF_REG4_PWR;

  adfRegisters[4] &= ~ADF_REG4_RF_EN;

  if (rfOutputEnabled) {
    adfRegisters[4] |= ADF_REG4_RF_EN;
  }

  adfRegisters[4] &= ~(ADF_REG4_BANDCLK | ADF_REG4_RFDIV);
  adfRegisters[4] |= ((uint32_t)bandClockDivider << 12) & ADF_REG4_BANDCLK;
  adfRegisters[4] |= ((uint32_t)outputDividerSelect << 20) & ADF_REG4_RFDIV;
  adfRegisters[4] |= 0x00000004;

  /*
    R5: lock detect pin mode = digital, address 5
  */
  adfRegisters[5] = ADF_REG5_LD_DIG | 0x00000005;

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
