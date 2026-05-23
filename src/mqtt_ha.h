#pragma once
#include <Arduino.h>

// Config fields — written directly by /mqttcfg POST handler in main.cpp
extern char     devId[20];
extern char     devName[48];
extern char     mqttHost[64];
extern uint16_t mqttPort;
extern char     mqttUser[32];
extern char     mqttPass[32];

// Flag — set by NVS save/delete ops; consumed by mqttTick()
extern volatile bool req_mqttDiscover;

// Call after WiFi is up and devId has been set
void mqttInit();

// Save config to NVS and apply connection change — call after updating the extern fields
void mqttApply();

// Drive MQTT reconnect + loop + deferred discovery — call every loop()
void mqttTick();

// Publish HA auto-discovery topics
void mqttPublishDiscovery();

// Publish cover open/close/stopped state after a replay
void mqttPublishCoverState(const char *label);

bool mqttIsConnected();
