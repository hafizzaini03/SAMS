#include <esp_now.h>
#include <WiFi.h>
#include <esp_wifi.h>
#include "EmonLib.h"

// =========================
// Submodule Configuration
// =========================
#define MODULE_ID 1
#define ROUTER_SSID "speedy"
#define FALLBACK_ESPNOW_CHANNEL 7

// Hardware pin selection for ESP32-C3 (GPIO 3 / ADC1_CH3)
#define CURRENT_SENSOR_PIN 3

EnergyMonitor emon1;
double calibration = 30.3; 

// Make sure this matches your Classic ESP32 Receiver MAC exactly!
uint8_t receiverAddress[] = {0xA4, 0xF0, 0x0F, 0x77, 0xED, 0x98};
uint8_t espnowChannel = FALLBACK_ESPNOW_CHANNEL;

typedef struct struct_message {
  uint8_t module_id;
  float current_A;
  float power_W;
} struct_message;

struct_message myData;
esp_now_peer_info_t peerInfo;

// =========================
// FIXED FOR ESP32 CORE 3.0+
// =========================
void OnDataSent(const esp_now_send_info_t *info, esp_now_send_status_t status) {
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
    return bestChannel;
  } else {
    Serial.println();
    Serial.print("Target router SSID not found. Using fallback.");
    return FALLBACK_ESPNOW_CHANNEL;
  }
}

void setup() {
  Serial.begin(115200);
  delay(3000); // Wait for Native USB to fully wake up

  Serial.println("\nESP32-C3 Submodule Sender Starting...");

  // 1. Scan the room to find the true channel of the router
  WiFi.mode(WIFI_STA);
  espnowChannel = findRouterChannel(ROUTER_SSID);
  Serial.print("Target Router Channel Detected: ");
  Serial.println(espnowChannel);

  // 2. HARDWARE CHANNEL LOCK
  WiFi.mode(WIFI_AP_STA);
  WiFi.softAP("C3_Fix_Net", "123456789", espnowChannel, 0); 
  
  // Set explicit Wi-Fi protocols for backward compatibility link
  esp_wifi_set_protocol(WIFI_IF_STA, WIFI_PROTOCOL_11B | WIFI_PROTOCOL_11G | WIFI_PROTOCOL_11N);
  esp_wifi_set_ps(WIFI_PS_NONE); // Disable power save to prevent C3 drops

  Serial.print("Hardware radio successfully LOCKED to Channel: ");
  Serial.println(espnowChannel);

  // Initialize the CT current sensor
  emon1.current(CURRENT_SENSOR_PIN, calibration);

  // Initialize ESP-NOW
  if (esp_now_init() != ESP_OK) {
    Serial.println("Error initializing ESP-NOW");
    return;
  }

  esp_now_register_send_cb(OnDataSent);

  // Configure Peer Settings
  memset(&peerInfo, 0, sizeof(peerInfo));
  memcpy(peerInfo.peer_addr, receiverAddress, 6);
  peerInfo.channel = espnowChannel; 
  peerInfo.encrypt = false;
  peerInfo.ifidx = WIFI_IF_STA;

  if (esp_now_add_peer(&peerInfo) != ESP_OK) {
    Serial.println("Failed to add peer");
    return;
  }
  Serial.println("Submodule Sender Ready.");
}

void main_loop() {
  double Irms = emon1.calcIrms(1480);
  double apparentPower = Irms * 240.0;

  myData.module_id = MODULE_ID;
  myData.current_A = (float)Irms;
  myData.power_W = (float)apparentPower;

  esp_err_t result = esp_now_send(receiverAddress, (uint8_t *) &myData, sizeof(myData));

  Serial.print("Module ID: ");
  Serial.print(MODULE_ID);
  Serial.print(" | Current: ");
  Serial.print(Irms);
  Serial.print(" A | Power: ");
  Serial.print(apparentPower);
  Serial.print(" W | ");

  if (result == ESP_OK) {
    Serial.println("Sent Success");
  } else {
    Serial.println("Sent Failed");
  }
  delay(1000);
}

void loop() {
  main_loop();
}
