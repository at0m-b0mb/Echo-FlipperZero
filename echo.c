#include "echo_i.h"

#include <stdio.h>
#include <string.h>

/* ------------------------------------------------------ alert vocabulary */

/* Only the 0 and 255 levels are exported for each channel, so "amber" is red
 * and green both hard on rather than a mixed level. */
static const NotificationSequence seq_led_amber = {
    &message_red_255,
    &message_green_255,
    &message_delay_100,
    &message_red_0,
    &message_green_0,
    NULL,
};
static const NotificationSequence seq_led_blue = {
    &message_blue_255,
    &message_delay_100,
    &message_blue_0,
    NULL,
};
static const NotificationSequence seq_vibro_short = {
    &message_vibro_on,
    &message_delay_50,
    &message_vibro_off,
    NULL,
};
static const NotificationSequence seq_snd_name = {
    &message_note_c5,
    &message_delay_50,
    &message_sound_off,
    NULL,
};
static const NotificationSequence seq_snd_sensitive = {
    &message_note_e5,
    &message_delay_50,
    &message_note_a5,
    &message_delay_50,
    &message_sound_off,
    NULL,
};
static const NotificationSequence seq_snd_linked = {
    &message_note_a5,
    &message_delay_50,
    &message_note_e5,
    &message_delay_50,
    &message_note_c5,
    &message_delay_50,
    &message_sound_off,
    NULL,
};

void echo_notify_name(EchoApp* app) {
    furi_assert(app);
    if(app->settings.alert_index != (uint8_t)EchoAlertAnyName) return;
    if(app->settings.led) notification_message(app->notifications, &seq_led_blue);
    if(app->settings.sound) notification_message(app->notifications, &seq_snd_name);
}

void echo_notify_sensitive(EchoApp* app) {
    furi_assert(app);
    if(app->settings.alert_index == (uint8_t)EchoAlertOff) return;
    if(app->settings.led) notification_message(app->notifications, &seq_led_amber);
    if(app->settings.vibro) notification_message(app->notifications, &seq_vibro_short);
    if(app->settings.sound) notification_message(app->notifications, &seq_snd_sensitive);
}

void echo_notify_linked(EchoApp* app) {
    furi_assert(app);
    if(app->settings.alert_index == (uint8_t)EchoAlertOff) return;
    if(app->settings.led) notification_message(app->notifications, &seq_led_blue);
    if(app->settings.sound) notification_message(app->notifications, &seq_snd_linked);
}

/* ------------------------------------------------------------ the link */

bool echo_link_is_live(EchoApp* app) {
    furi_assert(app);
    if(app->settings.demo) return true;
    if(!app->esp_connected) return false;
    return (uint32_t)(furi_get_tick() - app->last_rx_tick) < ECHO_LINK_TIMEOUT_MS;
}

void echo_listen_start(EchoApp* app) {
    furi_assert(app);

    if(app->settings.demo) {
        /* No radio to arm. Reset the script so every demo starts the same way. */
        demo_feed_init(&app->demo);
        app->demo_started_tick = furi_get_tick();
        return;
    }

    if(!uart_link_is_running(app->uart)) uart_link_start(app->uart);

    char cmd[16];
    uart_link_send_command(app->uart, "START\n");
    snprintf(cmd, sizeof(cmd), "CHAN:%u\n", echo_settings_channel(&app->settings));
    uart_link_send_command(app->uart, cmd);
    uart_link_send_command(app->uart, "PING\n");
}

void echo_listen_stop(EchoApp* app) {
    furi_assert(app);
    if(uart_link_is_running(app->uart)) {
        uart_link_send_command(app->uart, "STOP\n");
        uart_link_stop(app->uart);
    }
    app->esp_connected = false;
}

/* ----------------------------------------------------- one probe, one path */

/*
 * Everything funnels through here: the UART worker and the demo script hand
 * over identical arguments, so there is exactly one place where a probe turns
 * into a row, a score and possibly a buzz.
 */
static void echo_on_probe(
    void* context,
    const uint8_t mac[6],
    uint8_t channel,
    int8_t rssi,
    uint16_t seq,
    uint32_t fingerprint,
    const char* ssid) {
    EchoApp* app = context;
    app->last_rx_tick = furi_get_tick();
    if(channel) app->esp_channel = channel;

    EchoEvent event = echo_db_on_probe(app->db, mac, channel, rssi, seq, fingerprint, ssid);

    uint32_t custom = 0;
    switch(event) {
    case EchoEventLinked:
        custom = EchoCustomEventLinked;
        break;
    case EchoEventSensitive:
        custom = EchoCustomEventSensitive;
        break;
    case EchoEventNewName:
        custom = EchoCustomEventNewName;
        break;
    default:
        break;
    }

    /* The worker thread must not touch the GUI, so the scene gets told instead. */
    if(custom) view_dispatcher_send_custom_event(app->view_dispatcher, custom);
}

static void echo_on_hello(void* context, const char* version) {
    EchoApp* app = context;
    app->last_rx_tick = furi_get_tick();
    app->esp_connected = true;
    if(version) {
        strncpy(app->esp_version, version, sizeof(app->esp_version) - 1);
        app->esp_version[sizeof(app->esp_version) - 1] = '\0';
    }
}

static void echo_on_stat(void* context, uint32_t heard, uint8_t channel) {
    EchoApp* app = context;
    UNUSED(heard);
    app->last_rx_tick = furi_get_tick();
    app->esp_connected = true;
    app->esp_channel = channel;
}

void echo_demo_tick(EchoApp* app) {
    furi_assert(app);
    if(!app->settings.demo) return;
    demo_feed_step(&app->demo, furi_get_tick(), echo_on_probe, app);
}

/* -------------------------------------------------- view dispatcher plumbing */

static bool echo_custom_event_callback(void* context, uint32_t event) {
    EchoApp* app = context;
    return scene_manager_handle_custom_event(app->scene_manager, event);
}

static bool echo_back_event_callback(void* context) {
    EchoApp* app = context;
    return scene_manager_handle_back_event(app->scene_manager);
}

static void echo_tick_event_callback(void* context) {
    EchoApp* app = context;
    scene_manager_handle_tick_event(app->scene_manager);
}

/* ---------------------------------------------------------------- lifecycle */

static EchoApp* echo_app_alloc(void) {
    EchoApp* app = malloc(sizeof(EchoApp));
    memset(app, 0, sizeof(EchoApp));

    app->gui = furi_record_open(RECORD_GUI);
    app->notifications = furi_record_open(RECORD_NOTIFICATION);
    app->storage = furi_record_open(RECORD_STORAGE);

    app->view_dispatcher = view_dispatcher_alloc();
    app->scene_manager = scene_manager_alloc(&echo_scene_handlers, app);

    view_dispatcher_set_event_callback_context(app->view_dispatcher, app);
    view_dispatcher_set_custom_event_callback(app->view_dispatcher, echo_custom_event_callback);
    view_dispatcher_set_navigation_event_callback(app->view_dispatcher, echo_back_event_callback);
    view_dispatcher_set_tick_event_callback(app->view_dispatcher, echo_tick_event_callback, 100);

    /* defaults: hop every channel, buzz only for the names that matter */
    app->settings.channel_index = 0;
    app->settings.alert_index = (uint8_t)EchoAlertSensitive;
    app->settings.demo = false;
    app->settings.sound = true;
    app->settings.vibro = true;
    app->settings.led = true;
    app->selected_device = 0;

    app->db = echo_db_alloc();
    app->uart = uart_link_alloc();
    uart_link_set_callbacks(app->uart, echo_on_probe, echo_on_hello, echo_on_stat, app);
    demo_feed_init(&app->demo);

    app->submenu = submenu_alloc();
    view_dispatcher_add_view(app->view_dispatcher, EchoViewSubmenu, submenu_get_view(app->submenu));

    app->var_item_list = variable_item_list_alloc();
    view_dispatcher_add_view(
        app->view_dispatcher, EchoViewSettings, variable_item_list_get_view(app->var_item_list));

    app->widget = widget_alloc();
    view_dispatcher_add_view(app->view_dispatcher, EchoViewWidget, widget_get_view(app->widget));

    app->chamber_view = chamber_view_alloc();
    view_dispatcher_add_view(
        app->view_dispatcher, EchoViewChamber, chamber_view_get_view(app->chamber_view));

    app->net_list_view = net_list_view_alloc();
    view_dispatcher_add_view(
        app->view_dispatcher, EchoViewNetList, net_list_view_get_view(app->net_list_view));

    app->dev_list_view = dev_list_view_alloc();
    view_dispatcher_add_view(
        app->view_dispatcher, EchoViewDevList, dev_list_view_get_view(app->dev_list_view));

    app->dossier_view = dossier_view_alloc();
    view_dispatcher_add_view(
        app->view_dispatcher, EchoViewDossier, dossier_view_get_view(app->dossier_view));

    app->explain_view = explain_view_alloc();
    view_dispatcher_add_view(
        app->view_dispatcher, EchoViewExplain, explain_view_get_view(app->explain_view));

    view_dispatcher_attach_to_gui(app->view_dispatcher, app->gui, ViewDispatcherTypeFullscreen);

    return app;
}

static void echo_app_free(EchoApp* app) {
    furi_assert(app);

    /* the radio first, so the USART goes back to the expansion service */
    uart_link_stop(app->uart);
    uart_link_free(app->uart);

    view_dispatcher_remove_view(app->view_dispatcher, EchoViewSubmenu);
    view_dispatcher_remove_view(app->view_dispatcher, EchoViewSettings);
    view_dispatcher_remove_view(app->view_dispatcher, EchoViewWidget);
    view_dispatcher_remove_view(app->view_dispatcher, EchoViewChamber);
    view_dispatcher_remove_view(app->view_dispatcher, EchoViewNetList);
    view_dispatcher_remove_view(app->view_dispatcher, EchoViewDevList);
    view_dispatcher_remove_view(app->view_dispatcher, EchoViewDossier);
    view_dispatcher_remove_view(app->view_dispatcher, EchoViewExplain);

    submenu_free(app->submenu);
    variable_item_list_free(app->var_item_list);
    widget_free(app->widget);
    chamber_view_free(app->chamber_view);
    net_list_view_free(app->net_list_view);
    dev_list_view_free(app->dev_list_view);
    dossier_view_free(app->dossier_view);
    explain_view_free(app->explain_view);

    view_dispatcher_free(app->view_dispatcher);
    scene_manager_free(app->scene_manager);

    echo_db_free(app->db);

    furi_record_close(RECORD_STORAGE);
    furi_record_close(RECORD_NOTIFICATION);
    furi_record_close(RECORD_GUI);

    free(app);
}

int32_t echo_app(void* p) {
    UNUSED(p);
    EchoApp* app = echo_app_alloc();
    scene_manager_next_scene(app->scene_manager, EchoSceneStart);
    view_dispatcher_run(app->view_dispatcher);
    echo_app_free(app);
    return 0;
}
