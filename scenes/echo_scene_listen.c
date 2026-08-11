#include "../echo_i.h"

void echo_scene_listen_on_enter(void* context) {
    EchoApp* app = context;

    echo_listen_start(app);
    view_dispatcher_switch_to_view(app->view_dispatcher, EchoViewChamber);
}

bool echo_scene_listen_on_event(void* context, SceneManagerEvent event) {
    EchoApp* app = context;
    bool consumed = false;

    if(event.type == SceneManagerEventTypeCustom) {
        switch(event.event) {
        case EchoCustomEventSensitive:
            echo_notify_sensitive(app);
            consumed = true;
            break;
        case EchoCustomEventNewName:
            echo_notify_name(app);
            consumed = true;
            break;
        case EchoCustomEventLinked:
            echo_notify_linked(app);
            consumed = true;
            break;
        default:
            break;
        }

    } else if(event.type == SceneManagerEventTypeTick) {
        echo_demo_tick(app);

        EchoStats stats;
        echo_db_get_stats(app->db, &stats);

        size_t blips = echo_db_copy_blips(app->db, app->blips, CHAMBER_MAX_BLIPS);
        size_t feed = echo_db_copy_feed(app->db, app->feed, CHAMBER_FEED_KEEP);

        chamber_view_update(
            app->chamber_view,
            app->blips,
            blips,
            app->feed,
            feed,
            &stats,
            app->settings.demo ? 0u : app->esp_channel,
            echo_link_is_live(app),
            app->settings.demo);
        chamber_view_tick(app->chamber_view);

        consumed = true;
    }
    return consumed;
}

void echo_scene_listen_on_exit(void* context) {
    UNUSED(context);
    /* The link is deliberately left up: Networks and Devices keep listening,
     * and only the start menu takes the radio back down. */
}
