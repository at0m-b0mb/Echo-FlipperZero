#pragma once

#include <furi.h>
#include <furi_hal.h>
#include <gui/gui.h>
#include <gui/modules/submenu.h>
#include <gui/modules/variable_item_list.h>
#include <gui/modules/widget.h>
#include <gui/scene_manager.h>
#include <gui/view_dispatcher.h>
#include <notification/notification.h>
#include <notification/notification_messages.h>
#include <storage/storage.h>

#include "echo_icons.h" // generated from icons/ by fbt

#include "helpers/demo_feed.h"
#include "helpers/echo_db.h"
#include "helpers/echo_report.h"
#include "helpers/exposure.h"
#include "helpers/probe_intel.h"
#include "helpers/uart_link.h"
#include "scenes/echo_scene.h"
#include "views/chamber_view.h"
#include "views/dev_list_view.h"
#include "views/dossier_view.h"
#include "views/explain_view.h"
#include "views/net_list_view.h"

#define ECHO_VERSION "1.0"

/** No frame from the board for this long and the link light goes hollow. */
#define ECHO_LINK_TIMEOUT_MS 3000u

typedef enum {
    EchoViewSubmenu,
    EchoViewChamber,
    EchoViewNetList,
    EchoViewDevList,
    EchoViewDossier,
    EchoViewExplain,
    EchoViewSettings,
    EchoViewWidget,
} EchoViewId;

typedef enum {
    EchoCustomEventProbe = 100,
    EchoCustomEventNewName,
    EchoCustomEventSensitive,
    EchoCustomEventLinked,
    EchoCustomEventOpenDossier,
} EchoCustomEvent;

/** What deserves a buzz. Anything noisier than this and the demo is unwatchable. */
typedef enum {
    EchoAlertOff = 0,
    EchoAlertSensitive, /* a home, a workplace, a clinic - the ones that matter */
    EchoAlertAnyName,
    EchoAlertCount,
} EchoAlertMode;

typedef struct {
    uint8_t channel_index; /* 0 = hop every channel, 1..13 = camp on one */
    uint8_t alert_index; /* EchoAlertMode */
    bool demo;
    bool sound;
    bool vibro;
    bool led;
} EchoSettings;

typedef struct {
    Gui* gui;
    ViewDispatcher* view_dispatcher;
    SceneManager* scene_manager;
    NotificationApp* notifications;
    Storage* storage;

    Submenu* submenu;
    VariableItemList* var_item_list;
    Widget* widget;

    ChamberView* chamber_view;
    NetListView* net_list_view;
    DevListView* dev_list_view;
    DossierView* dossier_view;
    ExplainView* explain_view;

    EchoDb* db;
    UartLink* uart;
    DemoFeed demo;
    uint32_t demo_started_tick;

    EchoSettings settings;

    volatile uint32_t last_rx_tick;
    bool esp_connected;
    char esp_version[16];
    uint8_t esp_channel;

    /** Which device the dossier is about. */
    uint8_t selected_device;

    /**
     * Scratch space for the scenes. A device row is fifty-odd bytes and there
     * can be forty of them; copying that onto the GUI thread's stack once a
     * tick is how you get a stack overflow on a quiet afternoon.
     */
    EchoBlip blips[CHAMBER_MAX_BLIPS];
    EchoFeedRow feed[CHAMBER_FEED_KEEP];
    EchoSsid ssids[ECHO_MAX_SSIDS];
    EchoDeviceRow dev_rows[ECHO_MAX_DEVICES];
} EchoApp;

/* ---- settings scene, exported so the rest of the app can read them ---- */
uint8_t echo_settings_channel(const EchoSettings* s);
const char* echo_settings_channel_label(uint8_t index);
const char* echo_settings_alert_label(uint8_t index);

/* ---- echo.c ---- */

/** Start the link (or the demo), and push the current channel to the board. */
void echo_listen_start(EchoApp* app);

/** Stop the link and give the USART back to the expansion service. */
void echo_listen_stop(EchoApp* app);

/** Pump the demo script. No-op unless demo mode is on. */
void echo_demo_tick(EchoApp* app);

/** True while the board has said something recently. */
bool echo_link_is_live(EchoApp* app);

void echo_notify_name(EchoApp* app); /* a network name nobody had heard yet */
void echo_notify_sensitive(EchoApp* app); /* ...and it was a home or a clinic */
void echo_notify_linked(EchoApp* app); /* two addresses proved to be one radio */
