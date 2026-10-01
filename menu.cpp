#include "config.h"

String readLine() {
  String input = "";

  while (true) {
    while (Serial.available()) {
      char c = Serial.read();

      if (c == '\r' || c == '\n') {
        if (input.length() > 0) {
          return input;
        }
      } else {
        input += c;
      }
    }

    delay(5);
  }
}

String readLineAllowEmpty() {
  String input = "";

  while (true) {
    while (Serial.available()) {
      char c = Serial.read();

      if (c == '\r' || c == '\n') {
        return input;
      }

      input += c;
    }

    delay(5);
  }
}

double readNumber(const __FlashStringHelper* prompt) {
  Serial.println(prompt);
  Serial.print(F("> "));

  String input = readLine();
  double value = strtod(input.c_str(), nullptr);

  Serial.print(F("Value entered: "));
  Serial.println(value, 6);

  return value;
}

void printFormattedHz(double freqHz) {
  uint64_t hz = (uint64_t)llround(freqHz);
  char buf[32];
  snprintf(buf, sizeof(buf), "%llu", (unsigned long long)hz);
  int len = strlen(buf);
  for (int i = 0; i < len; i++) {
    if (i > 0 && (len - i) % 3 == 0) {
      Serial.print(',');
    }
    Serial.print(buf[i]);
  }
  Serial.print(F(" Hz"));
}

double parseCounterReadout(String raw) {
  raw.trim();
  if (raw.length() == 0) return -1.0;
  if (raw == "0") return 0.0;

  String lower = raw;
  lower.toLowerCase();
  bool explicitMHz = false;
  bool explicitHz = false;

  if (lower.indexOf("mhz") >= 0) {
    explicitMHz = true;
  } else if (lower.indexOf("hz") >= 0) {
    explicitHz = true;
  }

  // Strip commas, spaces, underscores, and letters
  String cleaned = "";
  bool hasDot = false;
  for (size_t i = 0; i < raw.length(); i++) {
    char c = raw[i];
    if (isDigit(c)) {
      cleaned += c;
    } else if (c == '.' && !hasDot) {
      cleaned += c;
      hasDot = true;
    }
  }

  if (cleaned.length() == 0) return -1.0;

  double val = strtod(cleaned.c_str(), nullptr);
  if (val <= 0.0 || isnan(val) || isinf(val)) return -1.0;

  double freqHz = 0.0;
  if (explicitMHz) {
    freqHz = val * 1000000.0;
  } else if (explicitHz) {
    freqHz = val;
  } else {
    // If entered without units:
    // Numbers >= 10000.0 are in Hz (e.g. 1000000000 or 35000000)
    // Numbers < 10000.0 are in MHz (e.g. 1000.000000 or 144.000000)
    if (val < 10000.0) {
      freqHz = val * 1000000.0;
    } else {
      freqHz = val;
    }
  }

  return freqHz;
}

// EEPROM Calibration storage
struct CalibrationData {
  uint32_t magic;
  double correctionFactor;
  double referenceMHz;
  double nominalReferenceMHz;
  uint32_t checksum;
};

const uint32_t CALIBRATION_MAGIC = 0x41444643; // 'ADFC'

uint32_t calculateCalibrationChecksum(const CalibrationData& data) {
  uint32_t crc = 0x12345678;
  const uint8_t* p = (const uint8_t*)&data;
  size_t len = offsetof(CalibrationData, checksum);
  for (size_t i = 0; i < len; i++) {
    crc ^= p[i];
    for (uint8_t bit = 0; bit < 8; bit++) {
      if (crc & 1) {
        crc = (crc >> 1) ^ 0xEDB88320;
      } else {
        crc >>= 1;
      }
    }
  }
  return crc;
}

bool loadCalibrationFromEEPROM() {
  EEPROM.begin(sizeof(CalibrationData));
  CalibrationData data;
  EEPROM.get(0, data);
  EEPROM.end();

  if (data.magic != CALIBRATION_MAGIC) {
    return false;
  }

  if (data.checksum != calculateCalibrationChecksum(data)) {
    Serial.println(F("EEPROM calibration checksum mismatch."));
    return false;
  }

  if (isnan(data.correctionFactor) || isinf(data.correctionFactor) ||
      data.correctionFactor < 0.5 || data.correctionFactor > 2.0) {
    Serial.println(F("EEPROM calibration factor out of bounds."));
    return false;
  }

  if (isnan(data.referenceMHz) || isinf(data.referenceMHz) ||
      data.referenceMHz < 1.0 || data.referenceMHz > 100.0) {
    Serial.println(F("EEPROM reference frequency out of bounds."));
    return false;
  }

  if (isnan(data.nominalReferenceMHz) || isinf(data.nominalReferenceMHz) ||
      data.nominalReferenceMHz < 1.0 || data.nominalReferenceMHz > 100.0) {
    Serial.println(F("EEPROM nominal reference frequency out of bounds."));
    return false;
  }

  refCorrectionFactor = data.correctionFactor;
  referenceMHz = data.referenceMHz;
  nominalReferenceMHz = data.nominalReferenceMHz;
  return true;
}

bool saveCalibrationToEEPROM() {
  CalibrationData data;
  data.magic = CALIBRATION_MAGIC;
  data.correctionFactor = refCorrectionFactor;
  data.referenceMHz = referenceMHz;
  data.nominalReferenceMHz = nominalReferenceMHz;
  data.checksum = calculateCalibrationChecksum(data);

  EEPROM.begin(sizeof(CalibrationData));
  EEPROM.put(0, data);
  bool ok = EEPROM.commit();
  EEPROM.end();

  if (ok) {
    printLine(F("Calibration saved to EEPROM."));
  } else {
    printLine(F("Error: Failed to save calibration to EEPROM."));
  }
  return ok;
}

void printMenu() {
  Serial.println();
  Serial.println(F("-----------------------------------"));
  Serial.print(F("Output Power:        Code "));
  Serial.print(rfPower);
  switch (rfPower) {
    case 0: Serial.print(F(" (~-4 dBm)")); break;
    case 1: Serial.print(F(" (~-1 dBm)")); break;
    case 2: Serial.print(F(" (~+2 dBm)")); break;
    case 3: Serial.print(F(" (~+5 dBm)")); break;
  }
  Serial.println(rfOutputEnabled ? F(" [ON]") : F(" [OFF]"));

  Serial.print(F("Output Frequency:    "));
  Serial.print(outputMHz, 4);
  Serial.println(F(" MHz"));

  Serial.print(F("Reference Frequency: "));
  Serial.print(referenceMHz, 6);
  Serial.println(F(" MHz"));
  Serial.println(F("-----------------------------------"));
  Serial.println(F("========== RF BENCH MENU =========="));
  Serial.println(F("s - Select synthesizer"));
  Serial.println(F("f - Set output frequency"));
  Serial.println(F("r - Set reference frequency"));
  Serial.println(F("c - Calibrate reference from counter"));
  Serial.println(F("z - Reset calibration to nominal"));
  Serial.println(F("F - Factory settings (erase EEPROM)"));
  Serial.println(F("p - Set RF power"));
  Serial.println(F("e - Enable RF output"));
  Serial.println(F("d - Disable RF output"));
  Serial.println(F("a - Read RF detector A/D"));
  Serial.println(F("l - Read lock status"));
  Serial.println(F("u - Select MUXOUT monitor source"));
  Serial.println(F("i - Show current settings"));
  Serial.println(F("m - Show this menu"));
  Serial.println(F("==================================="));
  Serial.println(F("Enter command:"));
}

void showSettings() {
  Serial.println();
  Serial.println(F("Current settings:"));

  Serial.print(F("Synthesizer: "));
  Serial.println(
    synthType == SYNTH_ADF4350 ? F("ADF4350") : F("ADF4351")
  );

  Serial.print(F("Output frequency: "));
  Serial.print(outputMHz, 6);
  Serial.println(F(" MHz"));

  double progHz = getProgrammedOutputFrequencyHz();
  Serial.print(F("Programmed output: "));
  printFormattedHz(progHz);
  Serial.print(F(" ("));
  Serial.print(progHz / 1000000.0, 6);
  Serial.println(F(" MHz)"));

  Serial.print(F("Reference frequency: "));
  Serial.print(referenceMHz, 6);
  Serial.println(F(" MHz ("));
  printFormattedHz(referenceMHz * 1000000.0);
  Serial.println(F(")"));

  Serial.print(F("Nominal reference:     "));
  Serial.print(nominalReferenceMHz, 6);
  Serial.println(F(" MHz"));

  Serial.print(F("Reference correction factor: "));
  Serial.println(refCorrectionFactor, 8);

  double ppm = (refCorrectionFactor - 1.0) * 1e6;
  Serial.print(F("Reference offset: "));
  if (ppm >= 0.0) Serial.print('+');
  Serial.print(ppm, 3);
  Serial.println(F(" ppm"));

  Serial.print(F("RF power code: "));
  Serial.println(rfPower);

  Serial.print(F("RF output: "));
  Serial.println(rfOutputEnabled ? F("ON") : F("OFF"));

  Serial.print(F("MUXOUT monitor:       "));
  Serial.print(muxoutMode);
  Serial.print(F(" - "));
  printMuxoutModeName(muxoutMode);

  if (muxoutMode == MUXOUT_R_DIV_OUT ||
      muxoutMode == MUXOUT_N_DIV_OUT) {
    Serial.print(F("Expected MUXOUT freq: "));
    Serial.print(getMuxoutExpectedFrequencyHz() / 1000000.0, 6);
    Serial.println(F(" MHz"));
  }
}

void selectSynthesizer() {
  Serial.println();
  Serial.println(F("Select synthesizer:"));
  Serial.println(F("0 - ADF4350, frequency range 137.5 to 4400 MHz"));
  Serial.println(F("1 - ADF4351, frequency range 35 to 4400 MHz"));
  Serial.print(F("> "));

  String input = readLine();
  int selection = input.toInt();

  if (selection == 0) {
    synthType = SYNTH_ADF4350;
    printLine(F("ADF4350 selected."));
  } else if (selection == 1) {
    synthType = SYNTH_ADF4351;
    printLine(F("ADF4351 selected."));
  } else {
    printLine(F("Invalid synthesizer selection."));
    return;
  }

  double minimumFrequency =
    synthType == SYNTH_ADF4350 ? 137.5 : 35.0;

  if (outputMHz < minimumFrequency) {
    outputMHz = minimumFrequency;
    Serial.print(F("Output adjusted to minimum: "));
    Serial.print(outputMHz, 1);
    Serial.println(F(" MHz"));
  }

  showSynthesizerInfo();
  programSynthesizer();
}

void setFrequency() {
  double minimumFrequency =
    synthType == SYNTH_ADF4350 ? 137.5 : 35.0;

  Serial.print(F("Frequency limits: "));
  Serial.print(minimumFrequency, 1);
  Serial.println(F(" to 4400.0 MHz"));

  double value = readNumber(F("Enter output frequency in MHz: "));

  if (value < minimumFrequency || value > 4400.0) {
    printLine(F("Frequency is outside the allowed range."));
    return;
  }

  outputMHz = value;
  programSynthesizer();
}

void setReference() {
  Serial.println(F("Reference frequency limits: 1 to 100 MHz."));

  double value = readNumber(F("Enter reference frequency in MHz: "));

  if (value < 1.0 || value > 100.0) {
    printLine(F("Reference frequency is outside the allowed range."));
    return;
  }

  referenceMHz = value;
  nominalReferenceMHz = value;
  refCorrectionFactor = 1.0;
  programSynthesizer();
  saveCalibrationToEEPROM();
}

void calibrateReference() {
  Serial.println();
  Serial.println(F("--- Calibrate Reference Frequency ---"));

  if (!rfOutputEnabled) {
    printLine(F("Note: RF output is currently disabled. Enabling RF output for counter measurement..."));
    rfOutputEnabled = true;
    programSynthesizer();
  }

  double progHz = getProgrammedOutputFrequencyHz();

  Serial.print(F("Target output:        "));
  Serial.print(outputMHz, 6);
  Serial.println(F(" MHz"));

  Serial.print(F("Programmed output:    "));
  printFormattedHz(progHz);
  Serial.print(F(" ("));
  Serial.print(progHz / 1000000.0, 6);
  Serial.println(F(" MHz)"));

  Serial.print(F("Current reference:    "));
  Serial.print(referenceMHz, 6);
  Serial.println(F(" MHz"));

  Serial.print(F("Current correction:   "));
  Serial.println(refCorrectionFactor, 8);

  Serial.println(F("Enter complete frequency counter readout (Hz, e.g. 1000000023, or 0 to reset):"));
  Serial.print(F("> "));

  String input = readLine();
  double measuredHz = parseCounterReadout(input);

  if (measuredHz < 0.0) {
    printLine(F("Invalid frequency counter readout. Calibration aborted."));
    return;
  }

  if (measuredHz == 0.0) {
    referenceMHz = nominalReferenceMHz;
    refCorrectionFactor = 1.0;
    Serial.print(F("Resetting reference frequency to nominal "));
    Serial.print(referenceMHz, 6);
    Serial.println(F(" MHz."));
    programSynthesizer();
    saveCalibrationToEEPROM();
    return;
  }

  Serial.print(F("Counter reading:      "));
  printFormattedHz(measuredHz);
  Serial.print(F(" ("));
  Serial.print(measuredHz / 1000000.0, 6);
  Serial.println(F(" MHz)"));

  double deviationRatio = fabs(measuredHz - progHz) / progHz;
  if (deviationRatio > 0.20) {
    printLine(F("Warning: Measured frequency deviates by >20% from programmed output."));
    printLine(F("Please verify frequency counter connection. Calibration aborted."));
    return;
  }

  // Exact factor: ratio of measured physical frequency to programmed synth frequency
  double factor = measuredHz / progHz;
  double newReferenceMHz = referenceMHz * factor;

  if (newReferenceMHz < 1.0 || newReferenceMHz > 100.0) {
    printLine(F("Calculated reference frequency outside 1 to 100 MHz range. Calibration aborted."));
    return;
  }

  double oldReferenceMHz = referenceMHz;
  referenceMHz = newReferenceMHz;
  refCorrectionFactor = referenceMHz / nominalReferenceMHz;

  double deltaHz = measuredHz - progHz;

  Serial.println();
  Serial.println(F("Calibration results:"));
  Serial.print(F("  Measured error:      "));
  if (deltaHz >= 0.0) Serial.print('+');
  Serial.print(deltaHz, 1);
  Serial.print(F(" Hz ("));
  double errPpm = (deltaHz / progHz) * 1e6;
  if (errPpm >= 0.0) Serial.print('+');
  Serial.print(errPpm, 3);
  Serial.println(F(" ppm)"));

  Serial.print(F("  Old reference:       "));
  Serial.print(oldReferenceMHz, 6);
  Serial.println(F(" MHz"));

  Serial.print(F("  New reference:       "));
  Serial.print(referenceMHz, 6);
  Serial.println(F(" MHz"));

  Serial.print(F("  Correction factor:   "));
  Serial.println(refCorrectionFactor, 8);

  double offsetPpm = (refCorrectionFactor - 1.0) * 1e6;
  Serial.print(F("  Offset from nominal: "));
  if (offsetPpm >= 0.0) Serial.print('+');
  Serial.print(offsetPpm, 3);
  Serial.println(F(" ppm"));

  printLine(F("Reprogramming synthesizer with corrected reference..."));
  programSynthesizer();

  saveCalibrationToEEPROM();

  double newProgHz = getProgrammedOutputFrequencyHz();
  Serial.print(F("New programmed output: "));
  printFormattedHz(newProgHz);
  Serial.print(F(" ("));
  Serial.print(newProgHz / 1000000.0, 6);
  Serial.println(F(" MHz)"));
}

void resetCalibration() {
  referenceMHz = nominalReferenceMHz;
  refCorrectionFactor = 1.0;
  programSynthesizer();
  saveCalibrationToEEPROM();
  Serial.print(F("Calibration reset to nominal reference: "));
  Serial.print(referenceMHz, 6);
  Serial.println(F(" MHz"));
}

void setMuxoutMode() {
  Serial.println();
  printLine(F("MUXOUT monitor source (ADF4350 R2 bits DB28:DB26):"));
  Serial.println(F("MUXOUT must be wired to an Oak pin, P14 by default."));
  Serial.println(F("Measure MUXOUT with a scope or counter, not the Oak ADC."));
  Serial.println();

  Serial.println(F("0 - Three-state (high impedance)"));
  Serial.println(F("1 - DVDD (logic high)"));
  Serial.println(F("2 - DGND (logic low)"));
  Serial.println(F("3 - Reference divider output  fREFIN / R"));
  Serial.println(F("4 - Feedback divider output  PFD rate"));
  Serial.println(F("5 - Analog lock detect  (DC voltage)"));
  Serial.println(F("6 - Digital lock detect  (high when locked)"));
  Serial.print(F("> "));

  String input = readLine();
  long value = atol(input.c_str());

  if (value < 0 || value > 6) {
    printLine(F("Invalid MUXOUT mode. No change made."));
    return;
  }

  muxoutMode = (uint8_t)value;

  Serial.print(F("MUXOUT mode: "));
  Serial.print(muxoutMode);
  Serial.print(F(" - "));
  printMuxoutModeName(muxoutMode);

  programSynthesizer();

  if (muxoutMode == MUXOUT_R_DIV_OUT || muxoutMode == MUXOUT_N_DIV_OUT) {
    Serial.print(F("Expected MUXOUT frequency: "));
    Serial.print(getMuxoutExpectedFrequencyHz() / 1000000.0, 6);
    Serial.println(F(" MHz"));
    printLine(F("Compare that against your counter reading on MUXOUT."));
  }
}

void factorySettings() {
  Serial.println();
  printLine(F("--- FACTORY SETTINGS ---"));
  printLine(F("This erases the calibration stored in EEPROM and restores:"));
  printLine(F("  Synthesizer   : ADF4350, 137.5 to 4400 MHz"));
  printLine(F("  Reference     : 10.000000 MHz nominal, factor 1.00000000"));
  printLine(F("  Output        : 1000.000000 MHz"));
  printLine(F("  RF power code : 3 (~+5 dBm), RF output ON"));
  printLine(F("  MUXOUT monitor: 6, digital lock detect"));
  Serial.println();
  printLine(F("The reference reverts to the 10 MHz default."));
  printLine(F("Use r if your actual reference is different."));
  Serial.println();
  printLine(F("Type FACTORY and press Enter to confirm."));
  printLine(F("Press Enter on its own to cancel."));
  Serial.print(F("> "));

  String input = readLineAllowEmpty();
  input.trim();
  input.toUpperCase();

  if (input != "FACTORY") {
    printLine(F("Factory settings cancelled; nothing changed."));
    return;
  }

  synthType = SYNTH_ADF4350;
  referenceMHz = DEFAULT_REFERENCE_MHZ;
  nominalReferenceMHz = DEFAULT_REFERENCE_MHZ;
  refCorrectionFactor = 1.0;
  outputMHz = 1000.0;
  rfPower = 3;
  rfOutputEnabled = true;
  muxoutMode = MUXOUT_DIGITAL_LD;

  EEPROM.begin(sizeof(CalibrationData));

  for (size_t i = 0; i < sizeof(CalibrationData); i++) {
    EEPROM.write(i, 0xFF);
  }

  bool erased = EEPROM.commit();
  EEPROM.end();

  if (erased) {
    printLine(F("EEPROM calibration erased."));
  } else {
    printLine(F("Warning: EEPROM commit failed. Calibration may survive a reboot."));
  }

  printLine(F("Defaults restored; reprogramming synthesizer..."));
  programSynthesizer();
  showSettings();
}

void setPower() {
  Serial.println(F("RF power settings:"));
  Serial.println(F("0 = approximately -4 dBm"));
  Serial.println(F("1 = approximately -1 dBm"));
  Serial.println(F("2 = approximately +2 dBm"));
  Serial.println(F("3 = approximately +5 dBm"));
  Serial.println(F("Allowed power codes: 0 to 3"));

  double value = readNumber(F("Enter RF power code: "));

  if (value < 0 || value > 3 || value != floor(value)) {
    printLine(F("Power code must be 0, 1, 2, or 3."));
    return;
  }

  rfPower = (uint8_t)value;
  programSynthesizer();
}

void readAdc() {
  int raw = analogRead(RF_POWER_PIN);

  Serial.print(F("RF detector ADC value: "));
  Serial.println(raw);

  Serial.println(F("Oak ADC range is typically 0 to 1023."));
  Serial.println(F("Convert to voltage only after confirming the Oak ADC scale."));
}

void handleCommand(char command) {
  if (command == 'F') {
    factorySettings();
    Serial.println();
    printMenu();
    return;
  }

  switch (tolower(command)) {
    case 's':
      selectSynthesizer();
      break;

    case 'f':
      setFrequency();
      break;

    case 'r':
      setReference();
      break;

    case 'c':
      calibrateReference();
      break;

    case 'z':
      resetCalibration();
      break;

    case 'p':
      setPower();
      break;

    case 'e':
      rfOutputEnabled = true;
      programSynthesizer();
      printLine(F("RF output enabled."));
      break;

    case 'd':
      rfOutputEnabled = false;
      programSynthesizer();
      printLine(F("RF output disabled."));
      break;

    case 'a':
      readAdc();
      break;

    case 'l':
      reportLockStatus();
      break;

    case 'u':
      setMuxoutMode();
      break;

    case 'i':
      showSettings();
      break;

    case 'm':
      break;

    default:
      printLine(F("Unknown command. Enter m for the menu."));
      break;
  }

  Serial.println();
  printMenu();
}

void serviceSerialMenu() {
  if (!Serial.available()) {
    return;
  }

  char command = Serial.read();

  if (command != '\r' && command != '\n') {
    handleCommand(command);
  }

  while (Serial.available()) {
    Serial.read();
  }
}
