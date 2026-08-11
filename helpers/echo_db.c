#include "echo_db.h"

#include <string.h>

struct EchoDb {
    FuriMutex* mutex;

    EchoDevice devices[ECHO_MAX_DEVICES];
    size_t device_count;

    EchoSsid ssids[ECHO_MAX_SSIDS];
    size_t ssid_count;

    EchoFeedRow feed[ECHO_MAX_FEED];
    size_t feed_count; /* entries written, saturating at ECHO_MAX_FEED */
    size_t feed_head; /* next slot to write */

    uint32_t probe_total;
    uint32_t named_total;
    uint32_t last_probe_tick;
};

/* ------------------------------------------------------------------ setup */

EchoDb* echo_db_alloc(void) {
    EchoDb* db = malloc(sizeof(EchoDb));
    memset(db, 0, sizeof(EchoDb));
    db->mutex = furi_mutex_alloc(FuriMutexTypeNormal);
    return db;
}

void echo_db_free(EchoDb* db) {
    furi_assert(db);
    furi_mutex_free(db->mutex);
    free(db);
}

static void echo_db_clear(EchoDb* db) {
    memset(db->devices, 0, sizeof(db->devices));
    memset(db->ssids, 0, sizeof(db->ssids));
    memset(db->feed, 0, sizeof(db->feed));
    db->device_count = 0;
    db->ssid_count = 0;
    db->feed_count = 0;
    db->feed_head = 0;
    db->probe_total = 0;
    db->named_total = 0;
    db->last_probe_tick = 0;
}

void echo_db_reset(EchoDb* db) {
    furi_assert(db);
    furi_mutex_acquire(db->mutex, FuriWaitForever);
    echo_db_clear(db);
    furi_mutex_release(db->mutex);
}

/* ------------------------------------------------------------- small parts */

static bool mac_eq(const uint8_t* a, const uint8_t* b) {
    return memcmp(a, b, 6) == 0;
}

static int8_t max_rssi(int8_t a, int8_t b) {
    return (a > b) ? a : b;
}

static uint8_t popcount64(uint64_t v) {
    uint8_t n = 0;
    while(v) {
        v &= (v - 1u);
        n++;
    }
    return n;
}

uint8_t echo_db_ssid_askers(const EchoSsid* ssid) {
    if(!ssid) return 0;
    return popcount64(ssid->asker_mask);
}

/* ------------------------------------------------------------ table lookup */

/* Caller holds the lock. Returns index, or -1 when the table is full. */
static int find_or_add_device(EchoDb* db, const uint8_t mac[6], uint32_t tick) {
    for(size_t i = 0; i < db->device_count; i++) {
        if(mac_eq(db->devices[i].mac, mac)) return (int)i;
    }
    if(db->device_count >= ECHO_MAX_DEVICES) return -1;

    size_t idx = db->device_count++;
    EchoDevice* d = &db->devices[idx];
    memset(d, 0, sizeof(*d));
    memcpy(d->mac, mac, 6);
    d->first_tick = tick;
    d->last_tick = tick;
    d->best_rssi = -128;
    d->link_group = ECHO_NO_GROUP;
    d->link_conf = (uint8_t)ExpoLinkNone;
    d->randomized = pi_mac_is_randomized(mac);
    return (int)idx;
}

/* Caller holds the lock. Returns index, or -1 when the table is full. */
static int find_or_add_ssid(EchoDb* db, const char* ssid, uint32_t tick) {
    for(size_t i = 0; i < db->ssid_count; i++) {
        if(strncmp(db->ssids[i].name, ssid, PI_SSID_MAX) == 0) return (int)i;
    }
    if(db->ssid_count >= ECHO_MAX_SSIDS) return -1;

    size_t idx = db->ssid_count++;
    EchoSsid* s = &db->ssids[idx];
    memset(s, 0, sizeof(*s));
    strncpy(s->name, ssid, PI_SSID_MAX - 1);
    s->name[PI_SSID_MAX - 1] = '\0';

    PiSsidInfo info = pi_classify(s->name);
    s->cat = (uint8_t)info.cat;
    s->flags = info.flags;
    s->first_tick = tick;
    s->last_tick = tick;
    s->best_rssi = -128;
    return (int)idx;
}

/*
 * Tie this address to any other one carrying the same probe fingerprint.
 *
 * A matching fingerprint alone is "likely": two phones off the same production
 * line answer the same way. A sequence counter that carries on across the
 * change of address is the stronger claim, because counters do not continue by
 * coincidence - which is exactly the flaw that makes MAC randomization leak.
 *
 * Caller holds the lock. Returns true when a link was made that did not exist.
 */
static bool link_device(EchoDb* db, size_t idx, uint16_t seq) {
    EchoDevice* d = &db->devices[idx];
    if(d->fp == 0u) return false;

    bool upgraded = false;

    for(size_t i = 0; i < db->device_count; i++) {
        if(i == idx) continue;
        EchoDevice* o = &db->devices[i];
        if(o->fp != d->fp) continue;

        uint8_t group = (o->link_group != ECHO_NO_GROUP) ? o->link_group : (uint8_t)i;
        bool was_unlinked = (d->link_group == ECHO_NO_GROUP);

        d->link_group = group;
        o->link_group = group;

        uint8_t conf = (uint8_t)ExpoLinkLikely;
        if(pi_seq_continues(o->last_seq, seq, ECHO_SEQ_WINDOW)) {
            conf = (uint8_t)ExpoLinkConfirmed;
        }

        if(conf > d->link_conf) {
            d->link_conf = conf;
            upgraded = true;
        }
        if(conf > o->link_conf) o->link_conf = conf;
        if(was_unlinked) upgraded = true;
    }

    return upgraded;
}

/* ---------------------------------------------------------------- ingestion */

EchoEvent echo_db_on_probe(
    EchoDb* db,
    const uint8_t mac[6],
    uint8_t channel,
    int8_t rssi,
    uint16_t seq,
    uint32_t fp,
    const char* ssid) {
    furi_assert(db);
    if(!ssid) ssid = "";

    uint32_t tick = furi_get_tick();
    EchoEvent event = EchoEventNone;

    furi_mutex_acquire(db->mutex, FuriWaitForever);

    db->probe_total++;
    db->last_probe_tick = tick;

    int di = find_or_add_device(db, mac, tick);
    if(di < 0) {
        /* Table full. Still record the probe in the feed so the screen keeps
         * moving, then get out. */
        furi_mutex_release(db->mutex);
        return EchoEventNone;
    }

    EchoDevice* d = &db->devices[(size_t)di];
    bool is_new_device = (d->probes == 0u);

    d->probes++;
    d->rssi = rssi;
    d->best_rssi = max_rssi(d->best_rssi, rssi);
    d->channel = channel;
    d->last_tick = tick;
    if(fp != 0u) d->fp = fp;

    if(is_new_device) event = EchoEventNewDevice;

    /* Link before overwriting last_seq: the comparison is against the *other*
     * address's counter, but a repeat visit to the same address must not be
     * measured against a stale value either. */
    if(fp != 0u && link_device(db, (size_t)di, seq)) {
        event = EchoEventLinked;
    }
    d->last_seq = seq;

    if(ssid[0] != '\0') {
        d->named_probes++;
        db->named_total++;

        int si = find_or_add_ssid(db, ssid, tick);
        if(si >= 0) {
            EchoSsid* s = &db->ssids[(size_t)si];
            bool first_ever = (s->hits == 0u);
            s->hits++;
            s->last_tick = tick;
            s->best_rssi = max_rssi(s->best_rssi, rssi);
            s->asker_mask |= ((uint64_t)1u << (uint64_t)di);

            /* remember it against the device, if there is room */
            bool known_here = false;
            for(uint8_t k = 0; k < d->ssid_n; k++) {
                if(d->ssid_idx[k] == (uint8_t)si) {
                    known_here = true;
                    break;
                }
            }
            if(!known_here) {
                d->named_total++;
                if(d->ssid_n < ECHO_DEV_SSIDS) d->ssid_idx[d->ssid_n++] = (uint8_t)si;
            }

            if(first_ever && event < EchoEventNewName) event = EchoEventNewName;

            uint8_t w = pi_cat_weight((PiCat)s->cat);
            if(first_ever && w >= 4u && event < EchoEventSensitive) {
                event = EchoEventSensitive;
            }
        }
    }

    /* ---- the live feed ---- */
    EchoFeedRow* row = &db->feed[db->feed_head];
    memset(row, 0, sizeof(*row));
    memcpy(row->mac, mac, 6);
    strncpy(row->ssid, ssid, PI_SSID_MAX - 1);
    row->ssid[PI_SSID_MAX - 1] = '\0';
    row->rssi = rssi;
    row->channel = channel;
    row->cat = (uint8_t)pi_classify(ssid).cat;
    row->randomized = d->randomized;
    row->tick = tick;

    db->feed_head = (db->feed_head + 1u) % ECHO_MAX_FEED;
    if(db->feed_count < ECHO_MAX_FEED) db->feed_count++;

    furi_mutex_release(db->mutex);
    return event;
}

/* ------------------------------------------------------------------ stats */

void echo_db_get_stats(EchoDb* db, EchoStats* out) {
    furi_assert(db);
    furi_assert(out);
    memset(out, 0, sizeof(*out));

    furi_mutex_acquire(db->mutex, FuriWaitForever);

    out->device_count = db->device_count;
    out->ssid_count = db->ssid_count;
    out->probe_total = db->probe_total;
    out->named_total = db->named_total;
    out->last_probe_tick = db->last_probe_tick;

    for(size_t i = 0; i < db->device_count; i++) {
        const EchoDevice* d = &db->devices[i];
        if(d->named_total > 0u) {
            out->leaking_count++;
        } else if(d->randomized) {
            out->silent_count++;
        }
    }

    uint32_t cat_mask = 0;
    for(size_t i = 0; i < db->ssid_count; i++) {
        if(db->ssids[i].cat != (uint8_t)PiCatUnknown) {
            cat_mask |= expo_cat_bit((PiCat)db->ssids[i].cat);
        }
    }
    for(uint8_t c = 0; c < 32u; c++) {
        if(cat_mask & ((uint32_t)1u << c)) out->place_count++;
    }

    /* count distinct link groups */
    uint64_t seen = 0;
    for(size_t i = 0; i < db->device_count; i++) {
        uint8_t g = db->devices[i].link_group;
        if(g == ECHO_NO_GROUP || g >= 64u) continue;
        uint64_t bit = (uint64_t)1u << (uint64_t)g;
        if(!(seen & bit)) {
            seen |= bit;
            out->linked_groups++;
        }
    }

    furi_mutex_release(db->mutex);
}

/* ------------------------------------------------------------------ copies */

size_t echo_db_copy_blips(EchoDb* db, EchoBlip* out, size_t max) {
    furi_assert(db);
    if(max == 0) return 0;

    furi_mutex_acquire(db->mutex, FuriWaitForever);
    uint32_t now = furi_get_tick();

    size_t n = 0;
    for(size_t i = 0; i < db->device_count && n < max; i++) {
        const EchoDevice* d = &db->devices[i];
        uint32_t age = now - d->last_tick;
        if(age > 20000u) continue; /* gone quiet; leave the screen */

        out[n].slot = (uint8_t)i;
        out[n].rssi = d->rssi;
        out[n].age_ms = (uint16_t)((age > 60000u) ? 60000u : age);
        out[n].named = (d->named_probes > 0u);
        out[n].randomized = d->randomized;
        n++;
    }

    /* strongest first, so the crowded middle of the screen keeps the loud ones */
    for(size_t i = 1; i < n; i++) {
        EchoBlip key = out[i];
        size_t j = i;
        while(j > 0 && out[j - 1].rssi < key.rssi) {
            out[j] = out[j - 1];
            j--;
        }
        out[j] = key;
    }

    furi_mutex_release(db->mutex);
    return n;
}

size_t echo_db_copy_feed(EchoDb* db, EchoFeedRow* out, size_t max) {
    furi_assert(db);
    if(max == 0) return 0;

    furi_mutex_acquire(db->mutex, FuriWaitForever);

    size_t n = db->feed_count < max ? db->feed_count : max;
    for(size_t i = 0; i < n; i++) {
        /* walk backwards from the most recent write */
        size_t slot = (db->feed_head + ECHO_MAX_FEED - 1u - i) % ECHO_MAX_FEED;
        out[i] = db->feed[slot];
    }

    furi_mutex_release(db->mutex);
    return n;
}

/* Sort key for networks: the telling ones first, ties broken by how often the
 * name was called out. */
static bool ssid_before(const EchoSsid* a, const EchoSsid* b) {
    uint8_t wa = pi_cat_weight((PiCat)a->cat);
    uint8_t wb = pi_cat_weight((PiCat)b->cat);
    if(wa != wb) return wa > wb;
    return a->hits > b->hits;
}

size_t echo_db_copy_ssids(EchoDb* db, EchoSsid* out, size_t max) {
    furi_assert(db);
    if(max == 0) return 0;

    furi_mutex_acquire(db->mutex, FuriWaitForever);

    size_t n = db->ssid_count < max ? db->ssid_count : max;
    for(size_t i = 0; i < n; i++) out[i] = db->ssids[i];

    for(size_t i = 1; i < n; i++) {
        EchoSsid key = out[i];
        size_t j = i;
        while(j > 0 && ssid_before(&key, &out[j - 1])) {
            out[j] = out[j - 1];
            j--;
        }
        out[j] = key;
    }

    furi_mutex_release(db->mutex);
    return n;
}

/* Caller holds the lock. */
static void build_exposure(const EchoDb* db, const EchoDevice* d, ExpoInput* in) {
    memset(in, 0, sizeof(*in));

    in->named_ssids = d->named_total;
    in->probe_count = d->probes;
    in->mac_randomized = d->randomized;
    in->vendor_known = (pi_mac_vendor(d->mac) != NULL);
    in->link_confidence = d->link_conf;

    uint8_t group = 1;
    if(d->link_group != ECHO_NO_GROUP) {
        group = 0;
        for(size_t i = 0; i < db->device_count; i++) {
            if(db->devices[i].link_group == d->link_group) group++;
        }
    }
    in->linked_macs = group;

    for(uint8_t k = 0; k < d->ssid_n; k++) {
        const EchoSsid* s = &db->ssids[d->ssid_idx[k]];
        in->cat_mask |= expo_cat_bit((PiCat)s->cat);
        if(s->flags & PiFlagPersonalName) in->personal_name = true;
        if(s->flags & PiFlagGeolocatable) in->geolocatable = true;
    }
}

/* Caller holds the lock. The name that says the most about its owner. */
static const EchoSsid* top_ssid_of(const EchoDb* db, const EchoDevice* d) {
    const EchoSsid* best = NULL;
    for(uint8_t k = 0; k < d->ssid_n; k++) {
        const EchoSsid* s = &db->ssids[d->ssid_idx[k]];
        if(!best || ssid_before(s, best)) best = s;
    }
    return best;
}

size_t echo_db_copy_devices(EchoDb* db, EchoDeviceRow* out, size_t max) {
    furi_assert(db);
    if(max == 0) return 0;

    furi_mutex_acquire(db->mutex, FuriWaitForever);

    size_t n = 0;
    for(size_t i = 0; i < db->device_count && n < max; i++) {
        const EchoDevice* d = &db->devices[i];
        EchoDeviceRow* r = &out[n];
        memset(r, 0, sizeof(*r));

        r->index = (uint8_t)i;
        memcpy(r->mac, d->mac, 6);
        r->rssi = d->rssi;
        r->probes = d->probes;
        r->named = d->named_total;
        r->randomized = d->randomized;
        r->link_conf = d->link_conf;
        r->vendor = pi_mac_vendor(d->mac);

        ExpoInput in;
        build_exposure(db, d, &in);
        r->group_size = in.linked_macs;

        ExpoResult res = expo_score(&in);
        r->score = res.score;
        r->grade = res.grade;

        const EchoSsid* top = top_ssid_of(db, d);
        if(top) {
            strncpy(r->top_ssid, top->name, PI_SSID_MAX - 1);
            r->top_ssid[PI_SSID_MAX - 1] = '\0';
            r->top_cat = top->cat;
        } else {
            r->top_cat = (uint8_t)PiCatUnknown;
        }
        n++;
    }

    /* worst first: the point of the screen is the phone that gave itself away */
    for(size_t i = 1; i < n; i++) {
        EchoDeviceRow key = out[i];
        size_t j = i;
        while(j > 0 && (out[j - 1].score < key.score ||
                        (out[j - 1].score == key.score && out[j - 1].rssi < key.rssi))) {
            out[j] = out[j - 1];
            j--;
        }
        out[j] = key;
    }

    furi_mutex_release(db->mutex);
    return n;
}

/* ------------------------------------------------------------ one device */

bool echo_db_get_device(EchoDb* db, uint8_t index, EchoDevice* out) {
    furi_assert(db);
    furi_assert(out);

    furi_mutex_acquire(db->mutex, FuriWaitForever);
    bool ok = (index < db->device_count);
    if(ok) *out = db->devices[index];
    furi_mutex_release(db->mutex);
    return ok;
}

bool echo_db_device_exposure(EchoDb* db, uint8_t index, ExpoInput* out) {
    furi_assert(db);
    furi_assert(out);

    furi_mutex_acquire(db->mutex, FuriWaitForever);
    bool ok = (index < db->device_count);
    if(ok) build_exposure(db, &db->devices[index], out);
    furi_mutex_release(db->mutex);
    return ok;
}

size_t echo_db_device_ssids(EchoDb* db, uint8_t index, EchoSsid* out, size_t max) {
    furi_assert(db);
    if(max == 0) return 0;

    furi_mutex_acquire(db->mutex, FuriWaitForever);

    size_t n = 0;
    if(index < db->device_count) {
        const EchoDevice* d = &db->devices[index];
        for(uint8_t k = 0; k < d->ssid_n && n < max; k++) out[n++] = db->ssids[d->ssid_idx[k]];

        for(size_t i = 1; i < n; i++) {
            EchoSsid key = out[i];
            size_t j = i;
            while(j > 0 && ssid_before(&key, &out[j - 1])) {
                out[j] = out[j - 1];
                j--;
            }
            out[j] = key;
        }
    }

    furi_mutex_release(db->mutex);
    return n;
}

uint8_t echo_db_group_size(EchoDb* db, uint8_t index) {
    furi_assert(db);

    furi_mutex_acquire(db->mutex, FuriWaitForever);
    uint8_t n = 1;
    if(index < db->device_count) {
        uint8_t g = db->devices[index].link_group;
        if(g != ECHO_NO_GROUP) {
            n = 0;
            for(size_t i = 0; i < db->device_count; i++) {
                if(db->devices[i].link_group == g) n++;
            }
        }
    }
    furi_mutex_release(db->mutex);
    return n;
}
