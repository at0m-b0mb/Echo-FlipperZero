#pragma once

/**
 * echo_report - write down what leaked, without writing down who leaked it.
 *
 * A tool that warns people about broadcast identifiers should not leave a file
 * full of them on an SD card. So the report keeps the shape of the finding and
 * throws away the identifying half: MAC addresses are cut back to the vendor
 * prefix, and network names to their first three characters. "Hil..." is
 * enough to remember the demo by and not enough to look anybody up.
 */

#include <furi.h>
#include <storage/storage.h>

#include "echo_db.h"

#define ECHO_REPORT_DIR      EXT_PATH("apps_data/echo")
#define ECHO_REPORT_PATH_MAX 96

/**
 * Write a redacted summary of everything currently in the table.
 *
 * `path_out` receives the file that was written, for showing the user.
 * Returns false if the card is missing or the write failed.
 */
bool echo_report_save(Storage* storage, EchoDb* db, char* path_out, size_t path_out_len);
