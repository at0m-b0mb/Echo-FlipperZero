#pragma once

/**
 * probe_intel - what a probe request gives away, independent of the Flipper.
 *
 * Two identifiers ride on every probe request: the network name the device is
 * calling out for, and the MAC address it calls from. This module turns each of
 * them into something a human can read.
 *
 * Deliberately free of Flipper headers so the whole thing compiles and runs on
 * a laptop under `make -C test`.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Longest SSID the 802.11 spec allows, plus the terminator. */
#define PI_SSID_MAX 33

/* ------------------------------------------------------------------ SSIDs */

/**
 * What kind of place a network name points at. The order matters only in that
 * PiCatUnknown must stay at zero; the sensitivity ranking lives in
 * pi_cat_weight(), not here.
 */
typedef enum {
    PiCatUnknown = 0,
    PiCatHome, /* an ISP's default router name, or a household's own */
    PiCatHotspot, /* someone's phone sharing its connection */
    PiCatHotel,
    PiCatAirport,
    PiCatAirline, /* in-flight wi-fi */
    PiCatTransit, /* trains, buses, metros, ferries */
    PiCatCafe,
    PiCatRetail,
    PiCatGym,
    PiCatMedical,
    PiCatEducation,
    PiCatWorkplace,
    PiCatVehicle, /* a car's own hotspot */
    PiCatDevice, /* printers, TVs, cameras, Chromecasts */
    PiCatPublic, /* carrier-wide free wi-fi: xfinitywifi, attwifi, BTWiFi */
    PiCatCount,
} PiCat;

/** Extra facts about a name that the category alone does not carry. */
typedef enum {
    PiFlagNone = 0,
    /** The name contains what looks like a person's name ("Kailash's iPhone"). */
    PiFlagPersonalName = 1 << 0,
    /**
     * Distinctive enough that a public wardriving database would very likely
     * resolve it to one street address. "NETGEAR" would not; "Sharma_Home_5G"
     * would.
     */
    PiFlagGeolocatable = 1 << 1,
} PiFlag;

typedef struct {
    PiCat cat;
    uint8_t flags; /* bitwise OR of PiFlag */
} PiSsidInfo;

/** Classify a network name. `ssid` may be empty; an empty name is Unknown. */
PiSsidInfo pi_classify(const char* ssid);

/** Short label for a category, e.g. "Hotel". Never NULL. */
const char* pi_cat_label(PiCat cat);

/** Single-character tag used in dense list rows, e.g. 'H' for Home. */
char pi_cat_tag(PiCat cat);

/**
 * One line saying what this category gives away, written for the person whose
 * phone it is: "Where you sleep." Never NULL.
 */
const char* pi_cat_reveals(PiCat cat);

/**
 * How much this category is worth to somebody watching, 0-5. Home and medical
 * top the scale; a coffee shop is near the bottom.
 */
uint8_t pi_cat_weight(PiCat cat);

/* -------------------------------------------------------------------- MACs */

/**
 * True when the address is locally administered - the bit every modern phone
 * sets on the throwaway MACs it invents for scanning. Its presence is a good
 * sign, not a bad one.
 */
bool pi_mac_is_randomized(const uint8_t mac[6]);

/**
 * Vendor behind a real (globally assigned) MAC, or NULL when the prefix is not
 * in the table - which is most of the time, because the table is small on
 * purpose. Always returns NULL for a randomized address, since the OUI of an
 * invented MAC belongs to nobody.
 */
const char* pi_mac_vendor(const uint8_t mac[6]);

/** Render a MAC as "AA:BB:CC:DD:EE:FF". `out` needs 18 bytes. */
void pi_mac_str(const uint8_t mac[6], char* out, size_t out_len);

/**
 * Render a MAC with the device half masked: "AA:BB:CC:**:**:**". Used anywhere
 * the output leaves the device - reports, screenshots - so Echo does not become
 * the thing it warns about. `out` needs 18 bytes.
 */
void pi_mac_str_masked(const uint8_t mac[6], char* out, size_t out_len);

/**
 * Mask a network name down to a hint: "Hilton_Honors" -> "Hil...". Keeps a
 * saved report readable without writing down where its subject sleeps.
 */
void pi_ssid_mask(const char* ssid, char* out, size_t out_len);

/* ------------------------------------------------------------ fingerprints */

/**
 * FNV-1a over a byte range. The ESP32 uses this to fold a probe's information
 * elements into one 32-bit fingerprint; the Flipper only compares them.
 */
uint32_t pi_fnv1a(const uint8_t* data, size_t len, uint32_t seed);

/** Seed for pi_fnv1a() when starting a fresh hash. */
#define PI_FNV_SEED 0x811C9DC5u

/**
 * Does sequence number `next` plausibly continue from `prev`?
 *
 * 802.11 sequence counters are 12 bits and wrap. Several randomization
 * implementations forget to reset the counter when they change MAC, so a new
 * address whose sequence carries on from an old one is the same radio - which
 * is a far stronger claim than a matching fingerprint.
 *
 * `window` is how many frames may go unheard in between; a sniffer that hops
 * channels misses plenty, so keep it generous. Equal values are rejected: a
 * retransmission is not progress.
 */
bool pi_seq_continues(uint16_t prev, uint16_t next, uint16_t window);

#ifdef __cplusplus
}
#endif
