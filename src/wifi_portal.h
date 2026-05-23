#pragma once
#include <Arduino.h>

// Restart flags — set internally and by /resetwifi; consumed by main loop()
extern volatile bool     req_restart;
extern volatile uint32_t restartAt;

// Connect using NVS credentials; launches captive portal if unavailable (never returns from portal)
void wifiBegin();

// Clear saved credentials and flag a restart (call before rebooting into portal)
void wifiClearCredentials();
