#include <esp_now.h>
#include <WiFi.h>
#include <HTTPClient.h>

// =========================
// WiFi Settings
// =========================
const char* WIFI_SSID = "Hhhh";
const char* WIFI_PASSWORD = "hafiz123";

// =========================
// ThingSpeak Settings
// =========================
const char* THINGSPEAK_WRITE_APIKEY = "B09N2VNM7DVZAVST";
const char* THINGSPEAK_SERVER = "http://api.thingspeak.com/update";
const unsigned long THINGSPEAK_UPDATE_INTERVAL_MS = 20000;

// =========================
// System Settings
// =========================
#define MODULE_COUNT 2

// Current threshold to decide aircond ON/OFF.
float ON_THRESHOLD_A = 0.50;

// If no data is received from a module for this long, mark it as OFF.
const unsigned long MODULE_TIMEOUT_MS = 60000;

// Serial monitoring interval
const unsigned long SERIAL_MONITOR_INTERVAL_MS = 5000;

// WiFi reconnect interval
const unsigned long WIFI_RECONNECT_INTERVAL_MS = 30000;

// =========================
// Hardware Settings (4 LEDs total)
// =========================
// Module 1: Green=GPIO 4, Red=GPIO 5
// Module 2: Green=GPIO 18, Red=GPIO 19
const int GREEN_PINS[MODULE_COUNT] = {4, 18}; // GPIO 4,18 -> Green LED (AC is running)
const int RED_PINS[MODULE_COUNT] = {5, 19};   // GPIO 5,19 -> Red LED (AC is off or disconnected)

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
  Serial.print("Main Module IP Address: ");  Serial.println(WiFi.localIP());
  Serial.print("Main Module MAC Address: "); Serial.println(WiFi.macAddress());
  Serial.print("Main Module WiFi Channel: "); Serial.println(WiFi.channel());
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
    // If no data ever arrived OR transmitter disconnected/timed out
    if (!hasReceivedData[i] || (currentMillis - lastReceivedTime[i] > MODULE_TIMEOUT_MS)) {
      aircondStatus[i] = 0;
      
      // Safety: Force Red LED ON and Green LED OFF if signal drops out (Fixed for all modules)
      digitalWrite(GREEN_PINS[i], LOW);
      digitalWrite(RED_PINS[i], HIGH);
    }
  }
}

// =========================
// ESP-NOW Receive Callback
// =========================
void OnDataRecv(const esp_now_recv_info_t *recv_info, const uint8_t *incoming, int len) {
  if (len != sizeof(incomingData)) {
    Serial.print("Invalid ESP-NOW packet size received: ");
    Serial.println(len);
    return;
  }

  memcpy(&incomingData, incoming, sizeof(incomingData));
  uint8_t moduleID = incomingData.module_id;

  if (moduleID < 1 || moduleID > MODULE_COUNT) {
    return;
  }

  int index = moduleID - 1;

  currentReadings[index] = incomingData.current_A;
  powerReadings[index] = incomingData.power_W;
  lastReceivedTime[index] = millis();
  hasReceivedData[index] = true;

  // Decide ON/OFF based on current threshold and trigger specific LEDs (Fixed to allow both modules)
  if (incomingData.current_A >= ON_THRESHOLD_A) {
    aircondStatus[index] = 1; // ON
  } else {
    aircondStatus[index] = 0; // OFF
  }
  
  digitalWrite(GREEN_PINS[index], aircondStatus[index] ? HIGH : LOW);
  digitalWrite(RED_PINS[index], aircondStatus[index] ? LOW : HIGH);
}

// =========================
// Helper: Print Serial Summary
// =========================
void printMonitoringSummary() {
  Serial.println();
  Serial.println("========== Main Module Monitoring ==========");
  Serial.print("WiFi Status: ");   Serial.println(WiFi.status() == WL_CONNECTED ? "Connected" : "Disconnected");
  Serial.print("WiFi Channel: ");  Serial.println(WiFi.channel());
  Serial.print("ON Threshold: ");  Serial.print(ON_THRESHOLD_A); Serial.println(" A");
  Serial.println("--------------------------------------------");

  for (int i = 0; i < MODULE_COUNT; i++) {
    Serial.print("Module "); Serial.print(i + 1); Serial.print(" | ");

    if (!hasReceivedData[i]) {
      Serial.println("No data received yet (RED LED active)");
      continue;
    }

    Serial.print("Current: "); Serial.print(currentReadings[i], 3);
    Serial.print(" A | Power: "); Serial.print(powerReadings[i], 2);
    Serial.print(" W | Status: "); Serial.println(aircondStatus[i] == 1 ? "ON (GREEN)" : "OFF (RED)");
  }
  Serial.println("============================================");
}

// =========================
// Send Data to ThingSpeak
// Field 1 = Air-cond 1 On/Off
// Field 2 = Air-cond 2 On/Off
// Field 3 = Air-cond 1 Ampere
// Field 4 = Air-cond 2 Ampere
// Field 5 = Air-cond 1 Power
// Field 6 = Air-cond 2 Power
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

  // Field 1 and Field 2: ON/OFF status
  postData += "&field1=";
  postData += String(aircondStatus[0]);

  postData += "&field2=";
  postData += String(aircondStatus[1]);

  // Field 3 and Field 4: Current in ampere
  postData += "&field3=";
  postData += String(currentReadings[0], 3);

  postData += "&field4=";
  postData += String(currentReadings[1], 3);

  // Field 5 and Field 6: Power in watt
  postData += "&field5=";
  postData += String(powerReadings[0], 2);

  postData += "&field6=";
  postData += String(powerReadings[1], 2);

  http.begin(client, THINGSPEAK_SERVER);
  http.addHeader("Content-Type", "application/x-www-form-urlencoded");

  int httpResponseCode = http.POST(postData);
  String response = http.getString();

  http.end();

  if (httpResponseCode == 200 && response.toInt() > 0) {
    Serial.print("ThingSpeak update successful. Entry ID: ");
    Serial.println(response);

    Serial.println("Uploaded to ThingSpeak:");
    Serial.print("Field 1 Air-cond 1 Status: ");  Serial.println(aircondStatus[0]);
    Serial.print("Field 2 Air-cond 2 Status: ");  Serial.println(aircondStatus[1]);
    Serial.print("Field 3 Air-cond 1 Ampere: ");  Serial.println(currentReadings[0], 3);
    Serial.print("Field 4 Air-cond 2 Ampere: ");  Serial.println(currentReadings[1], 3);
    Serial.print("Field 5 Air-cond 1 Power: ");   Serial.println(powerReadings[0], 2);
    Serial.print("Field 6 Air-cond 2 Power: ");   Serial.println(powerReadings[1], 2);
  } else {
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

  for (int i = 0; i < MODULE_COUNT; i++) {
    pinMode(GREEN_PINS[i], OUTPUT);
    pinMode(RED_PINS[i], OUTPUT);
    digitalWrite(GREEN_PINS[i], LOW);
    digitalWrite(RED_PINS[i], HIGH);
  }

  Serial.println("\nMain Module Receiver Starting...");
  connectToWiFi();

  if (esp_now_init() != ESP_OK) {
    Serial.println("Error initializing ESP-NOW");
    return;
  }

  esp_now_register_recv_cb(OnDataRecv);
  Serial.println("ESP-NOW receiver initialized and ready.");
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

  if (lastThingSpeakUpdate == 0 || currentMillis - lastThingSpeakUpdate >= THINGSPEAK_UPDATE_INTERVAL_MS) {
    sendToThingSpeak();
    lastThingSpeakUpdate = currentMillis;
  }
}