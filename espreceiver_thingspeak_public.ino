#include <esp_now.h>
#include <WiFi.h>
#include <HTTPClient.h>

// =========================
// WiFi Settings
// =========================
// Main module must connect to the same router SSID that sender scans.
const char* WIFI_SSID = "YOUR_WIFI_NAME";
const char* WIFI_PASSWORD = "YOUR_WIFI_PASSWORD";

// =========================
// ThingSpeak Settings
// =========================
const char* THINGSPEAK_WRITE_APIKEY = "YOUR_THINGSPEAK_WRITEAPI";
const char* THINGSPEAK_SERVER = "http://api.thingspeak.com/update";

// ThingSpeak free channel update interval should be >= 15 seconds.
// 20 seconds is safer.
const unsigned long THINGSPEAK_UPDATE_INTERVAL_MS = 20000;

// =========================
// System Settings
// =========================
// ThingSpeak supports up to 8 fields per channel.
// Field 1 = Module 1, Field 2 = Module 2, etc.
#define MODULE_COUNT 1

// Current threshold to decide aircond ON/OFF.
// You MUST calibrate this after observing real OFF and ON current readings.
float ON_THRESHOLD_A = 0.50;

// If no data is received from a module for this long, mark it as OFF.
const unsigned long MODULE_TIMEOUT_MS = 60000;

// Serial monitoring interval
const unsigned long SERIAL_MONITOR_INTERVAL_MS = 5000;

// WiFi reconnect interval
const unsigned long WIFI_RECONNECT_INTERVAL_MS = 30000;

// =========================
// ESP-NOW Payload Structure
// =========================
// IMPORTANT:
// This structure must be exactly the same as the sender/submodule code.
typedef struct struct_message {
  uint8_t module_id;
  float current_A;
  float power_W;
} struct_message;

struct_message incomingData;

// =========================
// Module Data Storage
// =========================
float currentReadings[MODULE_COUNT] = {0};
float powerReadings[MODULE_COUNT] = {0};
int aircondStatus[MODULE_COUNT] = {0}; 
bool hasReceivedData[MODULE_COUNT] = {false};
unsigned long lastReceivedTime[MODULE_COUNT] = {0};

// Timing
unsigned long lastThingSpeakUpdate = 0;
unsigned long lastSerialMonitorPrint = 0;
unsigned long lastWifiReconnectAttempt = 0;

// =========================
// WiFi Client
// =========================
WiFiClient client;

// =========================
// Helper: Connect WiFi
// =========================
void connectToWiFi() {
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  Serial.println();
  Serial.print("Connecting main module to WiFi: ");
  Serial.println(WIFI_SSID);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println();
  Serial.println("WiFi connected.");
  Serial.print("Main Module IP Address: ");
  Serial.println(WiFi.localIP());

  Serial.print("Main Module MAC Address: ");
  Serial.println(WiFi.macAddress());

  Serial.print("Main Module WiFi Channel: ");
  Serial.println(WiFi.channel());

  Serial.println();
  Serial.println("IMPORTANT:");
  Serial.println("Use this MAC address in every sender's receiverAddress[].");
  Serial.println("Senders will scan this router SSID to find the same WiFi channel.");
  Serial.println();
}

// =========================
// Helper: WiFi Reconnect
// =========================
void handleWiFiReconnect() {
  unsigned long currentMillis = millis();

  if ((WiFi.status() != WL_CONNECTED) &&
      (currentMillis - lastWifiReconnectAttempt >= WIFI_RECONNECT_INTERVAL_MS)) {
    
    Serial.println("WiFi disconnected. Attempting reconnect...");
    WiFi.disconnect();
    WiFi.reconnect();

    lastWifiReconnectAttempt = currentMillis;
  }
}

// =========================
// Helper: Update Timeout Status
// =========================
void updateModuleTimeoutStatus() {
  unsigned long currentMillis = millis();

  for (int i = 0; i < MODULE_COUNT; i++) {
    if (!hasReceivedData[i]) {
      aircondStatus[i] = 0;
    } 
    else if (currentMillis - lastReceivedTime[i] > MODULE_TIMEOUT_MS) {
      aircondStatus[i] = 0;
    }
  }
}

// =========================
// ESP-NOW Receive Callback
// =========================
// This callback signature is for newer ESP32 Arduino core versions.
void OnDataRecv(const esp_now_recv_info_t *recv_info, const uint8_t *incoming, int len) {
  if (len != sizeof(incomingData)) {
    Serial.print("Invalid ESP-NOW packet size. Expected: ");
    Serial.print(sizeof(incomingData));
    Serial.print(" bytes, Received: ");
    Serial.println(len);
    return;
  }

  memcpy(&incomingData, incoming, sizeof(incomingData));

  uint8_t moduleID = incomingData.module_id;

  if (moduleID < 1 || moduleID > MODULE_COUNT) {
    Serial.print("Invalid Module ID received: ");
    Serial.println(moduleID);
    return;
  }

  int index = moduleID - 1;

  currentReadings[index] = incomingData.current_A;
  powerReadings[index] = incomingData.power_W;
  lastReceivedTime[index] = millis();
  hasReceivedData[index] = true;

  // Decide ON/OFF based on current threshold
  if (incomingData.current_A >= ON_THRESHOLD_A) {
    aircondStatus[index] = 1; // ON
  } else {
    aircondStatus[index] = 0; // OFF
  }
}

// =========================
// Helper: Print Serial Summary
// =========================
void printMonitoringSummary() {
  Serial.println();
  Serial.println("========== Main Module Monitoring ==========");

  Serial.print("WiFi Status: ");
  Serial.println(WiFi.status() == WL_CONNECTED ? "Connected" : "Disconnected");

  Serial.print("WiFi Channel: ");
  Serial.println(WiFi.channel());

  Serial.print("ON Threshold: ");
  Serial.print(ON_THRESHOLD_A);
  Serial.println(" A");

  Serial.println("--------------------------------------------");

  for (int i = 0; i < MODULE_COUNT; i++) {
    Serial.print("Module ");
    Serial.print(i + 1);
    Serial.print(" | ");

    if (!hasReceivedData[i]) {
      Serial.println("No data received yet");
      continue;
    }

    Serial.print("Current: ");
    Serial.print(currentReadings[i], 3);
    Serial.print(" A | Power: ");
    Serial.print(powerReadings[i], 2);
    Serial.print(" W | Status: ");
    Serial.print(aircondStatus[i] == 1 ? "ON" : "OFF");

    Serial.print(" | Last received: ");
    Serial.print((millis() - lastReceivedTime[i]) / 1000);
    Serial.println(" seconds ago");
  }

  Serial.println("============================================");
  Serial.println();
}

// =========================
// Helper: Send Status to ThingSpeak
// =========================
int sendToThingSpeak() {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi not connected. ThingSpeak update skipped.");
    return -1;
  }

  updateModuleTimeoutStatus();

  HTTPClient http;

  String postData = "";
  postData += "api_key=";
  postData += THINGSPEAK_WRITE_APIKEY;

  for (int i = 0; i < MODULE_COUNT; i++) {
    postData += "&field";
    postData += String(i + 1);
    postData += "=";
    postData += String(aircondStatus[i]);
  }

  http.begin(client, THINGSPEAK_SERVER);
  http.addHeader("Content-Type", "application/x-www-form-urlencoded");

  int httpResponseCode = http.POST(postData);
  String response = http.getString();

  http.end();

  if (httpResponseCode == 200 && response.toInt() > 0) {
    Serial.print("ThingSpeak update successful. Entry ID: ");
    Serial.println(response);

    Serial.print("Uploaded statuses: ");
    for (int i = 0; i < MODULE_COUNT; i++) {
      Serial.print("Field ");
      Serial.print(i + 1);
      Serial.print("=");
      Serial.print(aircondStatus[i]);

      if (i < MODULE_COUNT - 1) {
        Serial.print(", ");
      }
    }
    Serial.println();
  } 
  else {
    Serial.print("ThingSpeak update failed. HTTP code: ");
    Serial.print(httpResponseCode);
    Serial.print(" | Response: ");
    Serial.println(response);
  }

  return httpResponseCode;
}

// =========================
// Setup
// =========================
void setup() {
  Serial.begin(115200);

  Serial.println();
  Serial.println("Main Module Receiver Starting...");

  // Connect main module to WiFi first.
  // This sets the ESP32 radio to the router channel.
  connectToWiFi();

  // Initialize ESP-NOW after WiFi is active
  if (esp_now_init() != ESP_OK) {
    Serial.println("Error initializing ESP-NOW");
    return;
  }

  // Register receive callback
  esp_now_register_recv_cb(OnDataRecv);

  Serial.println("ESP-NOW receiver initialized.");
  Serial.println("Main module is ready to receive submodule data.");
}

// =========================
// Main Loop
// =========================
void loop() {
  unsigned long currentMillis = millis();

  handleWiFiReconnect();
  updateModuleTimeoutStatus();

  // Print monitoring summary every few seconds
  if (currentMillis - lastSerialMonitorPrint >= SERIAL_MONITOR_INTERVAL_MS) {
    printMonitoringSummary();
    lastSerialMonitorPrint = currentMillis;
  }

  // Send all aircond ON/OFF statuses to ThingSpeak every 20 seconds
  if (lastThingSpeakUpdate == 0 ||
      currentMillis - lastThingSpeakUpdate >= THINGSPEAK_UPDATE_INTERVAL_MS) {
    
    sendToThingSpeak();
    lastThingSpeakUpdate = currentMillis;
  }
}