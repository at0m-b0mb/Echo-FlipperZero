#include "../echo_i.h"

void echo_scene_about_on_enter(void* context) {
    EchoApp* app = context;
    Widget* widget = app->widget;

    widget_reset(widget);
    widget_add_icon_element(widget, 2, 2, &I_ripple_10px);
    widget_add_string_element(
        widget, 16, 2, AlignLeft, AlignTop, FontPrimary, "Echo v" ECHO_VERSION);
    widget_add_string_element(
        widget, 16, 14, AlignLeft, AlignTop, FontSecondary, "What your phone shouts");

    widget_add_text_scroll_element(
        widget,
        0,
        26,
        128,
        38,
        "Phones look for networks they\n"
        "have joined before by calling\n"
        "out the name. Hotels, gyms,\n"
        "clinics, the router at home.\n"
        "Echo listens and reads the\n"
        "list back to you.\n"
        " \n"
        "Needs an ESP32 board on GPIO\n"
        "13/14 running the Echo sniffer\n"
        "firmware. No board? Turn on\n"
        "Demo in Settings.\n"
        " \n"
        "What it CANNOT do:\n"
        "- say whose phone it is\n"
        "- turn a name into a map pin;\n"
        "  it only says what kind of\n"
        "  place the name sounds like\n"
        "- tell two identical phone\n"
        "  models apart by fingerprint\n"
        "  alone - that is why a match\n"
        "  reads Likely, and only a\n"
        "  continuing frame counter\n"
        "  reads Confirmed\n"
        " \n"
        "A modern phone with private\n"
        "Wi-Fi addresses turned on and\n"
        "no stale saved networks gives\n"
        "away nothing here, and Echo\n"
        "will say so. That is the point.\n"
        " \n"
        "Receive only. Echo never\n"
        "transmits, never associates\n"
        "and never deauthenticates.\n"
        "Probe requests are broadcast\n"
        "in the clear, but recording\n"
        "them may still be regulated\n"
        "where you live. Use it on\n"
        "yourself and on people who\n"
        "asked you to.\n"
        " \n"
        "by at0m-b0mb\n"
        "at0m-b0mb/Echo-FlipperZero");

    view_dispatcher_switch_to_view(app->view_dispatcher, EchoViewWidget);
}

bool echo_scene_about_on_event(void* context, SceneManagerEvent event) {
    UNUSED(context);
    UNUSED(event);
    return false;
}

void echo_scene_about_on_exit(void* context) {
    EchoApp* app = context;
    widget_reset(app->widget);
}
