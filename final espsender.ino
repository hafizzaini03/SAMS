// Smart Air Conditioner Monitoring System (SaMS) - ESP-NOW Submodule Node
// Final Hardware-Verified & Network-Integrated Version

#include <Arduino.h>
#include <esp_now.h>
#include <WiFi.h>
#include <esp_wifi.h>
#include "EmonLib.h"

// =========================
// Sensor & Hardware Config
// =========================
#define ADC_INPUT 34
#define HOME_VOLTAGE 240.0
double calibration = 49.0; // Custom hardware-verified calibration

// =========================
// Network & Submodule Config
// =========================
#define MODULE_ID 2
#define ROUTER_SSID "seeyuuh"
#define FALLBACK_ESPNOW_CHANNEL 1

// REPLACE WITH YOUR MAIN MODULE / RECEIVER ESP32 MAC ADDRESS
uint8_t receiverAddress[] = {0x70, 0x4B, 0xCA, 0x92, 0x01, 0xC0};
uint8_t espnowChannel = FALLBACK_ESPNOW_CHANNEL;

EnergyMonitor emon1;

// Structure to hold the sensor data being sent
// Must match the receiver structure exactly
typedef struct struct_message {
  uint8_t module_id;
  float current_A;
  float power_W;
} struct_message;

struct_message myData;
esp_now_peer_info_t peerInfo;

// =========================
// ESP-NOW Send Callback
// =========================
void OnDataSent(const wifi_tx_info_t *tx_info, esp_now_send_status_t status) {
  Serial.print("  --> Delivery: ");
  Serial.println(status == ESP_NOW_SEND_SUCCESS ? "SUCCESS" : "FAILED");
}

// =========================
// Find Router WiFi Channel
// =========================
uint8_t findRouterChannel(const char* targetSSID) {
  Serial.println("\nScanning WiFi networks for router channel...");
  int networkCount = WiFi.scanNetworks();

  if (networkCount <= 0) {
    Serial.println("No WiFi networks found.");
    return FALLBACK_ESPNOW_CHANNEL;
  }

  int bestRSSI = -999;
  uint8_t bestChannel = FALLBACK_ESPNOW_CHANNEL;
  bool foundTargetSSID = false;

  for (int i = 0; i < networkCount; i++) {
    if (WiFi.SSID(i) == targetSSID && WiFi.RSSI(i) > bestRSSI) {
      bestRSSI = WiFi.RSSI(i);
      bestChannel = WiFi.channel(i);
      foundTargetSSID = true;
    }
  }

  WiFi.scanDelete();

  if (foundTargetSSID) {
    Serial.printf("Target SSID found! Selected channel: %d\n", bestChannel);
    return bestChannel;
  } else {
    Serial.printf("Target SSID not found. Using fallback channel: %d\n", FALLBACK_ESPNOW_CHANNEL);
    return FALLBACK_ESPNOW_CHANNEL;
  }
}

// =========================
// Setup
// =========================
void setup() {
  Serial.begin(115200);
  delay(1000);

  Serial.println("\n--- SaMS ESP32 Booting Up (ESP-NOW Sender) ---");
  Serial.printf("Module ID: %d\n", MODULE_ID);

  // Set ESP32 as WiFi Station for ESP-NOW
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  delay(500);

  // Automatically find and set the ESP-NOW channel
  espnowChannel = findRouterChannel(ROUTER_SSID);
  esp_wifi_set_channel(espnowChannel, WIFI_SECOND_CHAN_NONE);

  // ----------------------------------------------------
  // MODERN ESP32 ADC CONFIGURATION (Hardware-Verified)
  // ----------------------------------------------------
  analogReadResolution(10); 
  analogSetPinAttenuation(ADC_INPUT, ADC_11db); 

  // Initialize EmonLib
  emon1.current(ADC_INPUT, calibration);

  // Initialize ESP-NOW
  if (esp_now_init() != ESP_OK) {
    Serial.println("Error initializing ESP-NOW");
    return;
  }

  // Register send callback
  esp_now_register_send_cb(OnDataSent);

  // Register the Receiver/Main Module as a peer
  memset(&peerInfo, 0, sizeof(peerInfo));
  memcpy(peerInfo.peer_addr, receiverAddress, 6);
  peerInfo.channel = espnowChannel;
  peerInfo.encrypt = false;
  peerInfo.ifidx = WIFI_IF_STA;

  if (esp_now_add_peer(&peerInfo) != ESP_OK) {
    Serial.println("Failed to add peer");
    return;
  }

  Serial.println("System Ready. Beginning AC measurements...\n");
}

// =========================
// Main Loop
// =========================
void loop() {
  // Sample the AC wave 1480 times to calculate RMS current
  double amps = emon1.calcIrms(1480);

  // ----------------------------------------------------
  // THE SAMS NOISE GATE
  // ----------------------------------------------------
  if (amps < 0.40) {
    amps = 0.00;
  }

  // Calculate Apparent Power
  double watt = amps * HOME_VOLTAGE;

  // Pack the data into the ESP-NOW payload structure
  myData.module_id = MODULE_ID;
  myData.current_A = amps;
  myData.power_W = watt;

  // Print formatted output to the Serial Monitor
  Serial.print("[SaMS Node ");
  Serial.print(MODULE_ID);
  Serial.print("] Current: ");
  Serial.print(amps, 2);
  Serial.print(" A  |  Power: ");
  Serial.print(watt, 2);
  Serial.print(" W");

  // Send the data via ESP-NOW
  esp_now_send(receiverAddress, (uint8_t *) &myData, sizeof(myData));

  // Wait 2 seconds before taking the next reading
  delay(2000);
}
