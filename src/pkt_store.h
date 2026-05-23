#pragma once
#include "constants.h"

struct PktMeta {
    uint16_t tShort, tLong;
    uint8_t  reps;
    char     label[12];
    uint8_t  bits[8];
    uint8_t  bitCount;
    uint8_t  sofCount;
    bool     saved;
};

extern volatile uint8_t pktCount;
extern PktMeta          pktMeta[MAX_PKTS];
