#include <WiFi.h>
#include <HTTPClient.h>
#include "EmonLib.h"

// Create an instance
EnergyMonitor emon1;

// ESP32-C3 Analog Pin Change: GPIO 4 (ADC1_CH4)
#define CURRENT_SENSOR_PIN 4

// Calibration factor: Adjusted for SCT-013-000 (30A/30mA) with a 33 ohm burden resistor
double calibration = 30.3; 

// =========================
// WiFi Settings
// =========================
const char* WIFI_SSID = "Fuchi";
const char* WIFI_PASSWORD = "leicam6leicam6";

// =========================
// ThingSpeak Settings
// =========================
const char* THINGSPEAK_WRITE_APIKEY = "B09N2VNM7DVZAVST";
const char* THINGSPEAK_SERVER = "http://api.thingspeak.com/update";

// ThingSpeak free accounts should not update faster than every 15 seconds.
unsigned long thingspeakUpdateIntervalMs = 20000;
unsigned long lastThingSpeakUpdate = 0;

// WiFi reconnection timing
unsigned long wifiReconnectIntervalMs = 30000;
unsigned long lastWifiReconnectAttempt = 0;

WiFiClient client; // Added to prevent HTTPClient initialization issues on some board cores

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

  // Using the client instance ensures better stability during post requests
  http.begin(client, THINGSPEAK_SERVER);
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

  // Input pin updated to GPIO 4, calibration factor updated to 30.3
  emon1.current(CURRENT_SENSOR_PIN, calibration); 

  // Connect ESP32-C3 to WiFi
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
