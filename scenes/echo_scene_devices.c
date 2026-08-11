#include "../echo_i.h"

static void echo_scene_devices_ok_cb(void* context, uint8_t device_index) {
    EchoApp* app = context;
    app->selected_device = device_index;
    view_dispatcher_send_custom_event(app->view_dispatcher, EchoCustomEventOpenDossier);
}

void echo_scene_devices_on_enter(void* context) {
    EchoApp* app = context;

    dev_list_view_set_ok_callback(app->dev_list_view, echo_scene_devices_ok_cb, app);
    echo_listen_start(app);
    view_dispatcher_switch_to_view(app->view_dispatcher, EchoViewDevList);
}

bool echo_scene_devices_on_event(void* context, SceneManagerEvent event) {
    EchoApp* app = context;
    bool consumed = false;

    if(event.type == SceneManagerEventTypeCustom) {
        if(event.event == EchoCustomEventOpenDossier) {
            scene_manager_next_scene(app->scene_manager, EchoSceneDossier);
            consumed = true;
        }

    } else if(event.type == SceneManagerEventTypeTick) {
        echo_demo_tick(app);

        size_t n = echo_db_copy_devices(app->db, app->dev_rows, ECHO_MAX_DEVICES);
        dev_list_view_update(app->dev_list_view, app->dev_rows, n, echo_link_is_live(app));
        consumed = true;
    }
    return consumed;
}

void echo_scene_devices_on_exit(void* context) {
    UNUSED(context);
}
