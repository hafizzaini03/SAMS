#include <esp_now.h>
#include <WiFi.h>
#include "EmonLib.h"

// Create an instance
EnergyMonitor emon1;

// Calibration factor: For SCT-013-000
double calibration = 5.0; 

// REPLACE WITH YOUR RECEIVER'S MAC ADDRESS
uint8_t receiverAddress[] = {0xA4, 0xF0, 0x0F, 0x77, 0xED, 0x98};

// Structure to hold the sensor data being sent
// We will send both Current and Power
typedef struct struct_message {
  float current_A;
  float power_W;
} struct_message;

struct_message myData;
esp_now_peer_info_t peerInfo;

void setup() {
  Serial.begin(115200);
  
  // Set ESP32 as a Wi-Fi Station for ESP-NOW
  WiFi.mode(WIFI_STA);

  // Initialize EmonLib: input pin (D34), calibration factor
  emon1.current(34, calibration); 

  // Initialize ESP-NOW
  if (esp_now_init() != ESP_OK) {
    Serial.println("Error initializing ESP-NOW");
    return;
  }

  // Register the Receiver as a peer
  memcpy(peerInfo.peer_addr, receiverAddress, 6);
  peerInfo.channel = 0;  
  peerInfo.encrypt = false;
  
  if (esp_now_add_peer(&peerInfo) != ESP_OK){
    Serial.println("Failed to add peer");
    return;
  }
}

void main_loop() {
  // Calculate Irms (number of samples)
  double Irms = emon1.calcIrms(1480); 
  double apparentPower = Irms * 240.0; // Assuming 240V for Malaysia

  // Pack the data into the structure to send
  myData.current_A = Irms;
  myData.power_W = apparentPower;
  
  // Send the data via ESP-NOW
  esp_err_t result = esp_now_send(receiverAddress, (uint8_t *) &myData, sizeof(myData));

  // Print to Serial Monitor
  Serial.print("Current: ");
  Serial.print(Irms);
  Serial.print(" A | Apparent Power: ");
  Serial.print(apparentPower);
  Serial.print(" W | ESP-NOW: ");
  
  // Check if delivery was successful
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

