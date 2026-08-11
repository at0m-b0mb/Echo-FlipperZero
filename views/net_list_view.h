#pragma once

#include <gui/view.h>

#include "../helpers/echo_db.h"

#define NET_LIST_MAX     ECHO_MAX_SSIDS
#define NET_LIST_VISIBLE 4
#define NET_ROW_H        11
#define NET_ROWS_TOP     10
#define NET_STRIP_RULE   54
#define NET_STRIP_BASE   62

typedef struct NetListView NetListView;

NetListView* net_list_view_alloc(void);
void net_list_view_free(NetListView* view);
View* net_list_view_get_view(NetListView* view);

void net_list_view_update(NetListView* view, const EchoSsid* rows, size_t count, bool connected);
