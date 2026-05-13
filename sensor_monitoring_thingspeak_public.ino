#include <WiFi.h>
#include <HTTPClient.h>
#include "EmonLib.h"

// Create an instance
EnergyMonitor emon1;

// Calibration factor: For SCT-013-000 (100A/50mA)
double calibration = 5.0; 

// =========================
// WiFi Settings
// =========================
// Replace with WiFi name and password
const char* WIFI_SSID = "YOUR_WIFI_NAME";
const char* WIFI_PASSWORD = "YOUR_WIFI_PASSWORD";

// =========================
// ThingSpeak Settings
// =========================
const char* THINGSPEAK_WRITE_APIKEY = "YOUR_THINGSPEAK_WRITEAPI";
const char* THINGSPEAK_SERVER = "http://api.thingspeak.com/update";

// ThingSpeak free accounts should not update faster than every 15 seconds.
// 20 seconds is safe.
unsigned long thingspeakUpdateIntervalMs = 20000;
unsigned long lastThingSpeakUpdate = 0;

// WiFi reconnection timing
unsigned long wifiReconnectIntervalMs = 30000;
unsigned long lastWifiReconnectAttempt = 0;

void connectToWiFi() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  Serial.print("Connecting to WiFi");

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println();
  Serial.println("WiFi connected");
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());
}

void handleWiFiReconnect() {
  unsigned long currentMillis = millis();

  if ((WiFi.status() != WL_CONNECTED) &&
      (currentMillis - lastWifiReconnectAttempt >= wifiReconnectIntervalMs)) {
    
    Serial.println("Reconnecting to WiFi...");
    WiFi.disconnect();
    WiFi.reconnect();

    lastWifiReconnectAttempt = currentMillis;
  }
}

int sendToThingSpeak(double current_A, double power_W) {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi not connected. ThingSpeak update skipped.");
    return -1;
  }

  HTTPClient http;

  String postData = "";
  postData += "api_key=";
  postData += THINGSPEAK_WRITE_APIKEY;
  postData += "&field1=";
  postData += String(current_A, 3);
  postData += "&field2=";
  postData += String(power_W, 2);

  http.begin(THINGSPEAK_SERVER);
  http.addHeader("Content-Type", "application/x-www-form-urlencoded");

  int httpResponseCode = http.POST(postData);
  String response = http.getString();

  http.end();

  if (httpResponseCode == 200 && response.toInt() > 0) {
    Serial.print("ThingSpeak update successful. Entry ID: ");
    Serial.println(response);
  } 
  else {
    Serial.print("ThingSpeak update failed. HTTP code: ");
    Serial.print(httpResponseCode);
    Serial.print(" | Response: ");
    Serial.println(response);
  }

  return httpResponseCode;
}

void setup() {
  Serial.begin(115200);

  // input pin (D34), calibration factor
  emon1.current(34, calibration); 

  // Connect ESP32 to WiFi
  connectToWiFi();
}

void main_loop() {
  // Keep WiFi alive
  handleWiFiReconnect();

  // Check WiFi status and show in Serial Monitor
  Serial.println(WiFi.status() == WL_CONNECTED ? "WiFi Status: Connected" : "WiFi Status: Disconnected");

  // Calculate Irms (number of samples)
  double Irms = emon1.calcIrms(1480); 
  double apparentPower = Irms * 240.0; // Assuming 240V for Malaysia

  Serial.print("Current: ");
  Serial.print(Irms);
  Serial.print(" A | Apparent Power: ");
  Serial.print(apparentPower);
  Serial.println(" W");

  // Send data to ThingSpeak every 20 seconds
  unsigned long currentMillis = millis();

  if (lastThingSpeakUpdate == 0 ||
      currentMillis - lastThingSpeakUpdate >= thingspeakUpdateIntervalMs) {
    
    sendToThingSpeak(Irms, apparentPower);
    lastThingSpeakUpdate = currentMillis;
  }

  delay(1000);
}

void loop() {
  main_loop();
}