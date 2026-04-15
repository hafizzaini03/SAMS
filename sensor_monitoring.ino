#include "EmonLib.h"

// Create an instance
EnergyMonitor emon1;

// Calibration factor: For SCT-013-000 (100A/50mA), 
// if using a 62 ohm burden resistor, the value is ~30.
// If your sensor has a built-in resistor (SCT-013-030), it's different.
double calibration = 5.0; 

void setup() {
  Serial.begin(115200);
  
  // input pin (D34), calibration factor
  emon1.current(34, calibration); 
}

void main_loop() {
  // Calculate Irms (number of samples)
  double Irms = emon1.calcIrms(1480); 

  Serial.print("Current: ");
  Serial.print(Irms);
  Serial.print(" A | Apparent Power: ");
  Serial.print(Irms * 240.0); // Assuming 240V for Malaysia
  Serial.println(" W");

  delay(1000);
}

void loop() {
  main_loop();
}
