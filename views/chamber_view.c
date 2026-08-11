#include "chamber_view.h"

#include <furi.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#define CH_2PI 6.28318530718f

/*
 * The chamber is 128 wide and 39 tall, so a bearing is stretched hard sideways
 * and squashed vertically. Scaling the two axes separately - rather than one
 * factor and a clamp - is what stops every faint device piling up along the
 * top and bottom edges.
 */
#define CH_RX_SCALE 2.5f
#define CH_RY_SCALE 0.85f
#define CH_R_MIN    7.0f
#define CH_R_MAX    20.0f

/* A ripple grows to this radius over ECHO_RIPPLE_MS, then it is gone. Kept
 * small enough that two neighbours' rings read as two rings. */
#define CH_RIPPLE_MAX 9

struct ChamberView {
    View* view;
    ChamberViewCallback ok_cb;
    void* ok_ctx;
};

typedef struct {
    EchoBlip blips[CHAMBER_MAX_BLIPS];
    size_t blip_count;

    EchoFeedRow feed[CHAMBER_FEED_KEEP];
    size_t feed_count;

    EchoStats stats;
    uint8_t channel;
    bool connected;
    bool demo;

    uint8_t mode;
    uint8_t anim;
} ChamberModel;

/* ------------------------------------------------------------------ pieces */

static void chamber_polar(uint8_t angle, float radius, int* x, int* y) {
    float rad = (float)angle * (CH_2PI / 256.0f);
    *x = CH_CX + (int)(cosf(rad) * radius * CH_RX_SCALE);
    *y = CH_CY + (int)(sinf(rad) * radius * CH_RY_SCALE);
}

/** Signal strength to distance from the middle. Loud is close. */
static float chamber_radius(int8_t rssi) {
    int v = rssi;
    if(v > -30) v = -30;
    if(v < -95) v = -95;
    float t = (float)(-30 - v) / 65.0f; /* 0 = loud, 1 = faint */
    return CH_R_MIN + t * (CH_R_MAX - CH_R_MIN);
}

static void chamber_clamp(int* x, int* y) {
    if(*x < 3) *x = 3;
    if(*x > 124) *x = 124;
    if(*y < CH_TOP + 2) *y = CH_TOP + 2;
    if(*y > CH_BOT - 2) *y = CH_BOT - 2;
}

/** One point of a ripple, dropped if it would land outside the chamber band. */
static void chamber_plot(Canvas* canvas, int x, int y) {
    if(x < 0 || x > 127) return;
    if(y < CH_TOP + 1 || y > CH_BOT - 1) return;
    canvas_draw_dot(canvas, x, y);
}

/**
 * A circle clipped to the chamber.
 *
 * canvas_draw_circle() would happily scribble a ripple over the header and the
 * ticker, and a ring cut off by the edge of its own band is what a wavefront
 * leaving the room should look like anyway. Midpoint algorithm, eight-way
 * symmetry, every point filtered.
 */
static void chamber_ring(Canvas* canvas, int cx, int cy, int r) {
    if(r < 1) return;
    int x = 0;
    int y = r;
    int d = 3 - 2 * r;

    while(y >= x) {
        chamber_plot(canvas, cx + x, cy + y);
        chamber_plot(canvas, cx - x, cy + y);
        chamber_plot(canvas, cx + x, cy - y);
        chamber_plot(canvas, cx - x, cy - y);
        chamber_plot(canvas, cx + y, cy + x);
        chamber_plot(canvas, cx - y, cy + x);
        chamber_plot(canvas, cx + y, cy - x);
        chamber_plot(canvas, cx - y, cy - x);

        if(d > 0) {
            y--;
            d += 4 * (x - y) + 10;
        } else {
            d += 4 * x + 6;
        }
        x++;
    }
}

/** Reverse the low four bits: 1 -> 8, 2 -> 4, 3 -> 12. */
static uint8_t chamber_bitrev4(uint8_t v) {
    uint8_t r = 0;
    for(uint8_t i = 0; i < 4u; i++) {
        r = (uint8_t)((r << 1) | ((v >> i) & 1u));
    }
    return r;
}

/**
 * The listener, in the middle: a crosshair inside a ring. That is where you
 * are standing, and everything else on this screen is shouting at it.
 */
static void chamber_listener(Canvas* canvas) {
    canvas_draw_circle(canvas, CH_CX, CH_CY, 3);
    canvas_draw_line(canvas, CH_CX - 5, CH_CY, CH_CX - 4, CH_CY);
    canvas_draw_line(canvas, CH_CX + 4, CH_CY, CH_CX + 5, CH_CY);
    canvas_draw_line(canvas, CH_CX, CH_CY - 5, CH_CX, CH_CY - 4);
    canvas_draw_line(canvas, CH_CX, CH_CY + 4, CH_CX, CH_CY + 5);
    canvas_draw_dot(canvas, CH_CX, CH_CY);
}

/** The boxed category letter used in the ticker and the feed. */
static void chamber_tag(Canvas* canvas, int x, int y, uint8_t cat, bool named) {
    char t[2] = {pi_cat_tag((PiCat)cat), '\0'};
    if(!named) {
        canvas_draw_frame(canvas, x, y, 9, 9);
        return;
    }
    canvas_draw_box(canvas, x, y, 9, 9);
    canvas_set_color(canvas, ColorWhite);
    canvas_set_font(canvas, FontSecondary);
    canvas_draw_str_aligned(canvas, x + 4, y + 7, AlignCenter, AlignBottom, t);
    canvas_set_color(canvas, ColorBlack);
}

/** Copy at most `max` characters, marking the cut so nothing reads as complete. */
static void chamber_elide(char* dst, size_t dst_len, const char* src, size_t max) {
    size_t limit = (max < dst_len - 1u) ? max : dst_len - 1u;
    size_t n = 0;
    while(src[n] != '\0' && n < limit) {
        dst[n] = src[n];
        n++;
    }
    dst[n] = '\0';
    if(src[n] != '\0' && n >= 2) {
        dst[n - 1] = '.';
        dst[n - 2] = '.';
    }
}

/* ------------------------------------------------------------------ header */

static void chamber_draw_header(Canvas* canvas, ChamberModel* m) {
    char buf[32];

    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str(canvas, 2, CH_HDR_BASE, "ECHO");

    canvas_set_font(canvas, FontSecondary);
    if(m->demo) {
        canvas_draw_str(canvas, 32, CH_HDR_BASE, "DEMO");
    }

    if(m->channel == 0u) {
        snprintf(
            buf,
            sizeof(buf),
            "D%u L%u hop",
            (unsigned)m->stats.device_count,
            (unsigned)m->stats.leaking_count);
    } else {
        snprintf(
            buf,
            sizeof(buf),
            "D%u L%u c%u",
            (unsigned)m->stats.device_count,
            (unsigned)m->stats.leaking_count,
            (unsigned)(m->channel > 13u ? 13u : m->channel));
    }
    canvas_draw_str_aligned(canvas, 118, CH_HDR_BASE, AlignRight, AlignBottom, buf);

    /* the link light: solid when the board is talking, hollow when it is not */
    if(m->connected) {
        canvas_draw_disc(canvas, 123, 4, 2);
    } else {
        canvas_draw_circle(canvas, 123, 4, 2);
    }

    canvas_draw_line(canvas, 0, CH_RULE_Y, 127, CH_RULE_Y);
}

/* ----------------------------------------------------------------- chamber */

static void chamber_draw_room(Canvas* canvas, ChamberModel* m) {
    if(m->blip_count == 0) {
        char wait[16];
        const char* line1;
        const char* line2;

        if(m->connected) {
            /* Three dots that fill in, so the screen is visibly alive.
             *
             * The word is written out again here rather than read back out of
             * line1: with a variable the compiler cannot prove which of the two
             * messages lands in the buffer, and -Werror=format-truncation is
             * right to complain about the longer one. */
            uint8_t dots = (uint8_t)((m->anim / 4u) % 4u);
            snprintf(wait, sizeof(wait), "Listening%.*s", dots, "...");
            line1 = wait;
            line2 = "Nobody has spoken yet.";
        } else {
            line1 = "No board on the GPIO";
            line2 = "Flash it, or turn on Demo.";
        }

        canvas_set_font(canvas, FontPrimary);
        canvas_draw_str_aligned(canvas, 64, 27, AlignCenter, AlignBottom, line1);
        canvas_set_font(canvas, FontSecondary);
        canvas_draw_str_aligned(canvas, 64, 40, AlignCenter, AlignBottom, line2);
        return;
    }

    chamber_listener(canvas);

    /*
     * Sixteen places round the room, handed out by bit-reversing the device's
     * table index: 0, 8, 4, 12, 2, 10, 6, 14... Consecutive arrivals land on
     * opposite sides, so a room of five phones spreads instead of piling into
     * one corner, and a device keeps its spot for as long as it is there
     * because the index does not move. A hash of the MAC was the obvious thing
     * to use and looked like spilled sand.
     */
    uint16_t used = 0;

    /* Ripples are the loud part of this screen, and fourteen at once is a mess
     * rather than a room. Blips arrive loudest-first, so the cap keeps the ones
     * standing closest to you. */
    uint8_t ripples = 0;

    for(size_t i = 0; i < m->blip_count; i++) {
        const EchoBlip* b = &m->blips[i];

        uint8_t bucket = chamber_bitrev4((uint8_t)(b->slot & 0x0Fu));
        for(uint8_t t = 0; t < 16u; t++) {
            uint8_t candidate = (uint8_t)((bucket + t) & 0x0Fu);
            if(!(used & (uint16_t)(1u << candidate))) {
                bucket = candidate;
                break;
            }
        }
        used |= (uint16_t)(1u << bucket);

        int x, y;
        chamber_polar((uint8_t)(bucket * 16u + 8u), chamber_radius(b->rssi), &x, &y);
        chamber_clamp(&x, &y);

        /* the ripple: a probe going out, still in the air */
        if(b->age_ms < ECHO_RIPPLE_MS && ripples < 4u) {
            int r = (int)(((uint32_t)b->age_ms * CH_RIPPLE_MAX) / ECHO_RIPPLE_MS);
            if(r >= 2) {
                chamber_ring(canvas, x, y, r);
                /* a name given away gets a heavier ring */
                if(b->named) chamber_ring(canvas, x, y, r - 1);
                ripples++;
            }
        }

        /* the device itself: solid once it has named a network */
        if(b->named) {
            canvas_draw_disc(canvas, x, y, 2);
        } else {
            canvas_draw_circle(canvas, x, y, 2);
        }
    }
}

/* -------------------------------------------------------------------- feed */

static void chamber_draw_feed(Canvas* canvas, ChamberModel* m) {
    char buf[40];

    if(m->feed_count == 0) {
        canvas_set_font(canvas, FontSecondary);
        canvas_draw_str_aligned(canvas, 64, 34, AlignCenter, AlignBottom, "Nothing heard yet.");
        return;
    }

    canvas_set_font(canvas, FontSecondary);
    for(size_t i = 0; i < m->feed_count && i < CHAMBER_FEED_ROWS; i++) {
        const EchoFeedRow* r = &m->feed[i];
        int y = CH_FEED_TOP + (int)i * CH_FEED_H;
        int base = y + 8;
        bool named = (r->ssid[0] != '\0');

        chamber_tag(canvas, 2, y, r->cat, named);

        if(named) {
            chamber_elide(buf, sizeof(buf), r->ssid, 22);
        } else {
            /* the polite kind of probe. Worth showing: it is what good looks like */
            snprintf(buf, sizeof(buf), "no name given");
        }
        canvas_draw_str(canvas, 14, base, buf);

        snprintf(buf, sizeof(buf), "%d", r->rssi);
        canvas_draw_str_aligned(canvas, 126, base, AlignRight, AlignBottom, buf);
    }
}

/* ------------------------------------------------------------------ ticker */

static void chamber_draw_ticker(Canvas* canvas, ChamberModel* m) {
    char buf[40];
    canvas_draw_line(canvas, 0, CH_RULE2_Y, 127, CH_RULE2_Y);

    /* the newest named probe is the headline; if there is none, say so */
    const EchoFeedRow* latest = NULL;
    for(size_t i = 0; i < m->feed_count; i++) {
        if(m->feed[i].ssid[0] != '\0') {
            latest = &m->feed[i];
            break;
        }
    }

    if(!latest) {
        canvas_set_font(canvas, FontSecondary);
        const char* msg = (m->stats.device_count > 0) ? "No names given away. Good." :
                                                        "OK: switch to the raw feed";
        canvas_draw_str(canvas, 2, CH_TICK_BASE, msg);
        return;
    }

    chamber_tag(canvas, 2, CH_RULE2_Y + 2, latest->cat, true);

    canvas_set_font(canvas, FontPrimary);
    chamber_elide(buf, sizeof(buf), latest->ssid, 16);
    canvas_draw_str(canvas, 14, CH_TICK_BASE, buf);

    canvas_set_font(canvas, FontSecondary);
    snprintf(buf, sizeof(buf), "%d", latest->rssi);
    canvas_draw_str_aligned(canvas, 126, CH_TICK_BASE, AlignRight, AlignBottom, buf);
}

/* ------------------------------------------------------------------- draw */

static void chamber_view_draw(Canvas* canvas, void* model) {
    ChamberModel* m = model;

    chamber_draw_header(canvas, m);

    if(m->mode == (uint8_t)ChamberModeFeed) {
        chamber_draw_feed(canvas, m);
    } else {
        chamber_draw_room(canvas, m);
        chamber_draw_ticker(canvas, m);
    }
}

static bool chamber_view_input(InputEvent* event, void* context) {
    ChamberView* v = context;
    if(event->type == InputTypeShort && event->key == InputKeyOk) {
        with_view_model(
            v->view,
            ChamberModel * m,
            { m->mode = (uint8_t)((m->mode + 1u) % (uint8_t)ChamberModeCount); },
            true);
        if(v->ok_cb) v->ok_cb(v->ok_ctx);
        return true;
    }
    return false;
}

/* ------------------------------------------------------------------- glue */

ChamberView* chamber_view_alloc(void) {
    ChamberView* v = malloc(sizeof(ChamberView));
    v->ok_cb = NULL;
    v->ok_ctx = NULL;
    v->view = view_alloc();
    view_set_context(v->view, v);
    view_set_draw_callback(v->view, chamber_view_draw);
    view_set_input_callback(v->view, chamber_view_input);
    view_allocate_model(v->view, ViewModelTypeLocking, sizeof(ChamberModel));
    return v;
}

void chamber_view_free(ChamberView* v) {
    furi_assert(v);
    view_free(v->view);
    free(v);
}

View* chamber_view_get_view(ChamberView* v) {
    furi_assert(v);
    return v->view;
}

void chamber_view_set_ok_callback(ChamberView* v, ChamberViewCallback cb, void* context) {
    furi_assert(v);
    v->ok_cb = cb;
    v->ok_ctx = context;
}

void chamber_view_update(
    ChamberView* v,
    const EchoBlip* blips,
    size_t blip_count,
    const EchoFeedRow* feed,
    size_t feed_count,
    const EchoStats* stats,
    uint8_t channel,
    bool connected,
    bool demo) {
    furi_assert(v);
    with_view_model(
        v->view,
        ChamberModel * m,
        {
            size_t bn = blip_count < CHAMBER_MAX_BLIPS ? blip_count : CHAMBER_MAX_BLIPS;
            for(size_t i = 0; i < bn; i++) m->blips[i] = blips[i];
            m->blip_count = bn;

            size_t fn = feed_count < CHAMBER_FEED_KEEP ? feed_count : CHAMBER_FEED_KEEP;
            for(size_t i = 0; i < fn; i++) m->feed[i] = feed[i];
            m->feed_count = fn;

            m->stats = *stats;
            m->channel = channel;
            m->connected = connected;
            m->demo = demo;
        },
        true);
}

void chamber_view_tick(ChamberView* v) {
    furi_assert(v);
    with_view_model(v->view, ChamberModel * m, { m->anim++; }, true);
}

ChamberMode chamber_view_mode(ChamberView* v) {
    furi_assert(v);
    ChamberMode mode;
    with_view_model(v->view, ChamberModel * m, { mode = (ChamberMode)m->mode; }, false);
    return mode;
}
