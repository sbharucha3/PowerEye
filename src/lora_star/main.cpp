// PlatformIO entry point for the lora_star sketch. The original Arduino tabs are kept as-is and
// included in Arduino's order (main tab first, then the others alphabetically).
#include <Arduino.h>
#include <PowerEyeSense.h>

// Prototypes (Arduino IDE generated these automatically; PlatformIO needs them spelled out)
#include <stdint.h>
bool hubCommand(String& line);
bool nodeCommand(String& line);
void hubSetup();
void nodeSetup();
void hubOnPacket(const uint8_t* buf, int len, float rssi, float snr, int64_t rxUs);
void nodeOnPacket(const uint8_t* buf, int len, float rssi, float snr, int64_t rxUs);
void hubLoop();
void nodeLoop();

#include "lora_star.ino"
#include "hub.ino"
#include "node.ino"
