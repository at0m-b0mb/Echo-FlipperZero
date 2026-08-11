#include "../echo_i.h"

typedef enum {
    StartIndexListen,
    StartIndexNetworks,
    StartIndexDevices,
    StartIndexExplain,
    StartIndexReport,
    StartIndexSettings,
    StartIndexAbout,
} StartIndex;

static void echo_scene_start_submenu_cb(void* context, uint32_t index) {
    EchoApp* app = context;
    view_dispatcher_send_custom_event(app->view_dispatcher, index);
}

void echo_scene_start_on_enter(void* context) {
    EchoApp* app = context;
    Submenu* submenu = app->submenu;

    submenu_reset(submenu);
    submenu_set_header(submenu, "Echo");
    submenu_add_item(submenu, "Listen", StartIndexListen, echo_scene_start_submenu_cb, app);
    submenu_add_item(submenu, "Networks", StartIndexNetworks, echo_scene_start_submenu_cb, app);
    submenu_add_item(submenu, "Devices", StartIndexDevices, echo_scene_start_submenu_cb, app);
    submenu_add_item(
        submenu, "How it works", StartIndexExplain, echo_scene_start_submenu_cb, app);
    submenu_add_item(submenu, "Save report", StartIndexReport, echo_scene_start_submenu_cb, app);
    submenu_add_item(submenu, "Settings", StartIndexSettings, echo_scene_start_submenu_cb, app);
    submenu_add_item(submenu, "About", StartIndexAbout, echo_scene_start_submenu_cb, app);

    submenu_set_selected_item(
        submenu, scene_manager_get_scene_state(app->scene_manager, EchoSceneStart));

    /*
     * Back at the menu means nothing is listening. Dropping the link here (and
     * only here) is what lets Listen, Networks and Devices pass the radio
     * between them without any of them having to know about the others.
     */
    echo_listen_stop(app);

    view_dispatcher_switch_to_view(app->view_dispatcher, EchoViewSubmenu);
}

bool echo_scene_start_on_event(void* context, SceneManagerEvent event) {
    EchoApp* app = context;
    bool consumed = false;

    if(event.type == SceneManagerEventTypeCustom) {
        scene_manager_set_scene_state(app->scene_manager, EchoSceneStart, event.event);
        switch(event.event) {
        case StartIndexListen:
            scene_manager_next_scene(app->scene_manager, EchoSceneListen);
            consumed = true;
            break;
        case StartIndexNetworks:
            scene_manager_next_scene(app->scene_manager, EchoSceneNetworks);
            consumed = true;
            break;
        case StartIndexDevices:
            scene_manager_next_scene(app->scene_manager, EchoSceneDevices);
            consumed = true;
            break;
        case StartIndexExplain:
            scene_manager_next_scene(app->scene_manager, EchoSceneExplain);
            consumed = true;
            break;
        case StartIndexReport:
            scene_manager_next_scene(app->scene_manager, EchoSceneReport);
            consumed = true;
            break;
        case StartIndexSettings:
            scene_manager_next_scene(app->scene_manager, EchoSceneSettings);
            consumed = true;
            break;
        case StartIndexAbout:
            scene_manager_next_scene(app->scene_manager, EchoSceneAbout);
            consumed = true;
            break;
        default:
            break;
        }
    }
    return consumed;
}

void echo_scene_start_on_exit(void* context) {
    EchoApp* app = context;
    submenu_reset(app->submenu);
}
