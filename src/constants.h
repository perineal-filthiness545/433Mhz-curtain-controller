#pragma once

// ── Hardware pins ──────────────────────────────────────────────────
#define LED_PIN   8   // WS2812B
#define CC_CS     4
#define CC_GDO0   3
#define CC_SCK    6
#define CC_MOSI   7
#define CC_MISO   5

// ── RF ────────────────────────────────────────────────────────────
#define RF_FREQ_MHZ  433.92f

// ── Packet store limits ───────────────────────────────────────────
#define MAX_PKTS     16
#define MAX_PULSE_W  1024
