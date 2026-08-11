#include "../echo_i.h"

#include <stdio.h>

void echo_scene_report_on_enter(void* context) {
    EchoApp* app = context;
    Widget* widget = app->widget;
    char path[ECHO_REPORT_PATH_MAX];
    char line[ECHO_REPORT_PATH_MAX + 16];

    widget_reset(widget);

    EchoStats stats;
    echo_db_get_stats(app->db, &stats);

    if(stats.device_count == 0) {
        widget_add_string_element(
            widget, 64, 20, AlignCenter, AlignBottom, FontPrimary, "Nothing to report");
        widget_add_string_element(
            widget, 64, 36, AlignCenter, AlignBottom, FontSecondary, "Listen for a while first.");
        view_dispatcher_switch_to_view(app->view_dispatcher, EchoViewWidget);
        return;
    }

    bool ok = echo_report_save(app->storage, app->db, path, sizeof(path));

    if(ok) {
        widget_add_string_element(
            widget, 64, 14, AlignCenter, AlignBottom, FontPrimary, "Report saved");

        /* the path is long; show the file name and the folder separately */
        const char* name = strrchr(path, '/');
        snprintf(line, sizeof(line), "%s", name ? name + 1 : path);
        widget_add_string_element(widget, 64, 27, AlignCenter, AlignBottom, FontSecondary, line);
        widget_add_string_element(
            widget, 64, 37, AlignCenter, AlignBottom, FontSecondary, "in apps_data/echo");

        snprintf(
            line,
            sizeof(line),
            "%u devices, %u networks",
            (unsigned)stats.device_count,
            (unsigned)stats.ssid_count);
        widget_add_string_element(widget, 64, 50, AlignCenter, AlignBottom, FontSecondary, line);
        widget_add_string_element(
            widget, 64, 61, AlignCenter, AlignBottom, FontSecondary, "MACs and names are masked");
    } else {
        widget_add_string_element(
            widget, 64, 24, AlignCenter, AlignBottom, FontPrimary, "Could not save");
        widget_add_string_element(
            widget, 64, 40, AlignCenter, AlignBottom, FontSecondary, "Is the SD card in?");
    }

    view_dispatcher_switch_to_view(app->view_dispatcher, EchoViewWidget);
}

bool echo_scene_report_on_event(void* context, SceneManagerEvent event) {
    UNUSED(context);
    UNUSED(event);
    return false;
}

void echo_scene_report_on_exit(void* context) {
    EchoApp* app = context;
    widget_reset(app->widget);
}
