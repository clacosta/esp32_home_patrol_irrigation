// This code is licensed under MIT License.
// Copyright (c) 2024 Claudio Costa (clacosta@gmail.com.com)
// 
// Version: 0.1
// 
// This project is designed to create an IoT irrigation system using an ESP32 board. 
// It monitors soil moisture using a capacitive sensor, controls a water pump via a relay, 
// allows configuration via a mobile app connected via Bluetooth, and sends data to 
// Firebase Realtime Database for remote monitoring and analysis.
//
// **To define your own Wi-Fi and Firebase credentials, create a copy of the 
// constantes.sample file and rename it to constantes.h in the root directory of 
// your project. Update the values in this new file.**

#include <Arduino.h>
#include "constantes.h"
#if defined(ESP32)
#include <WiFi.h>
#elif defined(ESP8266)
#include <ESP8266WiFi.h>
#endif
#include <Firebase_ESP_Client.h>
#include "time.h"
#include "esp_sntp.h"
#include "addons/TokenHelper.h"
#include "addons/RTDBHelper.h"

// GPIO
const int ledOutput = 2;
const int input32 = 32;
const int output26 = 26;

// NTP
const char *ntpServer1 = "pool.ntp.org";
const char *ntpServer2 = "time.nist.gov";
const long ntpGmtOffset_sec = 3600;
const int ntpDaylightOffset_sec = 3600;
const char *ntpTime_zone = "GMT0";

// Firebase
FirebaseData firebaseFbdo;
FirebaseAuth firebaseAuth;
FirebaseConfig firebaseConfig;
String firebaseUserUid;
bool firebaseSignupOK = false;

// Millis
unsigned long firebasePrevMillis = 0;
unsigned long historyTimePrevMillis = 0;
unsigned long activeTimePrevMillis = 0;
unsigned long idleTimePrevMillis = 0;

// Status
int count = 0;
int desiredHumidity = -1;
int activeTime = 10;
int idleTime = 60;
float voltageSensor = -1;
float relativeHumidity = -1;
bool relayStatus = false;

// Time
String dateTimeIso = "";
String dateIso = "";
String timeIso = "";

boolean timePeriodIsOver(unsigned long &periodStartTime, unsigned long timePeriod) {
  unsigned long currentMillis = millis();
  if (currentMillis - periodStartTime >= timePeriod) {
    periodStartTime = currentMillis;
    return true;
  } else return false;
}

void blinkLED(int ioPin, int blinkPeriod) {
  static unsigned long staticBlinkTimer;
  pinMode(ioPin, OUTPUT);
  if (timePeriodIsOver(staticBlinkTimer, blinkPeriod)) {
    digitalWrite(ioPin, !digitalRead(ioPin));
  }
}

void measureHumidity(int ioPin, int measurePeriod) {
  static unsigned long staticMeasureHumidityTimer;
  if (timePeriodIsOver(staticMeasureHumidityTimer, measurePeriod)) {
    int humidadeAnalog = analogRead(ioPin);
    voltageSensor = (humidadeAnalog * 3.3) / 4095.0;
    relativeHumidity = map(humidadeAnalog, 4095, 0, 0, 100);
  }
}

void setRelayStatus(bool status, bool force) {
  if (relayStatus != status || force) {
    relayStatus = status;
    if (Firebase.ready() && firebaseSignupOK) {
      // Write an Bool on the database path v1/jaboticabeira/relay
      if (Firebase.RTDB.setBool(&firebaseFbdo, "v1/jaboticabeira/relay", relayStatus)) {
        Serial.println("PASSED");
        Serial.println("PATH: " + firebaseFbdo.dataPath());
        Serial.println("TYPE: " + firebaseFbdo.dataType());
      } else {
        Serial.println("FAILED");
        Serial.println("REASON: " + firebaseFbdo.errorReason());
      }
    }
  }
}

void timeavailable(struct timeval *t) {
  Serial.println("Got time adjustment from NTP!");
}

void verifyLocalTime() {
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) {
    Serial.println("No time available (yet)");
    return;
  }
  char buffer[30];

  sprintf(buffer, "%04d-%02d-%02dT%02d:%02d:%02d",
          timeinfo.tm_year + 1900, timeinfo.tm_mon + 1, timeinfo.tm_mday,
          timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);

  dateTimeIso = String(buffer) + "Z";

  sprintf(buffer, "%04d-%02d-%02d",
          timeinfo.tm_year + 1900, timeinfo.tm_mon + 1, timeinfo.tm_mday);

  dateIso = String(buffer);

  sprintf(buffer, "%02d:%02d:%02d",
          timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);

  timeIso = String(buffer);

  Serial.println(dateTimeIso);
}

void setup() {
  // Serial
  Serial.begin(115200);

  // GPIO
  pinMode(output26, OUTPUT);
  digitalWrite(output26, LOW);

  // Wifi
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  while (WiFi.status() != WL_CONNECTED) {
    Serial.print("Connecting to ");
    Serial.println(WIFI_SSID);
    Serial.println(WiFi.status());
    blinkLED(ledOutput, 4000);
    delay(1000);
  }

  Serial.println("");
  Serial.println("WiFi connected.");
  Serial.println("IP address: ");
  Serial.println(WiFi.localIP());

  // NTP
  sntp_set_time_sync_notification_cb(timeavailable);
  configTime(ntpGmtOffset_sec, ntpDaylightOffset_sec, ntpServer1, ntpServer2);
  configTzTime(ntpTime_zone, ntpServer1, ntpServer2);

  // Firebase
  firebaseConfig.api_key = API_KEY;
  firebaseConfig.database_url = DATABASE_URL;
  firebaseAuth.user.email = USER_EMAIL;
  firebaseAuth.user.password = USER_PASSWORD;
  firebaseConfig.token_status_callback = tokenStatusCallback;
  firebaseConfig.max_token_generation_retry = 5;

  Firebase.begin(&firebaseConfig, &firebaseAuth);
  Firebase.reconnectWiFi(true);

  // Auth
  Serial.println("Getting User UID");
  while ((firebaseAuth.token.uid) == "") {
    Serial.print("Login with ");
    Serial.println(USER_EMAIL);
    blinkLED(ledOutput, 4000);
    delay(1000);
  }

  firebaseSignupOK = true;

  firebaseUserUid = firebaseAuth.token.uid.c_str();
  Serial.print("User UID: ");
  Serial.print(firebaseUserUid);

  // Initial Status
  setRelayStatus(false, true);
}

void loop() {
  // Led
  blinkLED(ledOutput, 1000);

  // Measure Humidity
  measureHumidity(input32, 5000);

  // Firebase
  if (Firebase.ready() && firebaseSignupOK && (millis() - firebasePrevMillis > 10000 || firebasePrevMillis == 0)) {
    firebasePrevMillis = millis();

    // Time
    verifyLocalTime();

    // Get
    if (Firebase.RTDB.getInt(&firebaseFbdo, "v1/jaboticabeira/desiredHumidity")) {
      if (firebaseFbdo.dataType() == "int") {
        desiredHumidity = firebaseFbdo.intData();
        Serial.println("PASSED");
        Serial.println("PATH: " + firebaseFbdo.dataPath());
        Serial.println("TYPE: " + firebaseFbdo.dataType());
        Serial.print("VALUE: ");
        Serial.println(desiredHumidity);
      }
    } else {
      Serial.println(firebaseFbdo.errorReason());
    }

    if (Firebase.RTDB.getInt(&firebaseFbdo, "v1/jaboticabeira/activeTime")) {
      if (firebaseFbdo.dataType() == "int") {
        activeTime = firebaseFbdo.intData();
        Serial.println("PASSED");
        Serial.println("PATH: " + firebaseFbdo.dataPath());
        Serial.println("TYPE: " + firebaseFbdo.dataType());
        Serial.print("VALUE: ");
        Serial.println(activeTime);
      }
    } else {
      Serial.println(firebaseFbdo.errorReason());
    }

    if (Firebase.RTDB.getInt(&firebaseFbdo, "v1/jaboticabeira/idleTime")) {
      if (firebaseFbdo.dataType() == "int") {
        idleTime = firebaseFbdo.intData();
        Serial.println("PASSED");
        Serial.println("PATH: " + firebaseFbdo.dataPath());
        Serial.println("TYPE: " + firebaseFbdo.dataType());
        Serial.print("VALUE: ");
        Serial.println(idleTime);
      }
    } else {
      Serial.println(firebaseFbdo.errorReason());
    }

    // Set
    if (dateTimeIso != "") {
      if (Firebase.RTDB.setString(&firebaseFbdo, "v1/jaboticabeira/currentDateTime", dateTimeIso)) {
        Serial.println("PASSED");
        Serial.println("PATH: " + firebaseFbdo.dataPath());
        Serial.println("TYPE: " + firebaseFbdo.dataType());
      } else {
        Serial.println("FAILED");
        Serial.println("REASON: " + firebaseFbdo.errorReason());
      }

      if ((millis() - historyTimePrevMillis > 60000 || historyTimePrevMillis == 0) && relativeHumidity > 0) {
        historyTimePrevMillis = millis();
        if (Firebase.RTDB.setFloat(&firebaseFbdo, "v1/jaboticabeira/history/" + dateIso + "/" + timeIso, relativeHumidity)) {
          Serial.println("PASSED");
          Serial.println("PATH: " + firebaseFbdo.dataPath());
          Serial.println("TYPE: " + firebaseFbdo.dataType());
        } else {
          Serial.println("FAILED");
          Serial.println("REASON: " + firebaseFbdo.errorReason());
        }
      }
    }

    if (Firebase.RTDB.setInt(&firebaseFbdo, "v1/jaboticabeira/_count", count)) {
      Serial.println("PASSED");
      Serial.println("PATH: " + firebaseFbdo.dataPath());
      Serial.println("TYPE: " + firebaseFbdo.dataType());
    } else {
      Serial.println("FAILED");
      Serial.println("REASON: " + firebaseFbdo.errorReason());
    }
    count++;

    if (Firebase.RTDB.setFloat(&firebaseFbdo, "v1/jaboticabeira/voltageSensor", voltageSensor)) {
      Serial.println("PASSED");
      Serial.println("PATH: " + firebaseFbdo.dataPath());
      Serial.println("TYPE: " + firebaseFbdo.dataType());
    } else {
      Serial.println("FAILED");
      Serial.println("REASON: " + firebaseFbdo.errorReason());
    }

    if (Firebase.RTDB.setFloat(&firebaseFbdo, "v1/jaboticabeira/relativeHumidity", relativeHumidity)) {
      Serial.println("PASSED");
      Serial.println("PATH: " + firebaseFbdo.dataPath());
      Serial.println("TYPE: " + firebaseFbdo.dataType());
    } else {
      Serial.println("FAILED");
      Serial.println("REASON: " + firebaseFbdo.errorReason());
    }
  }

  // Relay
  if (relativeHumidity < desiredHumidity && relativeHumidity > 0) {
    if (relayStatus == false && (millis() - idleTimePrevMillis > (idleTime * 1000) || idleTimePrevMillis == 0) && relativeHumidity > 0) {
      activeTimePrevMillis = millis();
      setRelayStatus(true, false);
      digitalWrite(output26, HIGH);
    }    
  } else {
    if (relayStatus == true && (millis() - activeTimePrevMillis > (activeTime * 1000) || activeTimePrevMillis == 0) && relativeHumidity > 0 && relayStatus == true) {    
      idleTimePrevMillis = millis();
      setRelayStatus(false, false);
      digitalWrite(output26, LOW);
    }        
  }
}
