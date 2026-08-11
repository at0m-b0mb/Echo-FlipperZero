#pragma once

#include <gui/view.h>

#include "../helpers/echo_db.h"

#define CHAMBER_MAX_BLIPS 14
/** Feed rows drawn on screen. */
#define CHAMBER_FEED_ROWS 5
/**
 * Feed rows kept in the model. More than are drawn, because the ticker hunts
 * back through them for the most recent *named* probe - and a run of five
 * polite broadcast probes must not make it say "nothing heard yet".
 */
#define CHAMBER_FEED_KEEP 10

/* Layout, mirrored by tools_gen_mockups.py so the README screens are honest. */
#define CH_HDR_BASE  8
#define CH_RULE_Y    10
#define CH_TOP       11
#define CH_BOT       50
#define CH_CX        64
#define CH_CY        31
#define CH_RULE2_Y   51
#define CH_TICK_BASE 61
#define CH_FEED_TOP  13
#define CH_FEED_H    10

/** Chamber draws the room; Feed prints what was said, newest first. */
typedef enum {
    ChamberModeChamber = 0,
    ChamberModeFeed,
    ChamberModeCount,
} ChamberMode;

typedef struct ChamberView ChamberView;
typedef void (*ChamberViewCallback)(void* context);

ChamberView* chamber_view_alloc(void);
void chamber_view_free(ChamberView* view);
View* chamber_view_get_view(ChamberView* view);

/** Fired on OK, after the mode has already flipped. */
void chamber_view_set_ok_callback(ChamberView* view, ChamberViewCallback cb, void* context);

void chamber_view_update(
    ChamberView* view,
    const EchoBlip* blips,
    size_t blip_count,
    const EchoFeedRow* feed,
    size_t feed_count,
    const EchoStats* stats,
    uint8_t channel,
    bool connected,
    bool demo);

/** Advance the animation. Called once per GUI tick. */
void chamber_view_tick(ChamberView* view);

ChamberMode chamber_view_mode(ChamberView* view);
