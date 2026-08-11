#include "echo_report.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "exposure.h"
#include "probe_intel.h"

/* One line at a time, so nothing large lands on the caller's stack. */
static bool report_write(File* file, const char* text) {
    size_t len = strlen(text);
    return storage_file_write(file, text, len) == len;
}

static bool report_printf(File* file, const char* fmt, ...) {
    char line[160];
    va_list args;
    va_start(args, fmt);
    vsnprintf(line, sizeof(line), fmt, args);
    va_end(args);
    return report_write(file, line);
}

/** First free echo_NN.txt, so a demo can be run more than once. */
static void report_pick_path(Storage* storage, char* out, size_t out_len) {
    for(uint8_t i = 1; i < 100u; i++) {
        snprintf(out, out_len, ECHO_REPORT_DIR "/echo_%02u.txt", (unsigned)i);
        FileInfo info;
        if(storage_common_stat(storage, out, &info) != FSE_OK) return;
    }
    /* a hundred reports in and we simply overwrite the last one */
    snprintf(out, out_len, ECHO_REPORT_DIR "/echo_99.txt");
}

bool echo_report_save(Storage* storage, EchoDb* db, char* path_out, size_t path_out_len) {
    furi_assert(storage);
    furi_assert(db);

    storage_common_mkdir(storage, ECHO_REPORT_DIR);
    report_pick_path(storage, path_out, path_out_len);

    File* file = storage_file_alloc(storage);
    if(!storage_file_open(file, path_out, FSAM_WRITE, FSOM_CREATE_ALWAYS)) {
        storage_file_close(file);
        storage_file_free(file);
        return false;
    }

    EchoStats stats;
    echo_db_get_stats(db, &stats);

    bool ok = true;
    char masked[24];

    ok = ok && report_write(file, "Echo - Wi-Fi probe request exposure report\n");
    ok = ok && report_write(file, "=========================================\n\n");
    ok = ok && report_printf(
                   file,
                   "Devices heard    : %u\n"
                   "Probes heard     : %lu\n"
                   "Named probes     : %lu\n"
                   "Devices leaking  : %u\n"
                   "Devices silent   : %u\n"
                   "Networks named   : %u\n"
                   "Kinds of place   : %u\n"
                   "Radios behind >1 MAC : %u\n\n",
                   (unsigned)stats.device_count,
                   (unsigned long)stats.probe_total,
                   (unsigned long)stats.named_total,
                   (unsigned)stats.leaking_count,
                   (unsigned)stats.silent_count,
                   (unsigned)stats.ssid_count,
                   (unsigned)stats.place_count,
                   (unsigned)stats.linked_groups);

    /* ---- devices, worst first ---- */
    ok = ok && report_write(file, "Devices (most exposed first)\n");
    ok = ok && report_write(file, "---------------------------\n");

    EchoDeviceRow* rows = malloc(sizeof(EchoDeviceRow) * ECHO_MAX_DEVICES);
    size_t n = echo_db_copy_devices(db, rows, ECHO_MAX_DEVICES);
    for(size_t i = 0; i < n && ok; i++) {
        const EchoDeviceRow* r = &rows[i];
        pi_mac_str_masked(r->mac, masked, sizeof(masked));

        const char* who = r->randomized ? "random MAC" : (r->vendor ? r->vendor : "real MAC");
        ok = report_printf(
            file,
            "%-2s  %s  %-12s  %2u names  %3u/100\n",
            expo_grade_str((ExpoGrade)r->grade),
            masked,
            who,
            (unsigned)r->named,
            (unsigned)r->score);

        if(ok && r->group_size > 1u) {
            ok = report_printf(
                file, "      linked: %u addresses, one radio\n", (unsigned)r->group_size);
        }

        if(ok && r->top_ssid[0] != '\0') {
            char hint[16];
            pi_ssid_mask(r->top_ssid, hint, sizeof(hint));
            ok = report_printf(
                file, "      worst leak: %s \"%s\"\n", pi_cat_label((PiCat)r->top_cat), hint);
        }
    }
    free(rows);

    /* ---- networks ---- */
    ok = ok && report_write(file, "\nNetworks named (most telling first)\n");
    ok = ok && report_write(file, "-----------------------------------\n");

    EchoSsid* ssids = malloc(sizeof(EchoSsid) * ECHO_MAX_SSIDS);
    size_t sn = echo_db_copy_ssids(db, ssids, ECHO_MAX_SSIDS);
    for(size_t i = 0; i < sn && ok; i++) {
        const EchoSsid* s = &ssids[i];
        char hint[16];
        pi_ssid_mask(s->name, hint, sizeof(hint));
        ok = report_printf(
            file,
            "%-10s %-8s x%-4u %u device(s)%s\n",
            pi_cat_label((PiCat)s->cat),
            hint,
            (unsigned)s->hits,
            (unsigned)echo_db_ssid_askers(s),
            (s->flags & PiFlagGeolocatable) ? "  [maps to one address]" : "");
    }
    free(ssids);

    ok = ok && report_write(
                   file,
                   "\nNotes\n"
                   "-----\n"
                   "MAC addresses are cut back to the vendor prefix and network\n"
                   "names to their first three characters. This report records\n"
                   "what leaked, not who leaked it.\n"
                   "\n"
                   "Echo listens only. It never transmits, never associates and\n"
                   "never stores a full network name or address.\n");

    storage_file_close(file);
    storage_file_free(file);
    return ok;
}
