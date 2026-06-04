#include <esp_now.h>
#include <WiFi.h>
#include <esp_wifi.h>
#include "EmonLib.h"

// =========================
// Submodule Configuration
// =========================
// Change this for each submodule:
#define MODULE_ID 1

// This must be the same WiFi SSID that the main module ESP32 connects to.
#define ROUTER_SSID "Fuchi"

// Fallback channel if the router SSID cannot be found during scanning.
#define FALLBACK_ESPNOW_CHANNEL 6

// ESP32-C3 Analog Pin Change: GPIO 4 (ADC1_CH4)
#define CURRENT_SENSOR_PIN 4

// Create an EmonLib instance
EnergyMonitor emon1;

// Calibration factor: Adjusted for SCT-013-000 (30A/30mA) with a 33 ohm burden resistor
double calibration = 30.3; 

// REPLACE WITH YOUR MAIN MODULE / RECEIVER ESP32 MAC ADDRESS
uint8_t receiverAddress[] = {0xA4, 0xF0, 0x0F, 0x77, 0xED, 0x98};

// The ESP-NOW channel will be detected automatically by scanning ROUTER_SSID.
uint8_t espnowChannel = FALLBACK_ESPNOW_CHANNEL;

// Structure to hold the sensor data being sent
typedef struct struct_message {
  uint8_t module_id;
  float current_A;
  float power_W;
} struct_message;

struct_message myData;
esp_now_peer_info_t peerInfo;

// =========================
// ESP-NOW Send Callback (Updated for ESP32-C3 compatibility)
// =========================
void OnDataSent(const uint8_t *mac_addr, esp_now_send_status_t status) {
  Serial.print("ESP-NOW Delivery Status: ");
  Serial.println(status == ESP_NOW_SEND_SUCCESS ? "Delivered" : "Failed");
}

// =========================
// Find Router WiFi Channel
// =========================
uint8_t findRouterChannel(const char* targetSSID) {
  Serial.println();
  Serial.println("Scanning WiFi networks to find router channel...");

  int networkCount = WiFi.scanNetworks();

  if (networkCount <= 0) {
    Serial.println("No WiFi networks found.");
    Serial.print("Using fallback ESP-NOW channel: ");
    Serial.println(FALLBACK_ESPNOW_CHANNEL);
    return FALLBACK_ESPNOW_CHANNEL;
  }

  int bestRSSI = -999;
  uint8_t bestChannel = FALLBACK_ESPNOW_CHANNEL;
  bool foundTargetSSID = false;

  for (int i = 0; i < networkCount; i++) {
    String foundSSID = WiFi.SSID(i);
    int foundRSSI = WiFi.RSSI(i);
    uint8_t foundChannel = WiFi.channel(i);

    Serial.print("Found SSID: ");
    Serial.print(foundSSID);
    Serial.print(" | Channel: ");
    Serial.print(foundChannel);
    Serial.print(" | RSSI: ");
    Serial.println(foundRSSI);

    if (foundSSID == targetSSID && foundRSSI > bestRSSI) {
      bestRSSI = foundRSSI;
      bestChannel = foundChannel;
      foundTargetSSID = true;
    }
  }

  WiFi.scanDelete();

  if (foundTargetSSID) {
    Serial.println();
    Serial.print("Target router SSID found: ");
    Serial.println(targetSSID);
    Serial.print("Selected router channel: ");
    Serial.println(bestChannel);
    return bestChannel;
  } else {
    Serial.println();
    Serial.print("Target router SSID not found: ");
    Serial.println(targetSSID);
    Serial.print("Using fallback ESP-NOW channel: ");
    Serial.println(FALLBACK_ESPNOW_CHANNEL);
    return FALLBACK_ESPNOW_CHANNEL;
  }
}

// =========================
// Setup
// =========================
void setup() {
  Serial.begin(115200);

  // Set ESP32-C3 as WiFi Station for ESP-NOW.
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();

  delay(500);

  Serial.println();
  Serial.println("ESP32-C3 Submodule Sender Starting...");
  Serial.print("Module ID: ");
  Serial.println(MODULE_ID);

  // Automatically find the router channel used by the main module.
  espnowChannel = findRouterChannel(ROUTER_SSID);

  // Force sender to use the same channel as the router/main module.
  esp_wifi_set_channel(espnowChannel, WIFI_SECOND_CHAN_NONE);

  Serial.print("Sender ESP-NOW Channel Set To: ");
  Serial.println(espnowChannel);

  // Initialize EmonLib: Input pin updated to GPIO 4
  emon1.current(CURRENT_SENSOR_PIN, calibration);

  // Initialize ESP-NOW
  if (esp_now_init() != ESP_OK) {
    Serial.println("Error initializing ESP-NOW");
    return;
  }

  // Register callback to check actual ESP-NOW delivery status
  esp_now_register_send_cb(OnDataSent);

  // Clear peerInfo to avoid random leftover data
  memset(&peerInfo, 0, sizeof(peerInfo));

  // Register the Receiver/Main Module as a peer
  memcpy(peerInfo.peer_addr, receiverAddress, 6);
  peerInfo.channel = espnowChannel;
  peerInfo.encrypt = false;
  peerInfo.ifidx = WIFI_IF_STA;

  if (esp_now_add_peer(&peerInfo) != ESP_OK) {
    Serial.println("Failed to add peer");
    return;
  }

  Serial.println("ESP-NOW peer added successfully.");
  Serial.println("Submodule Sender Ready.");
}

// =========================
// Main Loop
// =========================
void main_loop() {

  // Calculate Irms
  double Irms = emon1.calcIrms(1480);
  double apparentPower = Irms * 240.0; // Assuming 240V for Malaysia

  // Pack the data into the structure to send
  myData.module_id = MODULE_ID;
  myData.current_A = Irms;
  myData.power_W = apparentPower;

  // Send the data via ESP-NOW
  esp_err_t result = esp_now_send(receiverAddress, (uint8_t *) &myData, sizeof(myData));

  // Print to Serial Monitor
  Serial.print("Module ID: ");
  Serial.print(MODULE_ID);
  Serial.print(" | Current: ");
  Serial.print(Irms);
  Serial.print(" A | Apparent Power: ");
  Serial.print(apparentPower);
  Serial.print(" W | ESP-NOW Send Request: ");

  if (result == ESP_OK) {
    Serial.println("Sent Success");
  } else {
    Serial.println("Sent Failed");
  }

  Serial.println("-----------------------------------");

  delay(1000);
}

void loop() {
  main_loop();
}
