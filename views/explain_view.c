#include "explain_view.h"

#include <furi.h>
#include <stdio.h>
#include <string.h>

/*
 * Four pages that answer "what am I actually looking at", drawn rather than
 * written wherever a drawing does it better. The animation is driven off a
 * single counter: at ten ticks a second, `anim / 5` is a half-second beat.
 */

#define EX_ART_TOP  12
#define EX_ART_BOT  38
#define EX_RULE_TOP 10
#define EX_RULE_MID 40
#define EX_LINE1    49
#define EX_LINE2    58

struct ExplainView {
    View* view;
};

typedef struct {
    uint8_t page;
    uint8_t anim;
} ExplainModel;

typedef struct {
    const char* title;
    const char* line1;
    const char* line2;
} ExplainCopy;

/*
 * Titles stay at fifteen characters or under: the page dots sit at x=99, and
 * FontPrimary is six pixels a character.
 */
static const ExplainCopy EX_COPY[EXPLAIN_PAGES] = {
    {"It keeps asking", "It calls out for networks", "it joined before, by name."},
    {"Anyone can hear", "Probes are unencrypted,", "and sent before joining."},
    {"A name, a place", "Hotels, gyms, clinics, home.", "A saved list is a diary."},
    {"Still one phone", "Its probe shape and frame", "counter give it away."},
};

/* The names the phone on page one is calling out for. */
static const char* const EX_CALLS[] = {
    "NETGEAR58?",
    "Hilton_Honors?",
    "PlanetFitness?",
    "CityHospital?",
};
#define EX_CALL_N (sizeof(EX_CALLS) / sizeof(EX_CALLS[0]))

/* The mapping page cycles these. */
static const char* const EX_PAIRS[][2] = {
    {"Hilton_Honors", "a hotel"},
    {"PlanetFitness", "your gym"},
    {"NETGEAR58", "your home"},
    {"ACME-Corp", "your work"},
};
#define EX_PAIR_N (sizeof(EX_PAIRS) / sizeof(EX_PAIRS[0]))

/* -------------------------------------------------------------- furniture */

static void ex_dots(Canvas* canvas, uint8_t page) {
    for(uint8_t i = 0; i < EXPLAIN_PAGES; i++) {
        int x = 101 + i * 8;
        if(i == page) {
            canvas_draw_disc(canvas, x, 5, 2);
        } else {
            canvas_draw_circle(canvas, x, 5, 2);
        }
    }
}

/** A handset: a body, a screen and a button. */
static void ex_phone(Canvas* canvas, int x, int y) {
    canvas_draw_rframe(canvas, x, y, 13, 23, 2);
    canvas_draw_line(canvas, x + 2, y + 4, x + 10, y + 4);
    canvas_draw_frame(canvas, x + 2, y + 6, 9, 11);
    canvas_draw_dot(canvas, x + 6, y + 19);
}

/** An aerial: a mast with a spark on top. */
static void ex_antenna(Canvas* canvas, int x, int y) {
    canvas_draw_line(canvas, x, y + 6, x, y + 22);
    canvas_draw_line(canvas, x - 4, y + 22, x + 4, y + 22);
    canvas_draw_line(canvas, x, y + 6, x - 4, y);
    canvas_draw_line(canvas, x, y + 6, x + 4, y);
    canvas_draw_disc(canvas, x, y + 4, 1);
}

/** Rings leaving a point, clipped by the art band. */
static void ex_rings(Canvas* canvas, int cx, int cy, uint8_t anim, uint8_t count) {
    for(uint8_t i = 0; i < count; i++) {
        int r = 5 + (int)(((anim + i * 6u) % 18u));
        if(r > 4) canvas_draw_circle(canvas, cx, cy, r);
    }
}

/* ------------------------------------------------------------------ pages */

static void ex_page_asking(Canvas* canvas, ExplainModel* m) {
    ex_phone(canvas, 8, EX_ART_TOP + 2);
    ex_rings(canvas, 14, EX_ART_TOP + 13, m->anim, 2);

    /* the speech box, with a different network name every couple of seconds */
    const char* call = EX_CALLS[(m->anim / 20u) % EX_CALL_N];
    canvas_draw_rframe(canvas, 44, EX_ART_TOP + 6, 82, 15, 3);
    canvas_draw_line(canvas, 44, EX_ART_TOP + 14, 38, EX_ART_TOP + 17);
    canvas_set_font(canvas, FontSecondary);
    canvas_draw_str_aligned(canvas, 85, EX_ART_TOP + 17, AlignCenter, AlignBottom, call);
}

static void ex_page_hearing(Canvas* canvas, ExplainModel* m) {
    ex_phone(canvas, 4, EX_ART_TOP + 2);
    ex_antenna(canvas, 116, EX_ART_TOP + 3);

    /* one wavefront crossing the gap, over and over */
    int span = 96;
    int x = 22 + (int)(((uint32_t)m->anim * 6u) % (uint32_t)span);
    for(int k = 0; k < 3; k++) {
        int xx = x - k * 10;
        if(xx < 22 || xx > 110) continue;
        canvas_draw_line(canvas, xx, EX_ART_TOP + 8, xx, EX_ART_TOP + 18);
        canvas_draw_dot(canvas, xx + 1, EX_ART_TOP + 9);
        canvas_draw_dot(canvas, xx + 1, EX_ART_TOP + 17);
    }

    canvas_set_font(canvas, FontSecondary);
    canvas_draw_str(canvas, 30, EX_ART_BOT, "no password needed");
}

static void ex_page_places(Canvas* canvas, ExplainModel* m) {
    size_t i = (m->anim / 20u) % EX_PAIR_N;

    canvas_set_font(canvas, FontSecondary);

    canvas_draw_rframe(canvas, 2, EX_ART_TOP + 4, 62, 14, 3);
    canvas_draw_str_aligned(
        canvas, 33, EX_ART_TOP + 14, AlignCenter, AlignBottom, EX_PAIRS[i][0]);

    /* an arrow that nudges along with the beat */
    int ax = 66 + (int)((m->anim / 3u) % 3u);
    canvas_draw_line(canvas, ax, EX_ART_TOP + 11, ax + 8, EX_ART_TOP + 11);
    canvas_draw_line(canvas, ax + 5, EX_ART_TOP + 8, ax + 8, EX_ART_TOP + 11);
    canvas_draw_line(canvas, ax + 5, EX_ART_TOP + 14, ax + 8, EX_ART_TOP + 11);

    canvas_draw_box(canvas, 78, EX_ART_TOP + 4, 48, 14);
    canvas_set_color(canvas, ColorWhite);
    canvas_draw_str_aligned(
        canvas, 102, EX_ART_TOP + 14, AlignCenter, AlignBottom, EX_PAIRS[i][1]);
    canvas_set_color(canvas, ColorBlack);
}

static void ex_page_linking(Canvas* canvas, ExplainModel* m) {
    static const char* const macs[3] = {"8A:3F:11", "B6:7C:D9", "EA:05:63"};
    static const char* const seqs[3] = {"412", "415", "418"};

    canvas_set_font(canvas, FontSecondary);

    /* The fingerprint column, boxed: three addresses come and go around it and
     * it never changes. That box is the whole argument of the page. */
    canvas_draw_frame(canvas, 50, EX_ART_TOP - 1, 46, 28);

    uint8_t shown = (uint8_t)(1u + (m->anim / 8u) % 3u);
    for(uint8_t i = 0; i < shown; i++) {
        int base = EX_ART_TOP + 7 + i * 9;
        canvas_draw_str(canvas, 2, base, macs[i]);
        canvas_draw_str(canvas, 53, base, "77C10E55");
        canvas_draw_str(canvas, 100, base, seqs[i]);
    }
}

/* ------------------------------------------------------------------- draw */

static void explain_view_draw(Canvas* canvas, void* model) {
    ExplainModel* m = model;
    const ExplainCopy* c = &EX_COPY[m->page % EXPLAIN_PAGES];

    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str(canvas, 2, 8, c->title);
    ex_dots(canvas, m->page);
    canvas_draw_line(canvas, 0, EX_RULE_TOP, 127, EX_RULE_TOP);

    switch(m->page) {
    case 1:
        ex_page_hearing(canvas, m);
        break;
    case 2:
        ex_page_places(canvas, m);
        break;
    case 3:
        ex_page_linking(canvas, m);
        break;
    default:
        ex_page_asking(canvas, m);
        break;
    }

    canvas_draw_line(canvas, 0, EX_RULE_MID, 127, EX_RULE_MID);
    canvas_set_font(canvas, FontSecondary);
    canvas_draw_str(canvas, 2, EX_LINE1, c->line1);
    canvas_draw_str(canvas, 2, EX_LINE2, c->line2);
}

static bool explain_view_input(InputEvent* event, void* context) {
    ExplainView* v = context;
    if(event->type != InputTypeShort && event->type != InputTypeRepeat) return false;

    if(event->key == InputKeyRight || event->key == InputKeyOk) {
        with_view_model(
            v->view,
            ExplainModel * m,
            {
                if(m->page + 1u < EXPLAIN_PAGES) m->page++;
            },
            true);
        return true;
    }
    if(event->key == InputKeyLeft) {
        with_view_model(
            v->view,
            ExplainModel * m,
            {
                if(m->page > 0) m->page--;
            },
            true);
        return true;
    }
    return false;
}

/* ------------------------------------------------------------------- glue */

ExplainView* explain_view_alloc(void) {
    ExplainView* v = malloc(sizeof(ExplainView));
    v->view = view_alloc();
    view_set_context(v->view, v);
    view_set_draw_callback(v->view, explain_view_draw);
    view_set_input_callback(v->view, explain_view_input);
    view_allocate_model(v->view, ViewModelTypeLocking, sizeof(ExplainModel));
    return v;
}

void explain_view_free(ExplainView* v) {
    furi_assert(v);
    view_free(v->view);
    free(v);
}

View* explain_view_get_view(ExplainView* v) {
    furi_assert(v);
    return v->view;
}

void explain_view_tick(ExplainView* v) {
    furi_assert(v);
    with_view_model(v->view, ExplainModel * m, { m->anim++; }, true);
}

void explain_view_reset(ExplainView* v) {
    furi_assert(v);
    with_view_model(
        v->view,
        ExplainModel * m,
        {
            m->page = 0;
            m->anim = 0;
        },
        true);
}
