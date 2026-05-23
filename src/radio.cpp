#include "radio.h"
#include <SPI.h>
#include <RadioLib.h>
#include <Preferences.h>
#include "constants.h"

// Defined in mqtt_ha.cpp; set when a signal is saved/deleted so HA re-discovers.
extern volatile bool req_mqttDiscover;

// ── CC1101 register addresses ──────────────────────────────────────
static constexpr uint8_t CC_IOCFG0    = 0x02;
static constexpr uint8_t CC_PKTCTRL0  = 0x08;
static constexpr uint8_t CC_MDMCFG2   = 0x12;
static constexpr uint8_t CC_AGCCTRL2  = 0x1B;
static constexpr uint8_t CC_FREND0    = 0x22;
static constexpr uint8_t CC_PATABLE   = 0x3E;

// ── CC1101 command strobes ─────────────────────────────────────────
static constexpr uint8_t CC_SRX   = 0x34;
static constexpr uint8_t CC_STX   = 0x35;
static constexpr uint8_t CC_SIDLE = 0x36;

// ── CC1101 status register addresses ──────────────────────────────
static constexpr uint8_t CC_MARCSTATE = 0x35;  // same addr as STX strobe; context (read vs write) differs

// ── CC1101 register values ─────────────────────────────────────────
static constexpr uint8_t CC_IOCFG0_ASYNC    = 0x0D;  // async serial data on GDO0
static constexpr uint8_t CC_PKTCTRL0_ASYNC  = 0x32;  // async serial, no preamble/sync
static constexpr uint8_t CC_MDMCFG2_OOK     = 0x30;  // OOK, no sync word
static constexpr uint8_t CC_FREND0_PA1      = 0x11;  // PA_POWER=1 → uses PATABLE[1] for OOK-on
static constexpr uint8_t CC_PA_OFF          = 0x00;  // OOK off power level
static constexpr uint8_t CC_PA_PLUS10DBM    = 0xC0;  // OOK on power level (+10 dBm)
static constexpr uint8_t CC_MARC_FSTXON     = 0x13;  // MARCSTATE: frequency synthesizer on

// ── BMC timing constants ───────────────────────────────────────────
static constexpr uint32_t BMC_T_US         = 362;   // one half-period (short pulse)
static constexpr uint32_t BMC_T2_US        = 724;   // two half-periods (long pulse, encodes 0 bit)
static constexpr int      BMC_REPS         = 3;     // frame repetitions per transmission
static constexpr int      BMC_PREAMBLE_N   = 65;    // preamble edge count (first rep)
static constexpr int      BMC_SYNC_N       = 17;    // sync edge count (subsequent reps)
static constexpr uint32_t BMC_INTER_REP_US = 2850;  // inter-rep low gap
static constexpr uint32_t BMC_SOF_US       = 4900;  // start-of-frame low pulse

// MAX_PKTS labels × (11 chars + comma) + null — must fit in NVS label list
static constexpr size_t LBLS_BUF = MAX_PKTS * 13u;

// ── RadioLib objects ───────────────────────────────────────────────
static SPIClass spi(FSPI);
static CC1101   radio = new Module(CC_CS, CC_GDO0, RADIOLIB_NC, RADIOLIB_NC, spi);

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
static volatile uint16_t isrBuf[MAX_PULSE_W];
static volatile uint16_t isrN    = 0;
static volatile uint32_t isrLast = 0;
static volatile uint16_t readyBuf[MAX_PULSE_W];
static volatile uint16_t readyN   = 0;
static volatile bool     pktReady = false;
volatile uint32_t isrEdges = 0;
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
uint16_t minPktN  = 50;
uint16_t pktData[MAX_PKTS][MAX_PULSE_W];
uint16_t pktLen[MAX_PKTS];
volatile uint8_t pktCount = 0;
PktMeta          pktMeta[MAX_PKTS];
static char      scanLabel[12] = "";

// ── Shared state ───────────────────────────────────────────────────
volatile bool     capturing = false;
volatile float    lastRSSI  = -127.0f;
volatile uint32_t scanStop  = 0;

// ── CC1101 TX helpers ──────────────────────────────────────────────
// GDO0 (GPIO3) becomes the async-serial data input in TX mode.
// CC1101 synthesizer is crystal-locked to 433.92 MHz; PATABLE=0xC0 → +10 dBm.
static void cc1101WritePATable(uint8_t pa0, uint8_t pa1) {
    spi.beginTransaction(SPISettings(4000000, MSBFIRST, SPI_MODE0));
    digitalWrite(CC_CS, LOW);
    spi.transfer(CC_PATABLE | 0x40);
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
    cc1101Strobe(CC_SIDLE);          // SIDLE
    delayMicroseconds(150);      // let chip reach IDLE before writing regs
    cc1101WriteReg(CC_PKTCTRL0, CC_PKTCTRL0_ASYNC);  // PKTCTRL0: async serial
    cc1101WriteReg(CC_MDMCFG2, CC_MDMCFG2_OOK);  // MDMCFG2: OOK, no sync
    cc1101WritePATable(CC_PA_OFF, CC_PA_PLUS10DBM);
    cc1101WriteReg(CC_FREND0, CC_FREND0_PA1);  // PA_POWER=1 → OOK-on uses PATABLE[1]
    pinMode(CC_GDO0, OUTPUT);
    digitalWrite(CC_GDO0, LOW);
    cc1101Strobe(CC_STX);
    // FS_AUTOCAL=IDLE_TO_RXTX: calibration ~721µs + PLL lock ~88µs before RF is ready.
    // Poll MARCSTATE until TX (0x14) or FSTXON (0x13), timeout 3ms.
    { uint32_t t0 = micros(); uint8_t marc;
      do { marc = cc1101ReadStatus(CC_MARCSTATE) & 0x1F; } while (marc < CC_MARC_FSTXON && (micros()-t0) < 3000); }
}
static void txCC1101Stop() {
    digitalWrite(CC_GDO0, LOW);
    cc1101Strobe(CC_SIDLE);          // SIDLE
    pinMode(CC_GDO0, INPUT);
    cc1101WriteReg(CC_PKTCTRL0, CC_PKTCTRL0_ASYNC);
    cc1101WriteReg(CC_MDMCFG2, CC_MDMCFG2_OOK);
    cc1101WriteReg(CC_IOCFG0, CC_IOCFG0_ASYNC);
    cc1101WriteReg(CC_AGCCTRL2, 0x03);
}

// BMC replay: preamble + sync + BMC_REPS× data via CC1101 OOK TX
void transmitBMCBytes(const uint8_t *data, uint8_t count) {
    txCC1101Start();
    noInterrupts();
    for (int rep = 0; rep < BMC_REPS; rep++) {
        if (rep == 0) {
            for (int i = 0; i < BMC_PREAMBLE_N; i++) {
                digitalWrite(CC_GDO0, (i & 1) ? LOW : HIGH);
                delayMicroseconds(BMC_T_US);
            }
        } else {
            digitalWrite(CC_GDO0, LOW); delayMicroseconds(BMC_INTER_REP_US);
            for (int i = 0; i < BMC_SYNC_N; i++) {
                digitalWrite(CC_GDO0, (i & 1) ? LOW : HIGH);
                delayMicroseconds(BMC_T_US);
            }
        }
        digitalWrite(CC_GDO0, LOW);  delayMicroseconds(BMC_SOF_US);
        digitalWrite(CC_GDO0, HIGH); delayMicroseconds(BMC_T_US);
        bool lv = false;
        for (int b = 0; b < count * 8; b++) {
            bool bit = (data[b >> 3] >> (7 - (b & 7))) & 1;
            digitalWrite(CC_GDO0, lv ? HIGH : LOW);
            delayMicroseconds(bit ? BMC_T_US : BMC_T2_US);
            lv = !lv;
            if (bit) { digitalWrite(CC_GDO0, lv ? HIGH : LOW); delayMicroseconds(BMC_T_US); lv = !lv; }
        }
    }
    digitalWrite(CC_GDO0, LOW);
    interrupts();
    txCC1101Stop();
}

int8_t findBestBMCSlot(const char *lbl) {
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
void loadSaved() {
    Preferences prefs;
    prefs.begin("sigs", true);
    char lstBuf[LBLS_BUF] = ""; prefs.getString("lbls", lstBuf, sizeof(lstBuf));
    prefs.end();
    if (!lstBuf[0]) return;
    char tmp[LBLS_BUF]; strncpy(tmp, lstBuf, sizeof(tmp)-1); tmp[sizeof(tmp)-1] = 0;
    char *ctx, *lbl = strtok_r(tmp, ",", &ctx);
    while (lbl && pktCount < MAX_PKTS) {
        if (!lbl[0]) { lbl = strtok_r(nullptr, ",", &ctx); continue; }
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

void saveToEEPROM(uint8_t slot) {
    const char *lbl = pktMeta[slot].label;
    if (!lbl[0]) return;
    Preferences prefs;
    prefs.begin("sigs", false);
    char lstBuf[LBLS_BUF] = ""; prefs.getString("lbls", lstBuf, sizeof(lstBuf));
    bool found = false;
    char tmp[LBLS_BUF]; strncpy(tmp, lstBuf, sizeof(tmp)-1); tmp[sizeof(tmp)-1] = 0;
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

void deleteFromEEPROM(const char *lbl) {
    Preferences prefs;
    prefs.begin("sigs", false);
    char lstBuf[LBLS_BUF]=""; prefs.getString("lbls", lstBuf, sizeof(lstBuf));
    char newLst[LBLS_BUF]="";
    char tmp[LBLS_BUF]; strncpy(tmp, lstBuf, sizeof(tmp)-1); tmp[sizeof(tmp)-1] = 0;
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

// ── CC1101 configure for async OOK receive ─────────────────────────
static void cc1101ConfigRx(uint8_t agcctrl2) {
    cc1101Strobe(CC_SIDLE);
    cc1101WriteReg(CC_IOCFG0, CC_IOCFG0_ASYNC);
    cc1101WriteReg(CC_PKTCTRL0, CC_PKTCTRL0_ASYNC);
    cc1101WriteReg(CC_MDMCFG2, CC_MDMCFG2_OOK);
    cc1101WriteReg(CC_AGCCTRL2, agcctrl2);
    delay(10);
    cc1101Strobe(CC_SRX);
    delay(5);
}

void doStartScan(uint32_t ms, const char *label) {
    strncpy(scanLabel, label, sizeof(scanLabel)-1);
    scanLabel[sizeof(scanLabel)-1] = 0;
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
        cc1101ReadReg(CC_IOCFG0), cc1101ReadReg(CC_PKTCTRL0), cc1101ReadReg(CC_MDMCFG2),
        cc1101ReadReg(CC_AGCCTRL2), cc1101ReadStatus(CC_MARCSTATE) & 0x1F, (int)minPktN);

    isrLast   = micros();
    capturing = true;
    scanStop  = ms ? (millis() + ms) : 0;
    pinMode(CC_GDO0, INPUT);
    attachInterrupt(digitalPinToInterrupt(CC_GDO0), edgeISR, CHANGE);
    isrAttached = true;
    Serial.printf("Scan started (%lu ms)\n", (unsigned long)ms);
}

void doStopScan() {
    if (isrAttached) { detachInterrupt(digitalPinToInterrupt(CC_GDO0)); isrAttached = false; }
    cc1101Strobe(CC_SIDLE);
    capturing = false;
    scanStop  = 0;
    scanLabel[0] = 0;
    Serial.printf("Scan stopped. pkts=%d  edges=%lu\n", (int)pktCount, (unsigned long)isrEdges);
}

// ── Public API ─────────────────────────────────────────────────────
void radioInit() {
    pinMode(CC_CS, OUTPUT); digitalWrite(CC_CS, HIGH);
    spi.begin(CC_SCK, CC_MISO, CC_MOSI, CC_CS);
    int state = radio.begin(RF_FREQ_MHZ);
    Serial.println(state == RADIOLIB_ERR_NONE ? "CC1101 OK" : "CC1101 FAIL");
    radio.setOOK(true);
    radio.setRxBandwidth(203.0);
    radio.setBitRate(2.778);
}

float radioGetRSSI() {
    lastRSSI = radio.getRSSI();
    return lastRSSI;
}

uint16_t radioConsumePkt(uint8_t slot) {
    if (!pktReady) return 0;
    noInterrupts();
    uint16_t n = readyN;
    memcpy(pktData[slot], (const void*)readyBuf, n * 2);
    pktReady = false;
    interrupts();
    pktLen[slot] = n;
    uint16_t threshold = minPktN ? minPktN : 10;
    if (n < threshold) {
        Serial.printf("Skip noise n=%d (filter=%d)\n", n, (int)minPktN);
        return 0;
    }
    computePktMeta(slot);
    return n;
}

void radioHandlePkt() {
    uint8_t slot = (pktCount < MAX_PKTS) ? pktCount : (MAX_PKTS - 1);
    uint16_t n = radioConsumePkt(slot);
    if (n == 0) return;
    if (pktCount < MAX_PKTS) pktCount++;
    Serial.printf("Pkt %d  n=%d  T=%dµs  2T=%dµs  reps=%d  [%s]\n",
        (int)pktCount, n,
        pktMeta[slot].tShort, pktMeta[slot].tLong,
        pktMeta[slot].reps,
        pktMeta[slot].label[0] ? pktMeta[slot].label : "?");
}
