#pragma once

/**
 * echo_db - everything the room has said, kept in one mutex-guarded table.
 *
 * Probes arrive on the UART worker thread and every screen reads from the GUI
 * thread, so the whole surface below takes the lock. Callers get copies, never
 * pointers into the table.
 *
 * Nothing is ever evicted while the app is open: device indices stay valid, so
 * a scene can remember "the user picked device 7" and still mean the same
 * radio a minute later.
 */

#include <furi.h>

#include "exposure.h"
#include "probe_intel.h"

#define ECHO_MAX_DEVICES 40
#define ECHO_MAX_SSIDS   48
#define ECHO_MAX_FEED    24
/** Named networks remembered per device. Beyond this the count still climbs. */
#define ECHO_DEV_SSIDS   10
#define ECHO_NO_GROUP    0xFFu

/**
 * How many frames may go unheard before a continuing sequence counter stops
 * counting as evidence. A sniffer hopping thirteen channels misses a lot, so
 * this is generous - but not so generous that any two numbers look related.
 */
#define ECHO_SEQ_WINDOW 200u

/** A ripple stays on the chamber screen this long after the probe that made it. */
#define ECHO_RIPPLE_MS 1400u

/** A network name somebody asked for. */
typedef struct {
    char name[PI_SSID_MAX];
    uint8_t cat; /* PiCat */
    uint8_t flags; /* PiFlag */
    uint16_t hits;
    int8_t best_rssi;
    uint32_t first_tick;
    uint32_t last_tick;
    uint64_t asker_mask; /* bit per device index */
} EchoSsid;

/** One radio, as far as Echo can tell. */
typedef struct {
    uint8_t mac[6];
    uint32_t fp; /* probe fingerprint, 0 when the board sent none */
    uint16_t last_seq;
    uint16_t probes;
    uint16_t named_probes;
    int8_t rssi;
    int8_t best_rssi;
    uint8_t channel;
    uint32_t first_tick;
    uint32_t last_tick;
    uint8_t ssid_idx[ECHO_DEV_SSIDS];
    uint8_t ssid_n; /* entries used in ssid_idx */
    uint16_t named_total; /* distinct names, even past ssid_idx */
    bool randomized;
    uint8_t link_group; /* ECHO_NO_GROUP, or the index of the first of its kind */
    uint8_t link_conf; /* ExpoLink */
} EchoDevice;

/** A row of the live feed. */
typedef struct {
    uint8_t mac[6];
    char ssid[PI_SSID_MAX];
    int8_t rssi;
    uint8_t channel;
    uint8_t cat;
    bool randomized;
    uint32_t tick;
} EchoFeedRow;

/** A device as the chamber screen wants it: a place, a distance, an age. */
typedef struct {
    /**
     * The device's own index in the table. The chamber turns this into a
     * bearing; nothing about a MAC address points anywhere real, so the screen
     * is free to choose an arrangement that can actually be read.
     */
    uint8_t slot;
    int8_t rssi;
    uint16_t age_ms; /* since its last probe, clamped */
    bool named; /* it has given a network name away */
    bool randomized;
} EchoBlip;

/** A device as the device list wants it: already scored, already sorted. */
typedef struct {
    uint8_t index; /* stable db index, for the dossier */
    uint8_t mac[6];
    int8_t rssi;
    uint16_t probes;
    uint16_t named;
    uint8_t score;
    uint8_t grade; /* ExpoGrade */
    bool randomized;
    uint8_t link_conf;
    uint8_t group_size;
    const char* vendor; /* static string, or NULL */
    char top_ssid[PI_SSID_MAX]; /* the most telling name it leaked */
    uint8_t top_cat;
} EchoDeviceRow;

typedef struct {
    size_t device_count;
    size_t leaking_count; /* devices that named at least one network */
    size_t silent_count; /* randomized address, nothing named */
    size_t ssid_count;
    size_t place_count; /* distinct categories, excluding Unknown */
    size_t linked_groups; /* radios caught behind more than one address */
    uint32_t probe_total;
    uint32_t named_total;
    uint32_t last_probe_tick;
} EchoStats;

/** What just happened, worst first. Drives the alert. */
typedef enum {
    EchoEventNone = 0,
    EchoEventNewDevice,
    EchoEventNewName, /* a network name nobody had heard yet */
    EchoEventSensitive, /* ...and it was a home, a workplace or a clinic */
    EchoEventLinked, /* two addresses proved to be one radio */
} EchoEvent;

typedef struct EchoDb EchoDb;

EchoDb* echo_db_alloc(void);
void echo_db_free(EchoDb* db);
void echo_db_reset(EchoDb* db);

/**
 * Fold one probe request into the table.
 *
 * `ssid` may be empty - a broadcast probe, which is the polite kind and is
 * counted but reveals nothing. `fp` is the fingerprint the sniffer computed
 * over the probe's information elements; pass 0 if there is none, and the
 * device will simply never be linked to another address.
 */
EchoEvent echo_db_on_probe(
    EchoDb* db,
    const uint8_t mac[6],
    uint8_t channel,
    int8_t rssi,
    uint16_t seq,
    uint32_t fp,
    const char* ssid);

void echo_db_get_stats(EchoDb* db, EchoStats* out);

/** Chamber blips, strongest signal first. Returns how many were written. */
size_t echo_db_copy_blips(EchoDb* db, EchoBlip* out, size_t max);

/** Live feed, newest first. */
size_t echo_db_copy_feed(EchoDb* db, EchoFeedRow* out, size_t max);

/** Networks, most telling first: category weight, then how often it was asked. */
size_t echo_db_copy_ssids(EchoDb* db, EchoSsid* out, size_t max);

/** Devices, most exposed first. */
size_t echo_db_copy_devices(EchoDb* db, EchoDeviceRow* out, size_t max);

/** How many devices asked for a given network. */
uint8_t echo_db_ssid_askers(const EchoSsid* ssid);

/** One device by its stable index. False if the index is past the end. */
bool echo_db_get_device(EchoDb* db, uint8_t index, EchoDevice* out);

/** The scoring input for one device, ready for expo_score(). */
bool echo_db_device_exposure(EchoDb* db, uint8_t index, ExpoInput* out);

/** The names one device leaked, most telling first. */
size_t echo_db_device_ssids(EchoDb* db, uint8_t index, EchoSsid* out, size_t max);

/** How many addresses belong to the same radio as this device (1 if unlinked). */
uint8_t echo_db_group_size(EchoDb* db, uint8_t index);
