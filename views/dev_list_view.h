#pragma once

#include <gui/view.h>

#include "../helpers/echo_db.h"

#define DEV_LIST_MAX     ECHO_MAX_DEVICES
#define DEV_LIST_VISIBLE 4
#define DEV_ROW_H        11
#define DEV_ROWS_TOP     10
#define DEV_STRIP_RULE   54
#define DEV_STRIP_BASE   62

typedef struct DevListView DevListView;
typedef void (*DevListViewCallback)(void* context, uint8_t device_index);

DevListView* dev_list_view_alloc(void);
void dev_list_view_free(DevListView* view);
View* dev_list_view_get_view(DevListView* view);

/** Fired on OK with the stable db index of the highlighted device. */
void dev_list_view_set_ok_callback(DevListView* view, DevListViewCallback cb, void* context);

void dev_list_view_update(
    DevListView* view,
    const EchoDeviceRow* rows,
    size_t count,
    bool connected);
