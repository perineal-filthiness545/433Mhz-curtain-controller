#include <Arduino.h>
#include <SPI.h>
#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <RadioLib.h>
#include <Adafruit_NeoPixel.h>
#include "website.h"
#include <Preferences.h>
#include <PubSubClient.h>

#define LED_PIN  8   // WS2812B on ESP32-C3 Plus
#define CC_CS    4
#define CC_GDO0  3
#define CC_SCK   6
#define CC_MOSI  7
#define CC_MISO  5

Adafruit_NeoPixel led(1, LED_PIN, NEO_GRB + NEO_KHZ800);

static void ledSet(uint8_t r, uint8_t g, uint8_t b) {
    led.setPixelColor(0, led.Color(r, g, b));
    led.show();
}

SPIClass spi(FSPI);
CC1101 radio = new Module(CC_CS, CC_GDO0, RADIOLIB_NC, RADIOLIB_NC, spi);

// ── Raw SPI helpers ────────────────────────────────────────────────
// AsyncTCP (web callbacks) runs on core 0; all SPI only from loop().
static void cc1101WriteReg(uint8_t addr, uint8_t val) {
    spi.beginTransaction(SPISettings(4000000, MSBFIRST, SPI_MODE0));
    digitalWrite(CC_CS, LOW);
    spi.transfer(addr & 0x3F);
    spi.transfer(val);
    digitalWrite(CC_CS, HIGH);
    spi.endTransaction();
}
static void cc1101Strobe(uint8_t cmd) {
    spi.beginTransaction(SPISettings(4000000, MSBFIRST, SPI_MODE0));
    digitalWrite(CC_CS, LOW);
    spi.transfer(cmd);
    digitalWrite(CC_CS, HIGH);
    spi.endTransaction();
}
static uint8_t cc1101ReadReg(uint8_t addr) {
    spi.beginTransaction(SPISettings(4000000, MSBFIRST, SPI_MODE0));
    digitalWrite(CC_CS, LOW);
    spi.transfer(addr | 0x80);
    uint8_t val = spi.transfer(0);
    digitalWrite(CC_CS, HIGH);
    spi.endTransaction();
    return val;
}
static uint8_t cc1101ReadStatus(uint8_t addr) {
    spi.beginTransaction(SPISettings(4000000, MSBFIRST, SPI_MODE0));
    digitalWrite(CC_CS, LOW);
    spi.transfer(addr | 0xC0);
    uint8_t val = spi.transfer(0);
    digitalWrite(CC_CS, HIGH);
    spi.endTransaction();
    return val;
}

// ── ISR pulse capture ──────────────────────────────────────────────
#define MAX_PULSE_W 1024
static volatile uint16_t isrBuf[MAX_PULSE_W];
static volatile uint16_t isrN    = 0;
static volatile uint32_t isrLast = 0;
static volatile uint16_t readyBuf[MAX_PULSE_W];
static volatile uint16_t readyN   = 0;
static volatile bool     pktReady = false;
static volatile uint32_t isrEdges = 0;
static bool isrAttached = false;

void IRAM_ATTR edgeISR() {
    isrEdges++;
    uint32_t now = micros();
    uint32_t dur = now - isrLast;
    isrLast = now;
    if (dur < 50) return;
    if (dur > 8000) {
        if (isrN >= 10 && !pktReady) {
            memcpy((void*)readyBuf, (void*)isrBuf, isrN * 2);
            readyN   = isrN;
            pktReady = true;
        }
        isrN = 0;
        return;
    }
    if (isrN < MAX_PULSE_W) isrBuf[isrN++] = (uint16_t)dur;
}

// ── Packet store ───────────────────────────────────────────────────
#define MAX_PKTS  16
static uint16_t  minPktN  = 50;   // adjustable via /setfilter?n=N  (0 = accept all ≥10)
static uint16_t pktData[MAX_PKTS][MAX_PULSE_W];
static uint16_t pktLen[MAX_PKTS];
static volatile uint8_t pktCount = 0;

struct PktMeta {
    uint16_t tShort, tLong;
    uint8_t  reps;
    char     label[12];
    uint8_t  bits[8];
    uint8_t  bitCount;
    uint8_t  sofCount;
    bool     saved;
};
static PktMeta pktMeta[MAX_PKTS];
static char    scanLabel[12] = "";
static Preferences prefs;

// ── MQTT / Home Assistant ──────────────────────────────────────────
static WiFiClient   wifiClient;
static PubSubClient mqtt(wifiClient);
static char         mqttHost[64]  = "";
static uint16_t     mqttPort      = 1883;
static char         mqttUser[32]  = "";
static char         mqttPass[32]  = "";
static bool         mqttEnabled   = false;
static uint32_t     mqttLastTry   = 0;
static char         devId[20]     = "";
static char         devName[48]   = "Garage Door";
static volatile bool req_mqttDiscover = false;

// ── Shared state ───────────────────────────────────────────────────
static volatile bool  capturing  = false;
static volatile float lastRSSI   = -127.0f;
static volatile uint32_t scanStop = 0;

// ── Flags set by web callbacks, consumed by loop() ─────────────────
static volatile bool     req_scan      = false;
static volatile uint32_t req_scanMs    = 0;
static volatile bool     req_stop      = false;
static char              req_capLabel[12] = "";
static volatile bool     req_replay    = false;
static char              req_replayLbl[12] = "";
static volatile float    req_txFreqMHz = 0;  // 0 = use current (433.92); set to override TX freq

// ── CC1101 TX helpers ──────────────────────────────────────────────
// GDO0 (GPIO3) becomes the async-serial data input in TX mode.
// CC1101 synthesizer is crystal-locked to 433.92 MHz; PATABLE=0xC0 → +10 dBm.
static void cc1101WritePATable(uint8_t pa0, uint8_t pa1) {
    spi.beginTransaction(SPISettings(4000000, MSBFIRST, SPI_MODE0));
    digitalWrite(CC_CS, LOW);
    spi.transfer(0x7E);   // burst write to PATABLE (0x3E | 0x40)
    spi.transfer(pa0);    // PATABLE[0]: OOK-off power (0x00 = min)
    spi.transfer(pa1);    // PATABLE[1]: OOK-on power  (0xC0 = +10 dBm)
    digitalWrite(CC_CS, HIGH);
    spi.endTransaction();
}
static void txCC1101Start() {
    if (isrAttached) {
        detachInterrupt(digitalPinToInterrupt(CC_GDO0));
        isrAttached = false;
    }
    // Optional frequency override (set by web handler before TX)
    float freqMHz = req_txFreqMHz;
    req_txFreqMHz = 0;
    if (freqMHz > 400 && freqMHz < 500) {
        // Write FREQ2/FREQ1/FREQ0 directly: FREQ = freqMHz / 26 * 65536
        uint32_t fr = (uint32_t)(freqMHz / 26.0f * 65536.0f);
        cc1101WriteReg(0x0D, (fr >> 16) & 0xFF);
        cc1101WriteReg(0x0E, (fr >>  8) & 0xFF);
        cc1101WriteReg(0x0F,  fr        & 0xFF);
        Serial.printf("TX freq=%.3f MHz  FREQ=0x%06lX\n", freqMHz, (unsigned long)fr);
    }
    cc1101Strobe(0x36);          // SIDLE
    delayMicroseconds(150);      // let chip reach IDLE before writing regs
    cc1101WriteReg(0x08, 0x32); // PKTCTRL0: async serial
    cc1101WriteReg(0x12, 0x30); // MDMCFG2: OOK, no sync
    cc1101WritePATable(0x00, 0xC0); // PATABLE: off=0x00, on=0xC0(+10dBm)
    cc1101WriteReg(0x22, 0x11);    // FREND0: PA_POWER=1 → OOK-on uses PATABLE[1]
    pinMode(CC_GDO0, OUTPUT);
    digitalWrite(CC_GDO0, LOW);
    cc1101Strobe(0x35);          // STX
    // FS_AUTOCAL=IDLE_TO_RXTX: calibration ~721µs + PLL lock ~88µs before RF is ready.
    // Poll MARCSTATE until TX (0x14) or FSTXON (0x13), timeout 3ms.
    { uint32_t t0 = micros(); uint8_t marc;
      do { marc = cc1101ReadStatus(0x35) & 0x1F; } while (marc < 0x13 && (micros()-t0) < 3000); }
}
static void txCC1101Stop() {
    digitalWrite(CC_GDO0, LOW);
    cc1101Strobe(0x36);          // SIDLE
    pinMode(CC_GDO0, INPUT);
    cc1101WriteReg(0x08, 0x32);
    cc1101WriteReg(0x12, 0x30);
    cc1101WriteReg(0x02, 0x0D);
    cc1101WriteReg(0x1B, 0x03);
}

// BMC replay: preamble + sync + 3× data via CC1101 OOK TX
static void transmitBMCBytes(const uint8_t *data, uint8_t count) {
    txCC1101Start();
    const uint32_t T = 362, T2 = 724;
    noInterrupts();
    for (int rep = 0; rep < 3; rep++) {
        if (rep == 0) {
            for (int i = 0; i < 65; i++) {
                digitalWrite(CC_GDO0, (i & 1) ? LOW : HIGH);
                delayMicroseconds(T);
            }
        } else {
            digitalWrite(CC_GDO0, LOW); delayMicroseconds(2850);
            for (int i = 0; i < 17; i++) {
                digitalWrite(CC_GDO0, (i & 1) ? LOW : HIGH);
                delayMicroseconds(T);
            }
        }
        digitalWrite(CC_GDO0, LOW);  delayMicroseconds(4900);
        digitalWrite(CC_GDO0, HIGH); delayMicroseconds(T);
        bool lv = false;
        for (int b = 0; b < count * 8; b++) {
            bool bit = (data[b >> 3] >> (7 - (b & 7))) & 1;
            digitalWrite(CC_GDO0, lv ? HIGH : LOW);
            delayMicroseconds(bit ? T : T2);
            lv = !lv;
            if (bit) { digitalWrite(CC_GDO0, lv ? HIGH : LOW); delayMicroseconds(T); lv = !lv; }
        }
    }
    digitalWrite(CC_GDO0, LOW);
    interrupts();
    txCC1101Stop();
}

static int8_t findBestRawSlot(const char *lbl) {
    int8_t best = -1;
    for (uint8_t i = 0; i < pktCount; i++) {
        if (strncmp(pktMeta[i].label, lbl, 11) == 0 && pktLen[i] >= 10) {
            if (best < 0 || pktLen[i] > pktLen[(uint8_t)best])
                best = (int8_t)i;
        }
    }
    return best;
}
static int8_t findBestBMCSlot(const char *lbl) {
    int8_t best = -1;
    for (uint8_t i = 0; i < pktCount; i++) {
        if (strncmp(pktMeta[i].label, lbl, 11) == 0 && pktMeta[i].bitCount >= 8) {
            if (best < 0 || pktMeta[i].bitCount > pktMeta[(uint8_t)best].bitCount)
                best = (int8_t)i;
        }
    }
    return best;
}

static void decodeBMC(uint8_t slot);

// ── Per-packet metadata ────────────────────────────────────────────
static void computePktMeta(uint8_t slot) {
    uint16_t n = pktLen[slot];
    uint16_t *p = pktData[slot];
    PktMeta &m = pktMeta[slot];
    memset(&m, 0, sizeof(PktMeta));
    strncpy(m.label, scanLabel, sizeof(m.label)-1);

    for (uint16_t i = 0; i < n; i++)
        if (p[i] > 3500 && p[i] < 8000) m.reps++;

    uint32_t sum1 = 0; int n1 = 0;
    for (uint16_t i = 0; i < n; i++)
        if (p[i] > 50 && p[i] < 600) { sum1 += p[i]; n1++; }
    if (n1 < 4) return;
    uint16_t tEst  = (uint16_t)(sum1 / n1);
    uint16_t thresh = tEst + tEst / 2;

    uint32_t sumS = 0, sumL = 0; int nS = 0, nL = 0;
    for (uint16_t i = 0; i < n; i++) {
        if      (p[i] < thresh) { sumS += p[i]; nS++; }
        else if (p[i] < 2000)  { sumL += p[i]; nL++; }
    }
    if (nS) m.tShort = (uint16_t)(sumS / nS);
    if (nL) m.tLong  = (uint16_t)(sumL / nL);
    decodeBMC(slot);
}

// ── BMC decoder ────────────────────────────────────────────────────
static void decodeBMC(uint8_t slot) {
    uint16_t  n  = pktLen[slot];
    uint16_t *p  = pktData[slot];
    PktMeta  &m  = pktMeta[slot];
    uint16_t  ts = m.tShort;
    if (!ts || n < 20) return;

    uint16_t thr = ts + ts / 2;

    uint16_t i = 0;
    while (i < n && p[i] < 3500) i++;
    if (i >= n) return;
    i++;

    m.sofCount = 0;
    for (uint16_t j = i; j < n && p[j] < thr && p[j] < 2000; j++) m.sofCount++;

    memset(m.bits, 0, sizeof(m.bits));
    m.bitCount = 0;

    while (i < n && m.bitCount < 64) {
        uint16_t v = p[i];
        if (v > 2000) break;
        if (v >= thr) {
            m.bitCount++;
            i++;
        } else {
            if (i + 1 < n && p[i+1] < thr && p[i+1] < 2000) {
                m.bits[m.bitCount >> 3] |= (1u << (7 - (m.bitCount & 7)));
                m.bitCount++;
                i += 2;
            } else {
                i++;
            }
        }
    }

    Serial.printf("  BMC sof=%d bits=%d  hex=", m.sofCount, m.bitCount);
    for (int b = 0; b < (m.bitCount + 7) / 8; b++) Serial.printf("%02X", m.bits[b]);
    Serial.println();
}

// ── EEPROM (NVS) persistence ───────────────────────────────────────
// Namespace "sigs"; "lbls" = comma-separated label list;
// "n_LABEL" = uint16 pulse count; "d_LABEL" = pulse data blob.
static void loadSaved() {
    prefs.begin("sigs", true);
    char lstBuf[128] = ""; prefs.getString("lbls", lstBuf, sizeof(lstBuf));
    prefs.end();
    if (!lstBuf[0]) return;
    char tmp[128]; strncpy(tmp, lstBuf, 127); tmp[127] = 0;
    char *ctx, *lbl = strtok_r(tmp, ",", &ctx);
    while (lbl && pktCount < MAX_PKTS) {
        char nKey[16], dKey[16];
        snprintf(nKey, sizeof(nKey), "n_%.11s", lbl);
        snprintf(dKey, sizeof(dKey), "d_%.11s", lbl);
        prefs.begin("sigs", true);
        uint16_t n = prefs.getUShort(nKey, 0);
        uint8_t  slot = pktCount;
        size_t   got  = (n && n <= MAX_PULSE_W) ? prefs.getBytes(dKey, pktData[slot], n*2) : 0;
        prefs.end();
        if (got == n*2 && n) {
            pktLen[slot] = n;
            strncpy(scanLabel, lbl, sizeof(scanLabel)-1); scanLabel[sizeof(scanLabel)-1] = 0;
            computePktMeta(slot);
            scanLabel[0] = 0;
            pktMeta[slot].saved = true;
            pktCount++;
            Serial.printf("Loaded [%s] n=%d\n", lbl, n);
        }
        lbl = strtok_r(nullptr, ",", &ctx);
    }
}

static void saveToEEPROM(uint8_t slot) {
    const char *lbl = pktMeta[slot].label;
    if (!lbl[0]) return;
    prefs.begin("sigs", false);
    char lstBuf[128] = ""; prefs.getString("lbls", lstBuf, sizeof(lstBuf));
    bool found = false;
    char tmp[128]; strncpy(tmp, lstBuf, 127); tmp[127] = 0;
    char *ctx, *t = strtok_r(tmp, ",", &ctx);
    while (t) { if (strcmp(t, lbl)==0){found=true;break;} t=strtok_r(nullptr,",",&ctx); }
    if (!found) {
        if (lstBuf[0]) strncat(lstBuf, ",", sizeof(lstBuf)-strlen(lstBuf)-1);
        strncat(lstBuf, lbl, sizeof(lstBuf)-strlen(lstBuf)-1);
        prefs.putString("lbls", lstBuf);
    }
    char nKey[16], dKey[16];
    snprintf(nKey, sizeof(nKey), "n_%.11s", lbl);
    snprintf(dKey, sizeof(dKey), "d_%.11s", lbl);
    prefs.putUShort(nKey, pktLen[slot]);
    prefs.putBytes(dKey, pktData[slot], pktLen[slot]*2);
    prefs.end();
    for (uint8_t i = 0; i < pktCount; i++)
        if (strncmp(pktMeta[i].label, lbl, 11)==0) pktMeta[i].saved = true;
    Serial.printf("Saved [%s] n=%d\n", lbl, pktLen[slot]);
    req_mqttDiscover = true;
}

static void deleteFromEEPROM(const char *lbl) {
    prefs.begin("sigs", false);
    char lstBuf[128]=""; prefs.getString("lbls", lstBuf, sizeof(lstBuf));
    char newLst[128]="";
    char tmp[128]; strncpy(tmp, lstBuf, 127); tmp[127] = 0;
    char *ctx, *t = strtok_r(tmp, ",", &ctx);
    while (t) {
        if (strcmp(t, lbl)!=0) {
            if (newLst[0]) strncat(newLst, ",", sizeof(newLst)-strlen(newLst)-1);
            strncat(newLst, t, sizeof(newLst)-strlen(newLst)-1);
        }
        t = strtok_r(nullptr, ",", &ctx);
    }
    prefs.putString("lbls", newLst);
    char nKey[16], dKey[16];
    snprintf(nKey, sizeof(nKey), "n_%.11s", lbl);
    snprintf(dKey, sizeof(dKey), "d_%.11s", lbl);
    prefs.remove(nKey); prefs.remove(dKey);
    prefs.end();
    for (uint8_t i = 0; i < pktCount; i++)
        if (strncmp(pktMeta[i].label, lbl, 11)==0) pktMeta[i].saved = false;
    Serial.printf("Deleted [%s] from EEPROM\n", lbl);
    req_mqttDiscover = true;
}

// ── MQTT helpers ───────────────────────────────────────────────────
static void mqttLoadConfig() {
    prefs.begin("mqtt", true);
    prefs.getString("host", mqttHost, sizeof(mqttHost));
    mqttPort = prefs.getUShort("port", 1883);
    prefs.getString("user", mqttUser, sizeof(mqttUser));
    prefs.getString("pass", mqttPass, sizeof(mqttPass));
    prefs.getString("name", devName, sizeof(devName));
    prefs.end();
    if (!devName[0]) strncpy(devName, "Garage Door", sizeof(devName)-1);
    mqttEnabled = (mqttHost[0] != 0);
}
static void mqttSaveConfig() {
    prefs.begin("mqtt", false);
    prefs.putString("host", mqttHost);
    prefs.putUShort("port", mqttPort);
    prefs.putString("user", mqttUser);
    prefs.putString("pass", mqttPass);
    prefs.putString("name", devName);
    prefs.end();
}

static void mqttPublishDiscovery() {
    if (!mqtt.connected()) return;
    char topic[128], payload[600];
    snprintf(topic, sizeof(topic), "home/433mhz/%s/avail", devId);
    mqtt.publish(topic, "online", true);

    // Cover entity — only when OPEN + CLOSE are both saved with BMC data
    bool hasOpen=false, hasClose=false, hasPause=false;
    for (uint8_t i=0; i<pktCount; i++) {
        if (!pktMeta[i].saved || pktMeta[i].bitCount<8) continue;
        if (strcmp(pktMeta[i].label,"OPEN") ==0) hasOpen=true;
        if (strcmp(pktMeta[i].label,"CLOSE")==0) hasClose=true;
        if (strcmp(pktMeta[i].label,"PAUSE")==0||strcmp(pktMeta[i].label,"STOP")==0) hasPause=true;
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
            devName,devId,devId,devName,devId,devId,
            hasPause?"\"payload_stop\":\"STOP\",":"",
            devId);
        mqtt.publish(topic, payload, true);
        Serial.printf("MQTT: cover discovery sent as [%s]\n", devName);
    }

    // Button entity for each unique saved label with BMC data
    for (uint8_t i=0; i<pktCount; i++) {
        if (!pktMeta[i].saved || pktMeta[i].bitCount<8) continue;
        bool dup=false;
        for (uint8_t j=0;j<i;j++)
            if (pktMeta[j].saved && strcmp(pktMeta[j].label,pktMeta[i].label)==0){dup=true;break;}
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
            devName,lbl,devId,lbl,devId,devName,devId,lbl,devId);
        mqtt.publish(topic, payload, true);
        Serial.printf("MQTT: button [%s %s] discovery sent\n", devName, lbl);
    }
}

static void mqttCallback(char *topic, byte *payload, unsigned int len) {
    char msg[32]="";
    if (len<sizeof(msg)){memcpy(msg,payload,len);msg[len]=0;}
    char coverTopic[80], pressPfx[80];
    snprintf(coverTopic, sizeof(coverTopic), "home/433mhz/%s/cover/cmd", devId);
    snprintf(pressPfx,   sizeof(pressPfx),   "home/433mhz/%s/press/",    devId);
    if (strcmp(topic, coverTopic)==0) {
        if      (strcmp(msg,"OPEN") ==0){strncpy(req_replayLbl,"OPEN", 11);req_replay=true;}
        else if (strcmp(msg,"CLOSE")==0){strncpy(req_replayLbl,"CLOSE",11);req_replay=true;}
        else if (strcmp(msg,"STOP") ==0){
            const char *sl="STOP";
            for (uint8_t i=0;i<pktCount;i++)
                if(pktMeta[i].saved&&strcmp(pktMeta[i].label,"PAUSE")==0&&pktMeta[i].bitCount>=8){sl="PAUSE";break;}
            strncpy(req_replayLbl,sl,11);req_replay=true;
        }
    } else if (strncmp(topic,pressPfx,strlen(pressPfx))==0 && strcmp(msg,"PRESS")==0) {
        strncpy(req_replayLbl, topic+strlen(pressPfx), sizeof(req_replayLbl)-1);
        req_replayLbl[sizeof(req_replayLbl)-1]=0;
        req_replay=true;
    }
    Serial.printf("MQTT [%s] %s\n", topic, msg);
}

static void mqttConnect() {
    if (!mqttEnabled || WiFi.status()!=WL_CONNECTED) return;
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

// ── CC1101 configure for async OOK receive ─────────────────────────
static void cc1101ConfigRx(uint8_t agcctrl2) {
    cc1101Strobe(0x36);
    cc1101WriteReg(0x02, 0x0D);
    cc1101WriteReg(0x08, 0x32);
    cc1101WriteReg(0x12, 0x30);
    cc1101WriteReg(0x1B, agcctrl2);
    delay(10);
    cc1101Strobe(0x34);
    delay(5);
}

static void doStartScan(uint32_t ms) {
    if (isrAttached) { detachInterrupt(digitalPinToInterrupt(CC_GDO0)); isrAttached = false; }
    // Preserve saved captures; compact them to front, drop unsaved
    { uint8_t k = 0;
      for (uint8_t i = 0; i < pktCount; i++) {
          if (pktMeta[i].saved) {
              if (i != k) { memcpy(pktData[k], pktData[i], pktLen[i]*2); pktLen[k]=pktLen[i]; pktMeta[k]=pktMeta[i]; }
              k++;
          }
      }
      pktCount = k; }
    isrN      = 0;
    isrEdges  = 0;
    pktReady  = false;
    cc1101ConfigRx(0x03);

    Serial.printf("IOCFG0=0x%02X  PKTCTRL0=0x%02X  MDMCFG2=0x%02X  AGCCTRL2=0x%02X  MARC=0x%02X  filter=%d\n",
        cc1101ReadReg(0x02), cc1101ReadReg(0x08), cc1101ReadReg(0x12),
        cc1101ReadReg(0x1B), cc1101ReadStatus(0x35) & 0x1F, (int)minPktN);

    isrLast   = micros();
    capturing = true;
    scanStop  = ms ? (millis() + ms) : 0;
    pinMode(CC_GDO0, INPUT);
    attachInterrupt(digitalPinToInterrupt(CC_GDO0), edgeISR, CHANGE);
    isrAttached = true;
    Serial.printf("Scan started (%lu ms)\n", (unsigned long)ms);
}

static void doStopScan() {
    if (isrAttached) { detachInterrupt(digitalPinToInterrupt(CC_GDO0)); isrAttached = false; }
    cc1101Strobe(0x36);
    capturing = false;
    scanStop  = 0;
    scanLabel[0] = 0;
    Serial.printf("Scan stopped. pkts=%d  edges=%lu\n", (int)pktCount, (unsigned long)isrEdges);
}

// ── Web server ─────────────────────────────────────────────────────
AsyncWebServer server(80);

#include "credentials.h"

void setup() {
    Serial.begin(115200);
    delay(2000);

    led.begin(); ledSet(0, 0, 0);
    pinMode(CC_CS, OUTPUT); digitalWrite(CC_CS, HIGH);

    spi.begin(CC_SCK, CC_MISO, CC_MOSI, CC_CS);

    int state = radio.begin(433.92);
    Serial.println(state == RADIOLIB_ERR_NONE ? "CC1101 OK" : "CC1101 FAIL");
    radio.setOOK(true);
    radio.setRxBandwidth(203.0);
    radio.setBitRate(2.778);

    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    Serial.print("WiFi");
    unsigned long t = millis();
    while (WiFi.status() != WL_CONNECTED && millis()-t < 10000) { delay(400); Serial.print("."); }
    Serial.println();
    Serial.print("IP: "); Serial.println(WiFi.localIP());
    { uint8_t mac[6]; WiFi.macAddress(mac);
      snprintf(devId, sizeof(devId), "gdo_%02X%02X%02X", mac[3], mac[4], mac[5]); }
    Serial.printf("Device ID: %s\n", devId);

    server.on("/", HTTP_GET, [](AsyncWebServerRequest *r) {
        r->send(200, "text/html", index_html);
    });

    server.on("/capture", HTTP_GET, [](AsyncWebServerRequest *r) {
        if (r->hasParam("btn")) {
            String v = r->getParam("btn")->value();
            strncpy(req_capLabel, v.c_str(), sizeof(req_capLabel)-1);
            req_capLabel[sizeof(req_capLabel)-1] = 0;
        } else {
            req_capLabel[0] = 0;
        }
        req_scanMs = r->hasParam("t") ? (uint32_t)r->getParam("t")->value().toInt() : 5000;
        req_scan   = true;
        r->send(200, "text/plain", "ok");
    });

    server.on("/stop", HTTP_GET, [](AsyncWebServerRequest *r) {
        req_stop = true;
        r->send(200, "text/plain", "ok");
    });

    server.on("/status", HTTP_GET, [](AsyncWebServerRequest *r) {
        uint32_t rem = 0;
        if (capturing && scanStop) {
            uint32_t now = millis();
            rem = (now < scanStop) ? (scanStop - now) : 0;
        }
        char buf[160];
        snprintf(buf, sizeof(buf),
            "{\"capturing\":%s,\"count\":%d,\"rssi\":%.1f,\"remaining\":%lu,\"edges\":%lu,\"filter\":%d}",
            capturing ? "true" : "false", (int)pktCount, (float)lastRSSI,
            (unsigned long)rem, (unsigned long)isrEdges, (int)minPktN);
        r->send(200, "application/json", buf);
    });

    server.on("/packets", HTTP_GET, [](AsyncWebServerRequest *r) {
        String s = "[";
        uint8_t n = pktCount;
        for (int i = 0; i < n; i++) {
            if (i) s += ",";
            String bmc = "";
            for (int j = 0; j < (pktMeta[i].bitCount + 7) / 8 && j < 8; j++) {
                char hb[3]; snprintf(hb, 3, "%02X", pktMeta[i].bits[j]);
                bmc += hb;
            }
            s += "{\"n\":"    + String(pktLen[i])
              + ",\"ts\":"    + String(pktMeta[i].tShort)
              + ",\"tl\":"    + String(pktMeta[i].tLong)
              + ",\"reps\":"  + String(pktMeta[i].reps)
              + ",\"sof\":"   + String(pktMeta[i].sofCount)
              + ",\"bn\":"    + String(pktMeta[i].bitCount)
              + ",\"bmc\":\"" + bmc + "\""
              + ",\"lbl\":\""  + String(pktMeta[i].label) + "\""
              + ",\"saved\":"  + (pktMeta[i].saved ? "true" : "false")
              + ",\"p\":[";
            for (int j = 0; j < pktLen[i]; j++) {
                if (j) s += ",";
                s += String(pktData[i][j]);
            }
            s += "]}";
        }
        r->send(200, "application/json", s + "]");
    });

    // Set minimum pulse count to accept as a packet (0 = accept all ≥10)
    server.on("/setfilter", HTTP_GET, [](AsyncWebServerRequest *r) {
        if (r->hasParam("n")) minPktN = (uint16_t)r->getParam("n")->value().toInt();
        char buf[32]; snprintf(buf, sizeof(buf), "filter=%d", (int)minPktN);
        r->send(200, "text/plain", buf);
    });

    server.on("/replay", HTTP_GET, [](AsyncWebServerRequest *r) {
        if (!r->hasParam("lbl")) { r->send(400, "text/plain", "missing lbl"); return; }
        String v = r->getParam("lbl")->value();
        strncpy(req_replayLbl, v.c_str(), sizeof(req_replayLbl)-1);
        req_replayLbl[sizeof(req_replayLbl)-1] = 0;
        req_replay = true;
        r->send(200, "text/plain", "queued");
    });

    server.on("/savesig", HTTP_GET, [](AsyncWebServerRequest *r) {
        if (!r->hasParam("lbl")) { r->send(400, "text/plain", "missing lbl"); return; }
        String lbl = r->getParam("lbl")->value();
        int8_t slot = findBestRawSlot(lbl.c_str());
        if (slot < 0) { r->send(404, "text/plain", "no capture"); return; }
        saveToEEPROM((uint8_t)slot);
        r->send(200, "text/plain", "saved");
    });

    server.on("/delsig", HTTP_GET, [](AsyncWebServerRequest *r) {
        if (!r->hasParam("lbl")) { r->send(400, "text/plain", "missing lbl"); return; }
        String lbl = r->getParam("lbl")->value();
        deleteFromEEPROM(lbl.c_str());
        r->send(200, "text/plain", "deleted");
    });

    server.on("/mqttcfg", HTTP_GET, [](AsyncWebServerRequest *r) {
        char buf[256];
        snprintf(buf, sizeof(buf),
            "{\"host\":\"%s\",\"port\":%d,\"user\":\"%s\",\"connected\":%s,\"devId\":\"%s\",\"name\":\"%s\"}",
            mqttHost, (int)mqttPort, mqttUser,
            mqtt.connected() ? "true" : "false", devId, devName);
        r->send(200, "application/json", buf);
    });

    server.on("/mqttcfg", HTTP_POST, [](AsyncWebServerRequest *r) {
        if (r->hasParam("host", true)) strncpy(mqttHost, r->getParam("host",true)->value().c_str(), sizeof(mqttHost)-1);
        if (r->hasParam("port", true)) mqttPort = (uint16_t)r->getParam("port",true)->value().toInt();
        if (r->hasParam("user", true)) strncpy(mqttUser, r->getParam("user",true)->value().c_str(), sizeof(mqttUser)-1);
        if (r->hasParam("pass", true)) strncpy(mqttPass, r->getParam("pass",true)->value().c_str(), sizeof(mqttPass)-1);
        if (r->hasParam("name", true)) {
            strncpy(devName, r->getParam("name",true)->value().c_str(), sizeof(devName)-1);
            if (!devName[0]) strncpy(devName, "Garage Door", sizeof(devName)-1);
        }
        mqttEnabled = (mqttHost[0] != 0);
        mqttSaveConfig();
        if (mqttEnabled) { mqtt.setServer(mqttHost, mqttPort); mqttLastTry = 0; }
        r->send(200, "text/plain", "ok");
    });

    server.on("/mqttdiscover", HTTP_GET, [](AsyncWebServerRequest *r) {
        mqttPublishDiscovery();
        r->send(200, "text/plain", mqtt.connected() ? "published" : "not connected");
    });

    loadSaved();

    mqttLoadConfig();
    if (mqttEnabled) {
        mqtt.setServer(mqttHost, mqttPort);
        mqtt.setCallback(mqttCallback);
        mqtt.setBufferSize(700);
        mqttConnect();
    }

    server.begin();
    Serial.println("Ready");
}

void loop() {
    if (req_scan) {
        req_scan = false;
        strncpy(scanLabel, req_capLabel, sizeof(scanLabel)-1);
        req_capLabel[0] = 0;
        doStartScan(req_scanMs);
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
        lastRSSI   = radio.getRSSI();
    }

    static uint32_t lastDiagMs = 0;
    if (capturing && millis() - lastDiagMs > 500) {
        lastDiagMs = millis();
        Serial.printf("GDO0=%d  RSSI=%.1f  edges=%lu  pkts=%d\n",
            digitalRead(CC_GDO0), (float)lastRSSI,
            (unsigned long)isrEdges, (int)pktCount);
    }

    // ── Store completed packet ────────────────────────────────────────
    if (pktReady) {
        noInterrupts();
        uint16_t n = readyN;
        uint16_t tmp[MAX_PULSE_W];
        memcpy(tmp, (const void*)readyBuf, n * 2);
        pktReady = false;
        interrupts();

        uint16_t threshold = minPktN ? minPktN : 10;
        if (n < threshold) {
            Serial.printf("Skip noise n=%d (filter=%d)\n", n, (int)minPktN);
        } else {
            uint8_t slot = (pktCount < MAX_PKTS) ? pktCount : (MAX_PKTS - 1);
            memcpy(pktData[slot], tmp, n * 2);
            pktLen[slot] = n;
            computePktMeta(slot);
            if (pktCount < MAX_PKTS) pktCount++;
            Serial.printf("Pkt %d  n=%d  T=%dµs  2T=%dµs  reps=%d  [%s]\n",
                (int)pktCount, n,
                pktMeta[slot].tShort, pktMeta[slot].tLong,
                pktMeta[slot].reps,
                pktMeta[slot].label[0] ? pktMeta[slot].label : "?");
        }
    }

    // ── BMC replay via CC1101 ─────────────────────────────────────────
    if (req_replay) {
        req_replay = false;
        int8_t slot = findBestBMCSlot(req_replayLbl);
        if (slot >= 0) {
            uint8_t nb = (pktMeta[slot].bitCount + 7) / 8;
            ledSet(0, 60, 60);   // cyan = CC1101 BMC TX
            transmitBMCBytes(pktMeta[slot].bits, nb);
            ledSet(0, 0, 0);
            Serial.printf("BMC TX [%s] %d bytes\n", req_replayLbl, nb);
            // Publish cover state to MQTT
            if (mqtt.connected()) {
                char st[80];
                snprintf(st, sizeof(st), "home/433mhz/%s/cover/state", devId);
                const char *stVal = strcmp(req_replayLbl,"OPEN")==0  ? "open"    :
                                    strcmp(req_replayLbl,"CLOSE")==0 ? "closed"  : "stopped";
                mqtt.publish(st, stVal, true);
            }
        } else {
            Serial.printf("BMC TX [%s]: no capture\n", req_replayLbl);
        }
    }

    // ── MQTT ──────────────────────────────────────────────────────────
    if (mqttEnabled && WiFi.status()==WL_CONNECTED) {
        if (!mqtt.connected() && millis()-mqttLastTry > 15000) {
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
