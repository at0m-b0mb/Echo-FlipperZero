#include "../echo_i.h"

void echo_scene_explain_on_enter(void* context) {
    EchoApp* app = context;

    explain_view_reset(app->explain_view);
    view_dispatcher_switch_to_view(app->view_dispatcher, EchoViewExplain);
}

bool echo_scene_explain_on_event(void* context, SceneManagerEvent event) {
    EchoApp* app = context;
    bool consumed = false;

    if(event.type == SceneManagerEventTypeTick) {
        explain_view_tick(app->explain_view);
        consumed = true;
    }
    return consumed;
}

void echo_scene_explain_on_exit(void* context) {
    UNUSED(context);
}
