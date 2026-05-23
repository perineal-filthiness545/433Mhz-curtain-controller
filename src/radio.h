#pragma once
#include <Arduino.h>
#include "pkt_store.h"

extern uint16_t          pktData[MAX_PKTS][MAX_PULSE_W];
extern uint16_t          pktLen[MAX_PKTS];
extern volatile bool     capturing;
extern volatile float    lastRSSI;
extern volatile uint32_t scanStop;
extern volatile uint32_t isrEdges;
extern uint16_t          minPktN;

void     radioInit();
float    radioGetRSSI();
// Returns pulse count if a packet was ready and passed the filter; 0 otherwise
uint16_t radioConsumePkt(uint8_t slot);
void     radioHandlePkt();

void     doStartScan(uint32_t ms, const char *label);
void     doStopScan();
int8_t   findBestBMCSlot(const char *lbl);
void     transmitBMCBytes(const uint8_t *data, uint8_t count);
void     loadSaved();
void     saveToEEPROM(uint8_t slot);
void     deleteFromEEPROM(const char *lbl);
