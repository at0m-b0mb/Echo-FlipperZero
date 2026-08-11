#pragma once

#include <gui/view.h>

#define EXPLAIN_PAGES 4

typedef struct ExplainView ExplainView;

ExplainView* explain_view_alloc(void);
void explain_view_free(ExplainView* view);
View* explain_view_get_view(ExplainView* view);

/** Advance the animation. Called once per GUI tick. */
void explain_view_tick(ExplainView* view);

/** Back to page one. Called when the scene opens. */
void explain_view_reset(ExplainView* view);
