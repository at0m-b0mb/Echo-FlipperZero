#include "../echo_i.h"

/* Refresh the card from the table. Cheap enough to redo every tick, which is
 * what keeps the numbers moving while the device is still talking. */
static void echo_scene_dossier_refresh(EchoApp* app) {
    EchoDevice device;
    if(!echo_db_get_device(app->db, app->selected_device, &device)) return;

    ExpoInput input;
    if(!echo_db_device_exposure(app->db, app->selected_device, &input)) return;
    ExpoResult result = expo_score(&input);

    EchoSsid names[DOSSIER_MAX_SSIDS];
    size_t n = echo_db_device_ssids(app->db, app->selected_device, names, DOSSIER_MAX_SSIDS);

    dossier_view_update(
        app->dossier_view,
        &device,
        &result,
        names,
        n,
        echo_db_group_size(app->db, app->selected_device));
}

void echo_scene_dossier_on_enter(void* context) {
    EchoApp* app = context;

    dossier_view_reset(app->dossier_view);
    echo_scene_dossier_refresh(app);
    view_dispatcher_switch_to_view(app->view_dispatcher, EchoViewDossier);
}

bool echo_scene_dossier_on_event(void* context, SceneManagerEvent event) {
    EchoApp* app = context;
    bool consumed = false;

    if(event.type == SceneManagerEventTypeTick) {
        echo_demo_tick(app);
        echo_scene_dossier_refresh(app);
        consumed = true;
    }
    return consumed;
}

void echo_scene_dossier_on_exit(void* context) {
    UNUSED(context);
}
