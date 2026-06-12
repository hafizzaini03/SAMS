#include <esp_now.h>
#include <WiFi.h>

// This MUST be identical to the Sender's structure
typedef struct struct_message {
  float current_A;
  float power_W;
} struct_message;

struct_message incomingData;

// This function triggers automatically whenever data is received
void OnDataRecv(const uint8_t * mac, const uint8_t *incoming, int len) {
  // Copy the incoming bytes into our structure
  memcpy(&incomingData, incoming, sizeof(incomingData));
  
  // Print the received data to the Serial Monitor
  Serial.println("=== New Reading Received ===");
  Serial.print("Submodule Current: ");
  Serial.print(incomingData.current_A);
  Serial.println(" A");
  
  Serial.print("Submodule Power:   ");
  Serial.print(incomingData.power_W);
  Serial.println(" W");
  Serial.println("============================");
}

void setup() {
  Serial.begin(9600);
  
  // Set ESP32 as a Wi-Fi Station
  WiFi.mode(WIFI_STA);

  // Initialize ESP-NOW
  if (esp_now_init() != ESP_OK) {
    Serial.println("Error initializing ESP-NOW");
    return;
  }
  
  // Tell ESP-NOW to use the OnDataRecv function when a packet arrives
  esp_now_register_recv_cb(esp_now_recv_cb_t(OnDataRecv));
  
  Serial.println("Main Module Ready. Listening for Submodule data...");
}

void loop() {
  // The loop is left completely empty.
  // ESP-NOW triggers the 'OnDataRecv' function automatically in the background.
}
