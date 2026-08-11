#pragma once

/**
 * demo_feed - a room full of phones, with no radio attached.
 *
 * Echo without an ESP32 board is a menu and an empty screen, which is a poor
 * way to show somebody what their pocket is doing. This module replays a
 * scripted half-minute of a real-feeling room: one old handset with its whole
 * saved-network list on display, one modern phone doing everything right, and
 * one that randomizes its address but gives itself away anyway.
 *
 * The script is fixed rather than random, so a screenshot taken today matches
 * a screenshot taken next year, and so the README mockups can be generated
 * from the same personas.
 *
 * No Flipper headers: the caller supplies the clock.
 */

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Same shape as the UART probe callback, so the app can reuse one path. */
typedef void (*DemoEmit)(
    void* context,
    const uint8_t mac[6],
    uint8_t channel,
    int8_t rssi,
    uint16_t seq,
    uint32_t fingerprint,
    const char* ssid);

#define DEMO_PERSONAS 5

typedef struct {
    bool started;
    uint32_t next_at_ms;
    uint16_t step;
    uint16_t seq[DEMO_PERSONAS];
    uint8_t rotation; /* which address the randomizing phone is wearing */
    uint32_t rng;
} DemoFeed;

void demo_feed_init(DemoFeed* feed);

/**
 * Emit every scripted probe whose moment has passed. `now_ms` is any
 * monotonic millisecond clock. The script loops forever.
 */
void demo_feed_step(DemoFeed* feed, uint32_t now_ms, DemoEmit emit, void* context);

/** How long one pass through the script takes, in milliseconds. */
uint32_t demo_feed_loop_ms(void);

#ifdef __cplusplus
}
#endif
