#include "WiFi.h"

void setup() {
  Serial.begin(9600); 
  WiFi.mode(WIFI_MODE_STA);
  
  // Wait a moment for the board to finish its noisy startup
  delay(2000); 
}

void loop() {
  // Print a blank line to separate readings
  Serial.println(); 
  
  // Print the MAC Address
  Serial.print("MAC Address: ");
  Serial.println(WiFi.macAddress());
  
  // Wait 3 seconds before printing again
  delay(3000); 
}
