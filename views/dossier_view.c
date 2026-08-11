#include "dossier_view.h"

#include <furi.h>
#include <stdio.h>
#include <string.h>

#define DOS_LIST_TOP     22
#define DOS_LIST_ROW_H   10
#define DOS_LIST_VISIBLE 3
#define DOS_FOOT_RULE    52
#define DOS_FOOT_BASE    62

struct DossierView {
    View* view;
};

typedef struct {
    uint8_t mac[6];
    const char* vendor;
    bool randomized;
    uint8_t link_conf;
    uint8_t group_size;
    uint16_t probes;
    uint16_t named;
    uint32_t first_tick;
    uint32_t last_tick;

    ExpoResult result;

    EchoSsid ssids[DOSSIER_MAX_SSIDS];
    size_t ssid_count;

    uint8_t page;
    uint8_t scroll;
} DossierModel;

/* -------------------------------------------------------------- furniture */

static void dos_page_dots(Canvas* canvas, uint8_t page) {
    int x = 104;
    for(uint8_t i = 0; i < DOSSIER_PAGES; i++) {
        if(i == page) {
            canvas_draw_disc(canvas, x + i * 8, 59, 2);
        } else {
            canvas_draw_circle(canvas, x + i * 8, 59, 2);
        }
    }
}

static void dos_elide(char* dst, size_t dst_len, const char* src, size_t max) {
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

/** The grade as a slab, big enough to be the first thing anyone sees. */
static void dos_grade_slab(Canvas* canvas, DossierModel* m) {
    canvas_draw_box(canvas, 0, 0, 20, 19);
    canvas_set_color(canvas, ColorWhite);
    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str_aligned(
        canvas, 10, 14, AlignCenter, AlignBottom, expo_grade_str((ExpoGrade)m->result.grade));
    canvas_set_color(canvas, ColorBlack);
}

/* ------------------------------------------------------ page 1: the dossier */

static void dos_draw_known(Canvas* canvas, DossierModel* m) {
    char buf[48];

    dos_grade_slab(canvas, m);

    canvas_set_font(canvas, FontSecondary);
    pi_mac_str(m->mac, buf, sizeof(buf));
    canvas_draw_str(canvas, 24, 8, buf);

    /* who it is, as far as anyone can tell from the air */
    if(m->group_size > 1u) {
        snprintf(
            buf,
            sizeof(buf),
            "%u MACs, one radio - %u probes",
            (unsigned)m->group_size,
            (unsigned)m->probes);
    } else if(m->randomized) {
        snprintf(buf, sizeof(buf), "random MAC - %u probes", (unsigned)m->probes);
    } else {
        snprintf(
            buf,
            sizeof(buf),
            "%s - %u probes",
            m->vendor ? m->vendor : "real MAC",
            (unsigned)m->probes);
    }
    canvas_draw_str(canvas, 24, 17, buf);

    canvas_draw_line(canvas, 0, 20, 127, 20);

    if(m->ssid_count == 0) {
        canvas_set_font(canvas, FontPrimary);
        canvas_draw_str_aligned(canvas, 64, 34, AlignCenter, AlignBottom, "It named nothing.");
        canvas_set_font(canvas, FontSecondary);
        canvas_draw_str_aligned(
            canvas, 64, 45, AlignCenter, AlignBottom, "No places. No history.");
    } else {
        size_t first = m->scroll;
        if(first + DOS_LIST_VISIBLE > m->ssid_count) {
            first = (m->ssid_count > DOS_LIST_VISIBLE) ? m->ssid_count - DOS_LIST_VISIBLE : 0;
        }

        canvas_set_font(canvas, FontSecondary);
        for(size_t i = 0; i < DOS_LIST_VISIBLE; i++) {
            size_t idx = first + i;
            if(idx >= m->ssid_count) break;
            const EchoSsid* s = &m->ssids[idx];
            int base = DOS_LIST_TOP + (int)i * DOS_LIST_ROW_H + 7;

            canvas_draw_str(canvas, 2, base, pi_cat_label((PiCat)s->cat));
            /* 40..122 is 82 px; sixteen characters clears the scrollbar even
             * at the widest font this could be drawn in */
            dos_elide(buf, sizeof(buf), s->name, 16);
            canvas_draw_str(canvas, 40, base, buf);
        }

        if(m->ssid_count > DOS_LIST_VISIBLE) {
            int track = DOS_LIST_ROW_H * DOS_LIST_VISIBLE;
            int knob = track * DOS_LIST_VISIBLE / (int)m->ssid_count;
            if(knob < 4) knob = 4;
            size_t span = m->ssid_count - DOS_LIST_VISIBLE;
            int pos = span ? (int)((size_t)(track - knob) * first / span) : 0;
            canvas_draw_box(canvas, 124, DOS_LIST_TOP + pos, 3, knob);
        }
    }

    canvas_draw_line(canvas, 0, DOS_FOOT_RULE, 127, DOS_FOOT_RULE);
    canvas_set_font(canvas, FontSecondary);
    canvas_draw_str(canvas, 2, DOS_FOOT_BASE, expo_verdict((ExpoGrade)m->result.grade));
    dos_page_dots(canvas, m->page);
}

/* ------------------------------------------------- page 2: where it came from */

typedef struct {
    const char* label;
    uint8_t value;
    uint8_t max;
} DosColumn;

static void dos_draw_why(Canvas* canvas, DossierModel* m) {
    char buf[32];

    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str(canvas, 2, 8, "Why this grade");
    canvas_set_font(canvas, FontSecondary);
    snprintf(buf, sizeof(buf), "%u/100", (unsigned)m->result.score);
    canvas_draw_str_aligned(canvas, 126, 8, AlignRight, AlignBottom, buf);
    canvas_draw_line(canvas, 0, 9, 127, 9);

    const DosColumn cols[5] = {
        {"Names", m->result.p_volume, 30},
        {"Places", m->result.p_sensitivity, 25},
        {"Address", m->result.p_geo, 10},
        {"Identity", m->result.p_identity, 10},
        {"Tracking", m->result.p_track, 25},
    };

    canvas_set_font(canvas, FontSecondary);
    for(uint8_t i = 0; i < 5u; i++) {
        int y = 12 + i * 8;
        canvas_draw_str(canvas, 2, y + 7, cols[i].label);

        int bar_x = 46;
        int bar_w = 62;
        canvas_draw_frame(canvas, bar_x, y + 1, bar_w, 6);
        if(cols[i].value > 0u) {
            int fill = (int)((uint32_t)cols[i].value * (uint32_t)(bar_w - 2) / cols[i].max);
            if(fill < 1) fill = 1;
            canvas_draw_box(canvas, bar_x + 1, y + 2, fill, 4);
        }

        snprintf(buf, sizeof(buf), "%u", (unsigned)cols[i].value);
        canvas_draw_str_aligned(canvas, 126, y + 7, AlignRight, AlignBottom, buf);
    }

    canvas_draw_line(canvas, 0, DOS_FOOT_RULE, 127, DOS_FOOT_RULE);
    canvas_set_font(canvas, FontSecondary);
    /* Short, because the page dots start at x=100. */
    if(m->result.cap_applied) {
        canvas_draw_str(canvas, 2, DOS_FOOT_BASE, "capped at D");
    } else if(m->result.floor_applied) {
        canvas_draw_str(canvas, 2, DOS_FOOT_BASE, "floored at B");
    } else {
        canvas_draw_str(canvas, 2, DOS_FOOT_BASE, "no floor or cap");
    }
    dos_page_dots(canvas, m->page);
}

/* ------------------------------------------------------- page 3: what to do */

/** Draw a string that carries its own newline, one line per 9 px. */
static void dos_draw_wrapped(Canvas* canvas, int x, int base, const char* text) {
    char line[40];
    size_t n = 0;
    int y = base;

    for(size_t i = 0;; i++) {
        char c = text[i];
        if(c == '\n' || c == '\0') {
            line[n] = '\0';
            canvas_draw_str(canvas, x, y, line);
            y += 9;
            n = 0;
            if(c == '\0') break;
        } else if(n < sizeof(line) - 1u) {
            line[n++] = c;
        }
    }
}

static void dos_draw_advice(Canvas* canvas, DossierModel* m) {
    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str(canvas, 2, 8, "What to do");
    canvas_draw_line(canvas, 0, 9, 127, 9);

    canvas_set_font(canvas, FontSecondary);
    dos_draw_wrapped(canvas, 2, 19, expo_advice((ExpoGrade)m->result.grade));

    canvas_draw_line(canvas, 0, 32, 127, 32);

    for(uint8_t i = 0; i < m->result.note_count && i < 3u; i++) {
        int base = 41 + i * 9;
        canvas_draw_str(canvas, 2, base, "-");
        canvas_draw_str(canvas, 9, base, m->result.notes[i]);
    }

    canvas_draw_line(canvas, 0, DOS_FOOT_RULE, 127, DOS_FOOT_RULE);
    canvas_draw_str(canvas, 2, DOS_FOOT_BASE, "Echo never transmits.");
    dos_page_dots(canvas, m->page);
}

/* -------------------------------------------------------------------- glue */

static void dossier_view_draw(Canvas* canvas, void* model) {
    DossierModel* m = model;
    switch(m->page) {
    case 1:
        dos_draw_why(canvas, m);
        break;
    case 2:
        dos_draw_advice(canvas, m);
        break;
    default:
        dos_draw_known(canvas, m);
        break;
    }
}

static bool dossier_view_input(InputEvent* event, void* context) {
    DossierView* v = context;
    bool consumed = false;

    if(event->type != InputTypeShort && event->type != InputTypeRepeat) return false;

    switch(event->key) {
    case InputKeyLeft:
        with_view_model(
            v->view,
            DossierModel * m,
            {
                if(m->page > 0) m->page--;
                m->scroll = 0;
            },
            true);
        consumed = true;
        break;
    case InputKeyRight:
        with_view_model(
            v->view,
            DossierModel * m,
            {
                if(m->page + 1u < DOSSIER_PAGES) m->page++;
                m->scroll = 0;
            },
            true);
        consumed = true;
        break;
    case InputKeyUp:
        with_view_model(
            v->view,
            DossierModel * m,
            {
                if(m->page == 0 && m->scroll > 0) m->scroll--;
            },
            true);
        consumed = true;
        break;
    case InputKeyDown:
        with_view_model(
            v->view,
            DossierModel * m,
            {
                if(m->page == 0 && m->ssid_count > DOS_LIST_VISIBLE &&
                   (size_t)m->scroll + DOS_LIST_VISIBLE < m->ssid_count) {
                    m->scroll++;
                }
            },
            true);
        consumed = true;
        break;
    default:
        break;
    }

    return consumed;
}

DossierView* dossier_view_alloc(void) {
    DossierView* v = malloc(sizeof(DossierView));
    v->view = view_alloc();
    view_set_context(v->view, v);
    view_set_draw_callback(v->view, dossier_view_draw);
    view_set_input_callback(v->view, dossier_view_input);
    view_allocate_model(v->view, ViewModelTypeLocking, sizeof(DossierModel));
    return v;
}

void dossier_view_free(DossierView* v) {
    furi_assert(v);
    view_free(v->view);
    free(v);
}

View* dossier_view_get_view(DossierView* v) {
    furi_assert(v);
    return v->view;
}

void dossier_view_reset(DossierView* v) {
    furi_assert(v);
    with_view_model(
        v->view,
        DossierModel * m,
        {
            m->page = 0;
            m->scroll = 0;
        },
        true);
}

void dossier_view_update(
    DossierView* v,
    const EchoDevice* device,
    const ExpoResult* result,
    const EchoSsid* ssids,
    size_t ssid_count,
    uint8_t group_size) {
    furi_assert(v);
    with_view_model(
        v->view,
        DossierModel * m,
        {
            memcpy(m->mac, device->mac, 6);
            m->vendor = pi_mac_vendor(device->mac);
            m->randomized = device->randomized;
            m->link_conf = device->link_conf;
            m->group_size = group_size;
            m->probes = device->probes;
            m->named = device->named_total;
            m->first_tick = device->first_tick;
            m->last_tick = device->last_tick;
            m->result = *result;

            size_t n = ssid_count < DOSSIER_MAX_SSIDS ? ssid_count : DOSSIER_MAX_SSIDS;
            for(size_t i = 0; i < n; i++) m->ssids[i] = ssids[i];
            m->ssid_count = n;
            if((size_t)m->scroll + DOS_LIST_VISIBLE > n) m->scroll = 0;
        },
        true);
}
