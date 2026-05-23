#pragma once
#include <Arduino.h>
#include <ESPAsyncWebServer.h>

// HTTP server instance (defined in website.cpp)
extern AsyncWebServer server;

// Request flags — set by web handlers, consumed by loop()
extern volatile bool     req_scan;
extern volatile uint32_t req_scanMs;
extern volatile bool     req_stop;
extern char              req_capLabel[12];
extern volatile bool     req_replay;
extern char              req_replayLbl[12];

extern const char index_html[];

// Register all HTTP route handlers — call from setup() before server.begin()
void websiteInit();
