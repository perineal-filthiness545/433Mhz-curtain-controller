#include <Arduino.h>
#include <WiFi.h>
#include "wifi_portal.h"
#include <Adafruit_NeoPixel.h>
#include "constants.h"
#include "pkt_store.h"
#include "mqtt_ha.h"
#include "website.h"
#include "radio.h"

Adafruit_NeoPixel led(1, LED_PIN, NEO_GRB + NEO_KHZ800);

void ledSet(uint8_t r, uint8_t g, uint8_t b) {
    led.setPixelColor(0, led.Color(r, g, b));
    led.show();
}

static void doReplayBMC() {
    req_replay = false;
    int8_t slot = findBestBMCSlot(req_replayLbl);
    if (slot >= 0) {
        uint8_t nb = (pktMeta[slot].bitCount + 7) / 8;
        ledSet(0, 60, 60);
        transmitBMCBytes(pktMeta[slot].bits, nb);
        ledSet(0, 0, 0);
        Serial.printf("BMC TX [%s] %d bytes\n", req_replayLbl, nb);
        mqttPublishCoverState(req_replayLbl);
    } else {
        Serial.printf("BMC TX [%s]: no capture\n", req_replayLbl);
    }
}

void setup() {
    Serial.begin(115200);
    delay(2000);

    led.begin(); ledSet(0, 0, 0);
    radioInit();

    wifiBegin(); // connects or launches captive portal (never returns from portal)
    Serial.print("IP: "); Serial.println(WiFi.localIP());
    uint8_t mac[6];
    WiFi.macAddress(mac);
    snprintf(devId, sizeof(devId), "gdo_%02X%02X%02X", mac[3], mac[4], mac[5]);
    Serial.printf("Device ID: %s\n", devId);

    websiteInit();
    loadSaved();

    mqttInit();

    server.begin();
    Serial.println("Ready");
}

void loop() {
    if (req_restart && millis() >= restartAt) {
        Serial.println("Restarting…");
        delay(100);
        ESP.restart();
    }
    if (req_scan) {
        req_scan = false;
        doStartScan(req_scanMs, req_capLabel);
        req_capLabel[0] = 0;
    }
    if (req_stop) {
        req_stop = false;
        doStopScan();
    }
    if (capturing && scanStop && millis() >= scanStop) {
        doStopScan();
    }

    static uint32_t lastRSSIms = 0;
    if (capturing && millis() - lastRSSIms > 300) {
        lastRSSIms = millis();
        radioGetRSSI();
    }

    static uint32_t lastDiagMs = 0;
    if (capturing && millis() - lastDiagMs > 500) {
        lastDiagMs = millis();
        Serial.printf("GDO0=%d  RSSI=%.1f  edges=%lu  pkts=%d\n",
            digitalRead(CC_GDO0), (float)lastRSSI,
            (unsigned long)isrEdges, (int)pktCount);
    }

    radioHandlePkt();
    if (req_replay) doReplayBMC();
    mqttTick();
}
