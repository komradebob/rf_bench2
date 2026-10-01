#ifndef CONFIG_H
#define CONFIG_H

#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <EEPROM.h>

// Wi-Fi
extern const char* WIFI_SSID;
extern const char* WIFI_PASSWORD;
extern const char* HOSTNAME;

// ADF4350/ADF4351 connections
const uint8_t ADF_DATA_PIN = P7;
const uint8_t ADF_CLK_PIN  = P9;
const uint8_t ADF_LE_PIN   = P6;
const uint8_t ADF_LD_PIN   = P5;

/*
  MUXOUT (pin 30 on the ADF4350/ADF4351) is a diagnostic tap: it can
  expose the reference divider output, the feedback divider output, or
  a lock detect level.

  The Oak silkscreen labels P0..P11 are NOT the same as the ESP8266 GPIO
  numbers, so both are given here. On this variant:

      silkscreen  GPIO   note
      P0          2      free, but boot strapping
      P1          5      free, drives the onboard LED
      P2          0      free, but boot strapping
      P3          3      Oak UART RX, in use
      P4          1      Oak UART TX, in use
      P5          4      ADF lock detect, in use
      P6         15      ADF LE, in use
      P7         13      ADF DATA, in use
      P8         12      free, but boot strapping
      P9         14      ADF CLK, in use
      P10        16      free, not strapping
      P11        17      A0 RF power detector, in use

  Default is GPIO16, silkscreen P10: spare and not a boot strapping pin,
  and it carries no LED or other loading, so a counter or scope sees the
  tap unloaded. GPIO5 / P1 is the alternative if you want digital lock
  detect to light the onboard LED, at the cost of the LED load on the
  line. Avoid GPIO0, GPIO2 and GPIO12, which are boot strapping pins and
  must be in a defined state at reset.

  Wiring this to the Oak is optional. The sketch never reads this pin;
  MUXOUT is only observed with a scope or counter, so the cleanest
  measurement is to probe MUXOUT directly on the ADF4350 module header
  and leave the Oak unconnected. The Oak pin matters only if the line is
  long enough that you want a termination point away from the module.
*/
const uint8_t ADF_MUXOUT_PIN = 16;

// RF power detector
const uint8_t RF_POWER_PIN = A0;

// Synthesizer selection
enum SynthType {
  SYNTH_ADF4350,
  SYNTH_ADF4351
};

extern SynthType synthType;

/*
  MUXOUT source select, ADF4350/ADF4351 R2 bits DB28:DB26.
  0 three-state, 1 DVDD, 2 DGND, 3 reference divider output,
  4 feedback divider output, 5 analog lock detect,
  6 digital lock detect.
*/
enum MuxoutMode {
  MUXOUT_THREE_STATE = 0,
  MUXOUT_DVDD        = 1,
  MUXOUT_DGND        = 2,
  MUXOUT_R_DIV_OUT   = 3,
  MUXOUT_N_DIV_OUT   = 4,
  MUXOUT_ANALOG_LD   = 5,
  MUXOUT_DIGITAL_LD  = 6
};

extern uint8_t muxoutMode;

void printMuxoutModeName(uint8_t mode);
double getMuxoutExpectedFrequencyHz();

// Current settings
const double DEFAULT_REFERENCE_MHZ = 10.0;
extern double referenceMHz;
extern double nominalReferenceMHz;
extern double refCorrectionFactor;
extern double outputMHz;
extern uint8_t rfPower;
extern bool rfOutputEnabled;

// Function declarations
void printLine(const __FlashStringHelper* text);
void printLine(const String& text);

void printMenu();
void serviceSerialMenu();
void reportLockStatus();
void reportAdcValue();

void programSynthesizer();
void calculateRegisters();
void writeADFRegister(uint32_t value);
void showSynthesizerInfo();

// Calibration & EEPROM
double getProgrammedOutputFrequencyHz();
bool loadCalibrationFromEEPROM();
bool saveCalibrationToEEPROM();
void calibrateReference();
void factorySettings();

#endif
