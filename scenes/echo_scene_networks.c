#include "../echo_i.h"

void echo_scene_networks_on_enter(void* context) {
    EchoApp* app = context;

    /* Keep listening while the list is open, so it fills in front of you. */
    echo_listen_start(app);
    view_dispatcher_switch_to_view(app->view_dispatcher, EchoViewNetList);
}

bool echo_scene_networks_on_event(void* context, SceneManagerEvent event) {
    EchoApp* app = context;
    bool consumed = false;

    if(event.type == SceneManagerEventTypeTick) {
        echo_demo_tick(app);

        size_t n = echo_db_copy_ssids(app->db, app->ssids, ECHO_MAX_SSIDS);
        net_list_view_update(app->net_list_view, app->ssids, n, echo_link_is_live(app));
        consumed = true;
    }
    return consumed;
}

void echo_scene_networks_on_exit(void* context) {
    UNUSED(context);
}
