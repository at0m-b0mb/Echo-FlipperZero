#include "../echo_i.h"

static const char* const chan_labels[] =
    {"Hop", "1", "2", "3", "4", "5", "6", "7", "8", "9", "10", "11", "12", "13"};
#define CHAN_COUNT 14

/* Off is for the room where every phone is leaking and the buzzing never
 * stops; Places is the useful default; Any name is for a quiet room. */
static const char* const alert_labels[] = {"Off", "Places", "Any name"};

static const char* const on_off[] = {"OFF", "ON"};

typedef enum {
    SettingsItemChannel,
    SettingsItemAlerts,
    SettingsItemDemo,
    SettingsItemSound,
    SettingsItemVibro,
    SettingsItemLed,
    SettingsItemClear,
} SettingsItem;

/* ---- exported to the rest of the app ---- */

uint8_t echo_settings_channel(const EchoSettings* s) {
    return s->channel_index % CHAN_COUNT;
}

const char* echo_settings_channel_label(uint8_t index) {
    return chan_labels[index % CHAN_COUNT];
}

const char* echo_settings_alert_label(uint8_t index) {
    return alert_labels[index % (uint8_t)EchoAlertCount];
}

/* ---- item callbacks ---- */

static void chan_changed(VariableItem* item) {
    EchoApp* app = variable_item_get_context(item);
    uint8_t i = variable_item_get_current_value_index(item);
    app->settings.channel_index = i;
    variable_item_set_current_value_text(item, chan_labels[i]);
}

static void alert_changed(VariableItem* item) {
    EchoApp* app = variable_item_get_context(item);
    uint8_t i = variable_item_get_current_value_index(item);
    app->settings.alert_index = i;
    variable_item_set_current_value_text(item, alert_labels[i]);
}

static void demo_changed(VariableItem* item) {
    EchoApp* app = variable_item_get_context(item);
    uint8_t i = variable_item_get_current_value_index(item);
    app->settings.demo = (i != 0);
    variable_item_set_current_value_text(item, on_off[i]);

    /* Switching modes mid-session would mix a scripted room into a real one. */
    echo_listen_stop(app);
    echo_db_reset(app->db);
    demo_feed_init(&app->demo);
}

static void sound_changed(VariableItem* item) {
    EchoApp* app = variable_item_get_context(item);
    uint8_t i = variable_item_get_current_value_index(item);
    app->settings.sound = (i != 0);
    variable_item_set_current_value_text(item, on_off[i]);
}

static void vibro_changed(VariableItem* item) {
    EchoApp* app = variable_item_get_context(item);
    uint8_t i = variable_item_get_current_value_index(item);
    app->settings.vibro = (i != 0);
    variable_item_set_current_value_text(item, on_off[i]);
}

static void led_changed(VariableItem* item) {
    EchoApp* app = variable_item_get_context(item);
    uint8_t i = variable_item_get_current_value_index(item);
    app->settings.led = (i != 0);
    variable_item_set_current_value_text(item, on_off[i]);
}

static void settings_enter_cb(void* context, uint32_t index) {
    EchoApp* app = context;
    if(index == (uint32_t)SettingsItemClear) {
        echo_db_reset(app->db);
        demo_feed_init(&app->demo);
        app->selected_device = 0;
        notification_message(app->notifications, &sequence_blink_blue_10);
    }
}

/* ---- scene ---- */

void echo_scene_settings_on_enter(void* context) {
    EchoApp* app = context;
    VariableItemList* list = app->var_item_list;
    VariableItem* item;

    variable_item_list_reset(list);

    item = variable_item_list_add(list, "Wi-Fi channel", CHAN_COUNT, chan_changed, app);
    variable_item_set_current_value_index(item, app->settings.channel_index);
    variable_item_set_current_value_text(item, chan_labels[app->settings.channel_index]);

    item = variable_item_list_add(
        list, "Alert on", (uint8_t)EchoAlertCount, alert_changed, app);
    variable_item_set_current_value_index(item, app->settings.alert_index);
    variable_item_set_current_value_text(item, alert_labels[app->settings.alert_index]);

    item = variable_item_list_add(list, "Demo mode", 2, demo_changed, app);
    variable_item_set_current_value_index(item, app->settings.demo ? 1 : 0);
    variable_item_set_current_value_text(item, on_off[app->settings.demo ? 1 : 0]);

    item = variable_item_list_add(list, "Sound", 2, sound_changed, app);
    variable_item_set_current_value_index(item, app->settings.sound ? 1 : 0);
    variable_item_set_current_value_text(item, on_off[app->settings.sound ? 1 : 0]);

    item = variable_item_list_add(list, "Vibrate", 2, vibro_changed, app);
    variable_item_set_current_value_index(item, app->settings.vibro ? 1 : 0);
    variable_item_set_current_value_text(item, on_off[app->settings.vibro ? 1 : 0]);

    item = variable_item_list_add(list, "LED", 2, led_changed, app);
    variable_item_set_current_value_index(item, app->settings.led ? 1 : 0);
    variable_item_set_current_value_text(item, on_off[app->settings.led ? 1 : 0]);

    /* No values: a plain row you press OK on. */
    variable_item_list_add(list, "Forget everything", 0, NULL, app);
    variable_item_list_set_enter_callback(list, settings_enter_cb, app);

    view_dispatcher_switch_to_view(app->view_dispatcher, EchoViewSettings);
}

bool echo_scene_settings_on_event(void* context, SceneManagerEvent event) {
    UNUSED(context);
    UNUSED(event);
    return false;
}

void echo_scene_settings_on_exit(void* context) {
    EchoApp* app = context;
    variable_item_list_reset(app->var_item_list);
}
