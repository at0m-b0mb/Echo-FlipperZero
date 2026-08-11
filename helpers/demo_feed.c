#include "demo_feed.h"

#include <string.h>

/*
 * The cast.
 *
 * 0  An older Android that never learned to randomize. Real MAC, and the whole
 *    saved-network list on show: home, a hotel, a gym, an airport, a hospital.
 *    This is the one people put their phone down after seeing.
 * 1  A current iPhone. Randomized address, broadcast probes only, never names
 *    a network. Echo has to be able to say "nothing here", and this is why.
 * 2  A phone that randomizes its address but keeps counting frames straight
 *    through the change. Three addresses, one radio, and Echo can prove it.
 * 3  A work laptop. Real MAC, and the two names that matter most: the office
 *    and the house.
 * 4  Another handset passing through, mid-leak.
 */
typedef struct {
    uint8_t mac[6];
    int8_t rssi;
    uint32_t fp;
} DemoPersona;

static const DemoPersona PERSONAS[DEMO_PERSONAS] = {
    {{0x34, 0x23, 0xBA, 0x7C, 0x19, 0x04}, -47, 0xA1B2C3D4u}, /* old Android  */
    {{0x8E, 0x41, 0xC2, 0x19, 0x7A, 0x03}, -58, 0x51F0AA21u}, /* modern iPhone */
    {{0x8A, 0x3F, 0x11, 0x02, 0x9C, 0x41}, -66, 0x77C10E55u}, /* the rotator  */
    {{0x7C, 0xB2, 0x7D, 0x44, 0x12, 0x9A}, -72, 0x1234ABCDu}, /* work laptop  */
    {{0xC0, 0xBD, 0xD1, 0x0A, 0x5E, 0x22}, -80, 0x0BADF00Du}, /* passer-by    */
};

/* The three addresses persona 2 cycles through. Same radio every time. */
static const uint8_t ROTATION[3][6] = {
    {0x8A, 0x3F, 0x11, 0x02, 0x9C, 0x41},
    {0xB6, 0x7C, 0xD9, 0x41, 0x22, 0x08},
    {0xEA, 0x05, 0x63, 0x9F, 0x11, 0xB2},
};

typedef struct {
    uint16_t gap_ms; /* since the previous step */
    uint8_t persona;
    const char* ssid; /* "" means a broadcast probe: no name given away */
} DemoStep;

static const DemoStep SCRIPT[] = {
    {300, 1, ""},
    {450, 0, "NETGEAR58"},
    {350, 2, ""},
    {500, 0, ""},
    {600, 3, "ACME-Corp"},
    {400, 1, ""},
    {550, 0, "Hilton_Honors"},
    {450, 4, ""},
    {500, 2, ""},
    {700, 0, "PlanetFitness"},
    {400, 3, ""},
    {600, 1, ""},
    {500, 0, "NETGEAR58"},
    {650, 4, "Marriott_GUEST"},
    {450, 2, ""},
    {600, 3, "Sharma_Home_5G"},
    {500, 0, "LAX-Free-WiFi"},
    {400, 1, ""},
    {700, 2, ""},
    {550, 0, "Starbucks WiFi"},
    {600, 4, "eduroam"},
    {450, 3, "ACME-Corp"},
    {500, 1, ""},
    {800, 0, "CityHospital-Guest"},
    {400, 2, ""},
    {600, 0, "NETGEAR58"},
    {500, 4, ""},
    {650, 1, ""},
    {550, 3, ""},
    {700, 2, ""},
    {600, 0, "Hilton_Honors"},
    {900, 1, ""},
};

#define SCRIPT_LEN (sizeof(SCRIPT) / sizeof(SCRIPT[0]))

/* Channels the sniffer would have been sitting on. Cosmetic, but a feed where
 * every row says the same channel looks fake. */
static const uint8_t CHANNELS[] = {1, 6, 11, 6, 1, 11};
#define CHANNEL_N (sizeof(CHANNELS) / sizeof(CHANNELS[0]))

void demo_feed_init(DemoFeed* feed) {
    memset(feed, 0, sizeof(*feed));
    for(uint8_t i = 0; i < DEMO_PERSONAS; i++) {
        /* start each radio's frame counter somewhere different */
        feed->seq[i] = (uint16_t)(400u + i * 611u);
    }
    feed->rng = 0x2545F491u;
}

uint32_t demo_feed_loop_ms(void) {
    uint32_t total = 0;
    for(size_t i = 0; i < SCRIPT_LEN; i++) total += SCRIPT[i].gap_ms;
    return total;
}

/** A tiny deterministic jitter, so signal strengths breathe. */
static int8_t jitter(DemoFeed* feed, int8_t base) {
    feed->rng = feed->rng * 1664525u + 1013904223u;
    int8_t delta = (int8_t)((int32_t)((feed->rng >> 16) % 9u) - 4);
    int32_t v = base + delta;
    if(v < -95) v = -95;
    if(v > -30) v = -30;
    return (int8_t)v;
}

void demo_feed_step(DemoFeed* feed, uint32_t now_ms, DemoEmit emit, void* context) {
    if(!feed || !emit) return;

    if(!feed->started) {
        feed->started = true;
        feed->step = 0;
        feed->next_at_ms = now_ms + SCRIPT[0].gap_ms;
        return;
    }

    /* Bounded, so a long stall (the app was in another scene) replays a little
     * of the script rather than the whole backlog at once. */
    for(uint8_t guard = 0; guard < 8u; guard++) {
        if((int32_t)(now_ms - feed->next_at_ms) < 0) break;

        const DemoStep* s = &SCRIPT[feed->step];
        const DemoPersona* p = &PERSONAS[s->persona];

        const uint8_t* mac = p->mac;
        if(s->persona == 2u) {
            mac = ROTATION[feed->rotation % 3u];
        }

        uint16_t seq = feed->seq[s->persona];
        feed->seq[s->persona] = (uint16_t)((seq + 3u) & 0x0FFFu);

        emit(
            context,
            mac,
            CHANNELS[feed->step % CHANNEL_N],
            jitter(feed, p->rssi),
            seq,
            p->fp,
            s->ssid);

        /* the rotator changes address every few frames - and keeps counting */
        if(s->persona == 2u && (feed->step % 7u) == 4u) feed->rotation++;

        feed->step = (uint16_t)((feed->step + 1u) % SCRIPT_LEN);
        feed->next_at_ms += SCRIPT[feed->step].gap_ms;
    }
}
