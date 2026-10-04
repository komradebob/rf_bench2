#include "config.h"

// Wi-Fi settings live in secrets.h so the credentials stay out of the
// repository. A fresh clone without secrets.h falls back to placeholders
// and still compiles; see secrets.h.example.
#if defined(__has_include)
  #if __has_include("secrets.h")
    #include "secrets.h"
  #endif
#endif

#ifndef RF_BENCH2_HAS_SECRETS
  const char* WIFI_SSID     = "YOUR_SSID";
  const char* WIFI_PASSWORD = "YOUR_PASSWORD";
  const char* HOSTNAME      = "rfbench";
#endif

// Initial synthesizer selection
SynthType synthType = SYNTH_ADF4350;

// MUXOUT diagnostic tap, defaulting to digital lock detect
uint8_t muxoutMode = MUXOUT_DIGITAL_LD;

// Whether the reference in use came from EEPROM rather than the default
bool referenceLoadedFromEeprom = false;

// Initial operating values
double referenceMHz = DEFAULT_REFERENCE_MHZ;
double nominalReferenceMHz = DEFAULT_REFERENCE_MHZ;
double outputMHz = 1000.0;
uint8_t rfPower = 3;
bool rfOutputEnabled = true;

void printLine(const __FlashStringHelper* text) {
  Serial.println(text);
}

void printLine(const String& text) {
  Serial.println(text);
}

void connectWiFi() {
  printLine(F("Starting Wi-Fi..."));

  WiFi.mode(WIFI_STA);
  WiFi.hostname(HOSTNAME);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  Serial.print(F("Connecting to SSID: "));
  Serial.println(WIFI_SSID);

  uint32_t startTime = millis();

  while (WiFi.status() != WL_CONNECTED &&
         millis() - startTime < 30000UL) {
    delay(500);
    Serial.print('.');
  }

  Serial.println();

  if (WiFi.status() == WL_CONNECTED) {
    printLine(F("Wi-Fi connected."));

    Serial.print(F("IP address: "));
    Serial.println(WiFi.localIP());

    Serial.print(F("Network name: "));
    Serial.println(HOSTNAME);
  } else {
    printLine(F("Wi-Fi connection timed out."));
    printLine(F("Synthesizer operation will continue."));
  }
}

void setup() {
  Serial.begin(115200);
  delay(500);

  printLine(F(""));
  printLine(F("RF bench controller starting"));
  printLine(F("Serial interface: Oak UART via TTL-to-USB"));

  pinMode(ADF_DATA_PIN, OUTPUT);
  pinMode(ADF_CLK_PIN, OUTPUT);
  pinMode(ADF_LE_PIN, OUTPUT);
  pinMode(ADF_LD_PIN, INPUT);
  pinMode(RF_POWER_PIN, INPUT);

  digitalWrite(ADF_DATA_PIN, LOW);
  digitalWrite(ADF_CLK_PIN, LOW);
  digitalWrite(ADF_LE_PIN, LOW);

  showSynthesizerInfo();

  if (loadCalibrationFromEEPROM()) {
    Serial.print(F("Loaded reference from EEPROM: "));
    Serial.print(referenceMHz, 6);
    Serial.println(F(" MHz"));
  } else {
    printLine(F("No saved calibration found in EEPROM. Using default reference."));
  }

  programSynthesizer();
  delay(1000);
  reportLockStatus();

  connectWiFi();
  printMenu();
}

void loop() {
  serviceSerialMenu();

  static uint32_t lastReport = 0;

  if (millis() - lastReport >= 3000UL) {
    lastReport = millis();

    //reportLockStatus();

    if (WiFi.status() != WL_CONNECTED) {
      printLine(F("Wi-Fi disconnected; attempting reconnect."));
      WiFi.reconnect();
    }
  }
}
