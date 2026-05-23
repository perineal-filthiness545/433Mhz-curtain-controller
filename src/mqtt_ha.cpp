#include "mqtt_ha.h"
#include <WiFi.h>
#include <PubSubClient.h>
#include <Preferences.h>
#include "pkt_store.h"

// Replay request — defined in main.cpp, consumed by loop()
extern volatile bool req_replay;
extern char          req_replayLbl[12];

// ── State ──────────────────────────────────────────────────────────
static WiFiClient   wifiClient;
static PubSubClient mqtt(wifiClient);
static bool         mqttEnabled = false;
static uint32_t     mqttLastTry = 0;

char     devId[20]    = "";
char     devName[48]  = "Garage Door";
char     mqttHost[64] = "";
uint16_t mqttPort     = 1883;
char     mqttUser[32] = "";
char     mqttPass[32] = "";
volatile bool req_mqttDiscover = false;

// ── NVS ────────────────────────────────────────────────────────────
static void loadConfig() {
    Preferences prefs;
    prefs.begin("mqtt", true);
    prefs.getString("host", mqttHost, sizeof(mqttHost));
    mqttPort = prefs.getUShort("port", 1883);
    prefs.getString("user", mqttUser, sizeof(mqttUser));
    prefs.getString("pass", mqttPass, sizeof(mqttPass));
    prefs.getString("name", devName, sizeof(devName));
    prefs.end();
    if (!devName[0]) strncpy(devName, "Garage Door", sizeof(devName) - 1);
    mqttEnabled = (mqttHost[0] != 0);
}

static void saveConfig() {
    Preferences prefs;
    prefs.begin("mqtt", false);
    prefs.putString("host", mqttHost);
    prefs.putUShort("port", mqttPort);
    prefs.putString("user", mqttUser);
    prefs.putString("pass", mqttPass);
    prefs.putString("name", devName);
    prefs.end();
}

// ── Discovery & callback ──────────────────────────────────────────
void mqttPublishDiscovery() {
    if (!mqtt.connected()) return;
    char topic[128], payload[600];
    snprintf(topic, sizeof(topic), "home/433mhz/%s/avail", devId);
    mqtt.publish(topic, "online", true);

    bool hasOpen = false, hasClose = false, hasPause = false;
    for (uint8_t i = 0; i < pktCount; i++) {
        if (!pktMeta[i].saved || pktMeta[i].bitCount < 8) continue;
        if (strcmp(pktMeta[i].label, "OPEN")  == 0) hasOpen  = true;
        if (strcmp(pktMeta[i].label, "CLOSE") == 0) hasClose = true;
        if (strcmp(pktMeta[i].label, "PAUSE") == 0 ||
            strcmp(pktMeta[i].label, "STOP")  == 0) hasPause = true;
    }
    if (hasOpen && hasClose) {
        snprintf(topic, sizeof(topic), "homeassistant/cover/%s/config", devId);
        snprintf(payload, sizeof(payload),
            "{\"name\":\"%s\","
            "\"unique_id\":\"%s_cover\","
            "\"device_class\":\"garage\","
            "\"device\":{\"identifiers\":[\"%s\"],\"name\":\"%s\",\"model\":\"ESP32-C3\"},"
            "\"command_topic\":\"home/433mhz/%s/cover/cmd\","
            "\"state_topic\":\"home/433mhz/%s/cover/state\","
            "\"state_open\":\"open\",\"state_closed\":\"closed\",\"state_stopped\":\"stopped\","
            "\"payload_open\":\"OPEN\",\"payload_close\":\"CLOSE\","
            "%s"
            "\"optimistic\":true,"
            "\"availability_topic\":\"home/433mhz/%s/avail\","
            "\"payload_available\":\"online\","
            "\"payload_not_available\":\"offline\"}",
            devName, devId, devId, devName, devId, devId,
            hasPause ? "\"payload_stop\":\"STOP\"," : "",
            devId);
        mqtt.publish(topic, payload, true);
        Serial.printf("MQTT: cover discovery sent as [%s]\n", devName);
    }

    for (uint8_t i = 0; i < pktCount; i++) {
        if (!pktMeta[i].saved || pktMeta[i].bitCount < 8) continue;
        bool dup = false;
        for (uint8_t j = 0; j < i; j++)
            if (pktMeta[j].saved && strcmp(pktMeta[j].label, pktMeta[i].label) == 0) { dup = true; break; }
        if (dup) continue;
        const char *lbl = pktMeta[i].label;
        snprintf(topic, sizeof(topic), "homeassistant/button/%s_%s/config", devId, lbl);
        snprintf(payload, sizeof(payload),
            "{\"name\":\"%s %s\","
            "\"unique_id\":\"%s_btn_%s\","
            "\"device\":{\"identifiers\":[\"%s\"],\"name\":\"%s\",\"model\":\"ESP32-C3\"},"
            "\"command_topic\":\"home/433mhz/%s/press/%s\","
            "\"payload_press\":\"PRESS\","
            "\"availability_topic\":\"home/433mhz/%s/avail\","
            "\"payload_available\":\"online\","
            "\"payload_not_available\":\"offline\"}",
            devName, lbl, devId, lbl, devId, devName, devId, lbl, devId);
        mqtt.publish(topic, payload, true);
        Serial.printf("MQTT: button [%s %s] discovery sent\n", devName, lbl);
    }
}

void mqttPublishCoverState(const char *label) {
    if (!mqtt.connected()) return;
    char topic[80];
    snprintf(topic, sizeof(topic), "home/433mhz/%s/cover/state", devId);
    const char *state = strcmp(label, "OPEN")  == 0 ? "open"    :
                        strcmp(label, "CLOSE") == 0 ? "closed"  : "stopped";
    mqtt.publish(topic, state, true);
}

static void mqttCallback(char *topic, byte *payload, unsigned int len) {
    char msg[32] = "";
    if (len < sizeof(msg)) { memcpy(msg, payload, len); msg[len] = 0; }
    char coverTopic[80], pressPfx[80];
    snprintf(coverTopic, sizeof(coverTopic), "home/433mhz/%s/cover/cmd", devId);
    snprintf(pressPfx,   sizeof(pressPfx),   "home/433mhz/%s/press/",    devId);
    if (strcmp(topic, coverTopic) == 0) {
        if      (strcmp(msg, "OPEN")  == 0) { strncpy(req_replayLbl, "OPEN",  11); req_replay = true; }
        else if (strcmp(msg, "CLOSE") == 0) { strncpy(req_replayLbl, "CLOSE", 11); req_replay = true; }
        else if (strcmp(msg, "STOP")  == 0) {
            const char *sl = "STOP";
            for (uint8_t i = 0; i < pktCount; i++)
                if (pktMeta[i].saved && strcmp(pktMeta[i].label, "PAUSE") == 0 && pktMeta[i].bitCount >= 8) { sl = "PAUSE"; break; }
            strncpy(req_replayLbl, sl, 11);
            req_replay = true;
        }
    } else if (strncmp(topic, pressPfx, strlen(pressPfx)) == 0 && strcmp(msg, "PRESS") == 0) {
        strncpy(req_replayLbl, topic + strlen(pressPfx), sizeof(req_replayLbl) - 1);
        req_replayLbl[sizeof(req_replayLbl) - 1] = 0;
        req_replay = true;
    }
    Serial.printf("MQTT [%s] %s\n", topic, msg);
}

// ── Connect ────────────────────────────────────────────────────────
static void mqttConnect() {
    if (!mqttEnabled || WiFi.status() != WL_CONNECTED) return;
    char willTopic[64];
    snprintf(willTopic, sizeof(willTopic), "home/433mhz/%s/avail", devId);
    bool ok = mqttUser[0]
        ? mqtt.connect(devId, mqttUser, mqttPass, willTopic, 1, true, "offline")
        : mqtt.connect(devId, nullptr,  nullptr,  willTopic, 1, true, "offline");
    if (ok) {
        char sub[80];
        snprintf(sub, sizeof(sub), "home/433mhz/%s/cover/cmd", devId); mqtt.subscribe(sub);
        snprintf(sub, sizeof(sub), "home/433mhz/%s/press/+",   devId); mqtt.subscribe(sub);
        mqttPublishDiscovery();
        Serial.printf("MQTT connected as %s\n", devId);
    } else {
        Serial.printf("MQTT connect failed rc=%d\n", mqtt.state());
    }
}

// ── Public API ─────────────────────────────────────────────────────
void mqttInit() {
    loadConfig();
    if (mqttEnabled) {
        mqtt.setServer(mqttHost, mqttPort);
        mqtt.setCallback(mqttCallback);
        mqtt.setBufferSize(700);
        mqttConnect();
    }
}

void mqttApply() {
    mqttEnabled = (mqttHost[0] != 0);
    saveConfig();
    if (mqttEnabled) { mqtt.setServer(mqttHost, mqttPort); mqttLastTry = 0; }
}

void mqttTick() {
    if (mqttEnabled && WiFi.status() == WL_CONNECTED) {
        if (!mqtt.connected() && millis() - mqttLastTry > 15000) {
            mqttLastTry = millis();
            mqttConnect();
        }
        if (mqtt.connected()) mqtt.loop();
    }
    if (req_mqttDiscover) {
        req_mqttDiscover = false;
        mqttPublishDiscovery();
    }
}

bool mqttIsConnected() {
    return mqtt.connected();
}
