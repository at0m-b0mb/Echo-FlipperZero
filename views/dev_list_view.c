#include "dev_list_view.h"

#include <furi.h>
#include <stdio.h>
#include <string.h>

struct DevListView {
    View* view;
    DevListViewCallback ok_cb;
    void* ok_ctx;
};

typedef struct {
    EchoDeviceRow rows[DEV_LIST_MAX];
    size_t count;
    size_t selected;
    bool connected;
} DevListModel;

/**
 * The grade, in a box. Filled for anything worse than a B, so the screen reads
 * at arm's length: the solid blocks are the phones giving themselves away.
 */
static void dev_draw_grade(Canvas* canvas, int x, int y, uint8_t grade, bool inverted) {
    const char* g = expo_grade_str((ExpoGrade)grade);
    bool solid = (grade >= (uint8_t)ExpoGradeC);

    canvas_set_font(canvas, FontSecondary);
    if(solid != inverted) {
        canvas_draw_box(canvas, x, y, 13, 9);
        canvas_set_color(canvas, ColorWhite);
        canvas_draw_str_aligned(canvas, x + 6, y + 8, AlignCenter, AlignBottom, g);
        canvas_set_color(canvas, inverted ? ColorWhite : ColorBlack);
    } else {
        canvas_draw_frame(canvas, x, y, 13, 9);
        canvas_draw_str_aligned(canvas, x + 6, y + 8, AlignCenter, AlignBottom, g);
    }
}

static void dev_list_view_draw(Canvas* canvas, void* model) {
    DevListModel* m = model;
    char buf[40];

    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str(canvas, 2, 8, "Devices");
    canvas_set_font(canvas, FontSecondary);
    snprintf(buf, sizeof(buf), "%u", (unsigned)m->count);
    canvas_draw_str_aligned(canvas, 126, 8, AlignRight, AlignBottom, buf);
    canvas_draw_line(canvas, 0, 9, 127, 9);

    if(m->count == 0) {
        canvas_set_font(canvas, FontPrimary);
        canvas_draw_str_aligned(canvas, 64, 30, AlignCenter, AlignBottom, "Nobody here yet");
        canvas_set_font(canvas, FontSecondary);
        canvas_draw_str_aligned(
            canvas,
            64,
            42,
            AlignCenter,
            AlignBottom,
            m->connected ? "Waiting for a probe." : "Start listening first.");
        return;
    }

    size_t first = 0;
    if(m->selected >= DEV_LIST_VISIBLE) first = m->selected - (DEV_LIST_VISIBLE - 1);

    for(size_t i = 0; i < DEV_LIST_VISIBLE; i++) {
        size_t idx = first + i;
        if(idx >= m->count) break;
        const EchoDeviceRow* r = &m->rows[idx];
        int y = DEV_ROWS_TOP + (int)i * DEV_ROW_H;
        int base = y + 8;
        bool sel = (idx == m->selected);

        if(sel) {
            canvas_draw_box(canvas, 0, y, 122, DEV_ROW_H);
            canvas_set_color(canvas, ColorWhite);
        }

        dev_draw_grade(canvas, 2, y + 1, r->grade, sel);

        canvas_set_font(canvas, FontSecondary);
        pi_mac_str(r->mac, buf, sizeof(buf));
        canvas_draw_str(canvas, 18, base, buf);

        if(r->named > 0u) {
            snprintf(buf, sizeof(buf), "x%u", (unsigned)r->named);
        } else {
            snprintf(buf, sizeof(buf), "-");
        }
        canvas_draw_str_aligned(canvas, 119, base, AlignRight, AlignBottom, buf);

        if(sel) canvas_set_color(canvas, ColorBlack);
    }

    if(m->count > DEV_LIST_VISIBLE) {
        int track = DEV_ROW_H * DEV_LIST_VISIBLE;
        int knob = track * DEV_LIST_VISIBLE / (int)m->count;
        if(knob < 4) knob = 4;
        int pos = (int)((size_t)(track - knob) * m->selected / (m->count - 1));
        canvas_draw_box(canvas, 124, DEV_ROWS_TOP + pos, 3, knob);
    }

    /* ---- the selected device, in one line ---- */
    const EchoDeviceRow* sel = &m->rows[m->selected];
    canvas_draw_line(canvas, 0, DEV_STRIP_RULE, 127, DEV_STRIP_RULE);
    canvas_set_font(canvas, FontSecondary);

    if(sel->group_size > 1u) {
        /* the interesting case: several addresses, one radio */
        snprintf(buf, sizeof(buf), "1 of %u MACs", (unsigned)sel->group_size);
    } else if(sel->randomized) {
        snprintf(buf, sizeof(buf), "random MAC");
    } else if(sel->vendor) {
        snprintf(buf, sizeof(buf), "%s", sel->vendor);
    } else {
        snprintf(buf, sizeof(buf), "real MAC");
    }
    canvas_draw_str(canvas, 2, DEV_STRIP_BASE, buf);

    /* The badge already carries the verdict, so the strip carries the number
     * behind it - and the two never disagree, because both come out of the
     * same expo_score() call. */
    snprintf(buf, sizeof(buf), "%u/100", (unsigned)sel->score);
    canvas_draw_str_aligned(canvas, 126, DEV_STRIP_BASE, AlignRight, AlignBottom, buf);
}

static bool dev_list_view_input(InputEvent* event, void* context) {
    DevListView* v = context;
    bool consumed = false;

    if(event->type == InputTypeShort || event->type == InputTypeRepeat) {
        if(event->key == InputKeyUp) {
            with_view_model(
                v->view,
                DevListModel * m,
                {
                    if(m->selected > 0) m->selected--;
                },
                true);
            consumed = true;
        } else if(event->key == InputKeyDown) {
            with_view_model(
                v->view,
                DevListModel * m,
                {
                    if(m->count && m->selected + 1 < m->count) m->selected++;
                },
                true);
            consumed = true;
        }
    }

    if(event->type == InputTypeShort && event->key == InputKeyOk) {
        uint8_t index = 0xFF;
        with_view_model(
            v->view,
            DevListModel * m,
            {
                if(m->count) index = m->rows[m->selected].index;
            },
            false);
        if(index != 0xFFu && v->ok_cb) v->ok_cb(v->ok_ctx, index);
        consumed = true;
    }

    return consumed;
}

DevListView* dev_list_view_alloc(void) {
    DevListView* v = malloc(sizeof(DevListView));
    v->ok_cb = NULL;
    v->ok_ctx = NULL;
    v->view = view_alloc();
    view_set_context(v->view, v);
    view_set_draw_callback(v->view, dev_list_view_draw);
    view_set_input_callback(v->view, dev_list_view_input);
    view_allocate_model(v->view, ViewModelTypeLocking, sizeof(DevListModel));
    return v;
}

void dev_list_view_free(DevListView* v) {
    furi_assert(v);
    view_free(v->view);
    free(v);
}

View* dev_list_view_get_view(DevListView* v) {
    furi_assert(v);
    return v->view;
}

void dev_list_view_set_ok_callback(DevListView* v, DevListViewCallback cb, void* context) {
    furi_assert(v);
    v->ok_cb = cb;
    v->ok_ctx = context;
}

void dev_list_view_update(
    DevListView* v,
    const EchoDeviceRow* rows,
    size_t count,
    bool connected) {
    furi_assert(v);
    with_view_model(
        v->view,
        DevListModel * m,
        {
            size_t n = count < DEV_LIST_MAX ? count : DEV_LIST_MAX;
            for(size_t i = 0; i < n; i++) m->rows[i] = rows[i];
            m->count = n;
            m->connected = connected;
            if(m->selected >= n) m->selected = n ? n - 1 : 0;
        },
        true);
}
