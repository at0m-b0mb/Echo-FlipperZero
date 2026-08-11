#include "net_list_view.h"

#include <furi.h>
#include <stdio.h>
#include <string.h>

struct NetListView {
    View* view;
};

typedef struct {
    EchoSsid rows[NET_LIST_MAX];
    size_t count;
    size_t selected;
    bool connected;
} NetListModel;

static void net_elide(char* dst, size_t dst_len, const char* src, size_t max) {
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

static void net_list_view_draw(Canvas* canvas, void* model) {
    NetListModel* m = model;
    char buf[40];

    /* ---- header ---- */
    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str(canvas, 2, 8, "Networks");
    canvas_set_font(canvas, FontSecondary);
    snprintf(buf, sizeof(buf), "%u", (unsigned)m->count);
    canvas_draw_str_aligned(canvas, 126, 8, AlignRight, AlignBottom, buf);
    canvas_draw_line(canvas, 0, 9, 127, 9);

    if(m->count == 0) {
        canvas_set_font(canvas, FontPrimary);
        canvas_draw_str_aligned(canvas, 64, 30, AlignCenter, AlignBottom, "Nothing named yet");
        canvas_set_font(canvas, FontSecondary);
        canvas_draw_str_aligned(
            canvas,
            64,
            42,
            AlignCenter,
            AlignBottom,
            m->connected ? "Everyone here is behaving." : "Start listening first.");
        return;
    }

    /* ---- rows ---- */
    size_t first = 0;
    if(m->selected >= NET_LIST_VISIBLE) first = m->selected - (NET_LIST_VISIBLE - 1);

    for(size_t i = 0; i < NET_LIST_VISIBLE; i++) {
        size_t idx = first + i;
        if(idx >= m->count) break;
        const EchoSsid* s = &m->rows[idx];
        int y = NET_ROWS_TOP + (int)i * NET_ROW_H;
        int base = y + 8;
        bool sel = (idx == m->selected);

        if(sel) {
            canvas_draw_box(canvas, 0, y, 122, NET_ROW_H);
            canvas_set_color(canvas, ColorWhite);
        }

        /* category letter, boxed */
        char tag[2] = {pi_cat_tag((PiCat)s->cat), '\0'};
        canvas_draw_frame(canvas, 2, y + 1, 9, 9);
        canvas_set_font(canvas, FontSecondary);
        canvas_draw_str_aligned(canvas, 6, y + 8, AlignCenter, AlignBottom, tag);

        net_elide(buf, sizeof(buf), s->name, 21);
        canvas_draw_str(canvas, 14, base, buf);

        snprintf(buf, sizeof(buf), "x%u", (unsigned)s->hits);
        canvas_draw_str_aligned(canvas, 119, base, AlignRight, AlignBottom, buf);

        if(sel) canvas_set_color(canvas, ColorBlack);
    }

    /* ---- scrollbar ---- */
    if(m->count > NET_LIST_VISIBLE) {
        int track = NET_ROW_H * NET_LIST_VISIBLE;
        int knob = track * NET_LIST_VISIBLE / (int)m->count;
        if(knob < 4) knob = 4;
        int pos = (int)((size_t)(track - knob) * m->selected / (m->count - 1));
        canvas_draw_box(canvas, 124, NET_ROWS_TOP + pos, 3, knob);
    }

    /* ---- what the selected name gives away ----
     *
     * The sentence gets the whole strip. It is the payload of the screen: the
     * tag letter in the row is decoration, and "Where you sleep." is the bit
     * that makes somebody put their phone down. The pin marker goes on the far
     * right so it can never push the sentence off the edge. */
    const EchoSsid* sel = &m->rows[m->selected];
    canvas_draw_line(canvas, 0, NET_STRIP_RULE, 127, NET_STRIP_RULE);
    canvas_set_font(canvas, FontSecondary);
    canvas_draw_str(canvas, 2, NET_STRIP_BASE, pi_cat_reveals((PiCat)sel->cat));

    if(sel->flags & PiFlagGeolocatable) {
        /* this name is distinctive enough to resolve to one street address */
        canvas_draw_box(canvas, 118, NET_STRIP_RULE + 1, 9, 9);
        canvas_set_color(canvas, ColorWhite);
        canvas_draw_str_aligned(canvas, 122, NET_STRIP_BASE, AlignCenter, AlignBottom, "!");
        canvas_set_color(canvas, ColorBlack);
    }
}

static bool net_list_view_input(InputEvent* event, void* context) {
    NetListView* v = context;
    bool consumed = false;

    if(event->type == InputTypeShort || event->type == InputTypeRepeat) {
        if(event->key == InputKeyUp) {
            with_view_model(
                v->view,
                NetListModel * m,
                {
                    if(m->selected > 0) m->selected--;
                },
                true);
            consumed = true;
        } else if(event->key == InputKeyDown) {
            with_view_model(
                v->view,
                NetListModel * m,
                {
                    if(m->count && m->selected + 1 < m->count) m->selected++;
                },
                true);
            consumed = true;
        }
    }
    return consumed;
}

NetListView* net_list_view_alloc(void) {
    NetListView* v = malloc(sizeof(NetListView));
    v->view = view_alloc();
    view_set_context(v->view, v);
    view_set_draw_callback(v->view, net_list_view_draw);
    view_set_input_callback(v->view, net_list_view_input);
    view_allocate_model(v->view, ViewModelTypeLocking, sizeof(NetListModel));
    return v;
}

void net_list_view_free(NetListView* v) {
    furi_assert(v);
    view_free(v->view);
    free(v);
}

View* net_list_view_get_view(NetListView* v) {
    furi_assert(v);
    return v->view;
}

void net_list_view_update(NetListView* v, const EchoSsid* rows, size_t count, bool connected) {
    furi_assert(v);
    with_view_model(
        v->view,
        NetListModel * m,
        {
            size_t n = count < NET_LIST_MAX ? count : NET_LIST_MAX;
            for(size_t i = 0; i < n; i++) m->rows[i] = rows[i];
            m->count = n;
            m->connected = connected;
            if(m->selected >= n) m->selected = n ? n - 1 : 0;
        },
        true);
}
