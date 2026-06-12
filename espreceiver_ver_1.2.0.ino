#include <esp_now.h>
#include <WiFi.h>
#include <esp_wifi.h>
#include <HTTPClient.h>

// =========================
// WiFi Settings
// =========================
const char* WIFI_SSID = "<Your_Wifi_Name>";
const char* WIFI_PASSWORD = "<Your_Wifi_Password>";

// =========================
// ThingSpeak Settings
// =========================
const char* THINGSPEAK_WRITE_APIKEY = "<Your_Thingspeak_API_Write_Key>";
const char* THINGSPEAK_SERVER = "http://api.thingspeak.com/update";

const unsigned long THINGSPEAK_UPDATE_INTERVAL_MS = 20000;

// =========================
// System Settings
// =========================
#define MODULE_COUNT 1

// Current threshold to decide aircond ON/OFF.
float ON_THRESHOLD_A = 0.50;

const unsigned long MODULE_TIMEOUT_MS = 60000;
const unsigned long SERIAL_MONITOR_INTERVAL_MS = 5000;
const unsigned long WIFI_RECONNECT_INTERVAL_MS = 30000;

// =========================
// ESP-NOW Payload Structure
// =========================
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
  Serial.println("IMPORTANT: Copy this MAC address into your ESP32-C3 sender configuration!");
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
// ESP-NOW Receive Callback (CORRECTED ARGUMENT STRUCT FOR CORE 3.0+)
// =========================
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
  }  
  else {
    Serial.print("ThingSpeak update failed. HTTP code: ");
    Serial.println(httpResponseCode);
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

  // Connect to network first to lock in channel assignment
  connectToWiFi();

  // Enforce older protocol compatibility limits on the receiver radio interface
  esp_wifi_set_protocol(WIFI_IF_STA, WIFI_PROTOCOL_11B | WIFI_PROTOCOL_11G | WIFI_PROTOCOL_11N);

  // Initialize ESP-NOW
  if (esp_now_init() != ESP_OK) {
    Serial.println("Error initializing ESP-NOW");
    return;
  }

  // Register updated callback function
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

  if (currentMillis - lastSerialMonitorPrint >= SERIAL_MONITOR_INTERVAL_MS) {
    printMonitoringSummary();
    lastSerialMonitorPrint = currentMillis;
  }

  if (lastThingSpeakUpdate == 0 ||
      currentMillis - lastThingSpeakUpdate >= THINGSPEAK_UPDATE_INTERVAL_MS) {
    
    sendToThingSpeak();
    lastThingSpeakUpdate = currentMillis;
  }
}
