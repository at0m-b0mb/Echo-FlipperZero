/*
 * Echo - ESP32 companion firmware
 * -------------------------------
 * Puts the ESP32 radio into promiscuous mode and listens for one thing only:
 * probe requests. Those are the frames a phone sends when it goes looking for
 * a network, and the interesting ones carry the name of a network the phone
 * has joined before.
 *
 * Nothing is ever transmitted. The radio is brought up in NULL mode - no AP,
 * no station - so the board cannot associate, deauthenticate or beacon even by
 * accident.
 *
 * Hardware : Flipper Zero WiFi devboard (ESP32-S2) or any ESP32 dev board.
 * Wiring   : ESP32 UART0 -> Flipper GPIO (TX=pin 13, RX=pin 14, GND=GND) @115200.
 *            On the official devboard this is already bridged - just plug it in.
 *
 * Wire protocol (see helpers/uart_link.h on the Flipper side):
 *   ESP32 -> Flipper
 *     EHELLO,<version>
 *     EPR,<mac12hex>,<ch>,<rssi>,<seq>,<fp8hex>,<ssid>
 *     ESTAT,<heard>,<ch>
 *   Flipper -> ESP32
 *     START | STOP | CHAN:<0-13> | PING
 *
 * The SSID goes last so a network name containing a comma still arrives whole.
 */

#include <Arduino.h>
#include "esp_wifi.h"

#define ECHO_FW_VERSION "1.0"

#define HOP_INTERVAL_MS  280
#define STAT_INTERVAL_MS 1000
/* A phone bursts several identical probes at once; one of each per window is
 * plenty and keeps the 115200 link from becoming the bottleneck. */
#define DEDUP_MS         900
#define DEDUP_SIZE       32

static bool sniffing = false;
static bool hop_mode = true;
static uint8_t locked_channel = 1;
static uint8_t cur_channel = 1;
static unsigned long last_hop = 0;
static unsigned long last_stat = 0;
static uint32_t heard = 0;

struct Recent {
  uint8_t mac[6];
  uint32_t ssid_hash;
  unsigned long last_ms;
};
static Recent recent[DEDUP_SIZE];
static int recent_len = 0;

/* ------------------------------------------------------------ small parts */

static void mac_to_hex(const uint8_t* m, char* out) {
  static const char* H = "0123456789ABCDEF";
  for (int i = 0; i < 6; i++) {
    out[i * 2] = H[m[i] >> 4];
    out[i * 2 + 1] = H[m[i] & 0xF];
  }
  out[12] = '\0';
}

static uint32_t fnv1a(const uint8_t* data, size_t len, uint32_t h) {
  for (size_t i = 0; i < len; i++) {
    h ^= (uint32_t)data[i];
    h *= 16777619u;
  }
  return h;
}

/* Same seed the Flipper uses, though only the ESP32 ever computes a hash. */
#define FNV_SEED 0x811C9DC5u

static bool should_emit(const uint8_t* mac, uint32_t ssid_hash) {
  unsigned long now = millis();
  for (int i = 0; i < recent_len; i++) {
    if (memcmp(recent[i].mac, mac, 6) == 0 && recent[i].ssid_hash == ssid_hash) {
      if (now - recent[i].last_ms < DEDUP_MS) return false;
      recent[i].last_ms = now;
      return true;
    }
  }
  int slot;
  if (recent_len < DEDUP_SIZE) {
    slot = recent_len++;
  } else {
    slot = 0;
    for (int i = 1; i < recent_len; i++)
      if (recent[i].last_ms < recent[slot].last_ms) slot = i;
  }
  memcpy(recent[slot].mac, mac, 6);
  recent[slot].ssid_hash = ssid_hash;
  recent[slot].last_ms = now;
  return true;
}

/*
 * Fold the probe's information elements into one 32-bit value.
 *
 * This is the part that survives a change of MAC address. Phones differ in
 * which elements they include, in what order, and in the exact contents of the
 * rate and capability fields - so the shape of a probe is close to a model
 * signature. The SSID element is deliberately excluded: the same phone sends
 * both named and broadcast probes, and a fingerprint that changed between them
 * would link nothing to anything.
 *
 * It is a fingerprint, not an identity. Two phones off the same production line
 * hash the same, which is exactly why the Flipper calls a bare match "likely"
 * and waits for a continuing sequence counter before it says more.
 */
static uint32_t probe_fingerprint(const uint8_t* body, int body_len) {
  uint32_t h = FNV_SEED;
  int i = 0;

  while (i + 2 <= body_len) {
    uint8_t id = body[i];
    uint8_t len = body[i + 1];
    const uint8_t* d = body + i + 2;
    if (i + 2 + len > body_len) break;

    if (id != 0) {  // skip the SSID: it is the thing that varies
      h = fnv1a(&id, 1, h);

      switch (id) {
        case 1:    // supported rates
        case 50:   // extended supported rates
        case 127:  // extended capabilities
          h = fnv1a(d, len, h);
          break;
        case 45:  // HT capabilities: the first two bytes are the interesting ones
          h = fnv1a(d, len < 2 ? len : 2, h);
          break;
        case 191:  // VHT capabilities
          h = fnv1a(d, len < 4 ? len : 4, h);
          break;
        case 221:  // vendor specific: the OUI, not the payload
          h = fnv1a(d, len < 3 ? len : 3, h);
          break;
        default:
          break;
      }
    }
    i += 2 + len;
  }
  return h;
}

/* ------------------------------------------------------------- the sniffer */

static void sniffer_cb(void* buf, wifi_promiscuous_pkt_type_t type) {
  if (type != WIFI_PKT_MGMT) return;
  const wifi_promiscuous_pkt_t* pkt = (wifi_promiscuous_pkt_t*)buf;
  const uint8_t* p = pkt->payload;
  int len = pkt->rx_ctrl.sig_len;
  if (len < 24) return;

  uint16_t fc = p[0] | (p[1] << 8);
  uint8_t ftype = (fc >> 2) & 0x3;
  uint8_t fsub = (fc >> 4) & 0xF;
  if (ftype != 0 || fsub != 4) return;  // management, subtype 4 = probe request

  const uint8_t* src = p + 10;
  /* sequence control: 12 bits of counter in the high nibbles */
  uint16_t seq = ((uint16_t)(p[22] | (p[23] << 8))) >> 4;

  const uint8_t* body = p + 24;  // a probe request has no fixed parameters
  int body_len = len - 24;
  if (body_len < 0) body_len = 0;

  /* SSID is tag 0, and it is allowed to be empty - that is a broadcast probe,
   * the polite kind that gives nothing away. */
  char ssid[33] = {0};
  int ssid_len = 0;
  if (body_len >= 2 && body[0] == 0) {
    ssid_len = body[1];
    if (ssid_len > 32) ssid_len = 32;
    if (2 + ssid_len > body_len) ssid_len = body_len - 2;
    if (ssid_len < 0) ssid_len = 0;
    for (int k = 0; k < ssid_len; k++) {
      uint8_t c = body[2 + k];
      /* commas are fine - the SSID is the last field - but a newline would
       * split the line in half at the other end */
      ssid[k] = (c < 0x20 || c > 0x7E) ? '.' : (char)c;
    }
    ssid[ssid_len] = '\0';
  }

  uint32_t ssid_hash = fnv1a((const uint8_t*)ssid, strlen(ssid), FNV_SEED);
  if (!should_emit(src, ssid_hash)) return;

  uint32_t fp = probe_fingerprint(body, body_len);

  char mac_hex[13];
  mac_to_hex(src, mac_hex);
  heard++;

  Serial.printf(
      "EPR,%s,%u,%d,%u,%08X,%s\n",
      mac_hex,
      pkt->rx_ctrl.channel,
      pkt->rx_ctrl.rssi,
      seq,
      fp,
      ssid);
}

static void start_sniffer() {
  wifi_promiscuous_filter_t filter = {.filter_mask = WIFI_PROMIS_FILTER_MASK_MGMT};
  esp_wifi_set_promiscuous_filter(&filter);
  esp_wifi_set_promiscuous_rx_cb(&sniffer_cb);
  esp_wifi_set_promiscuous(true);
  esp_wifi_set_channel(cur_channel, WIFI_SECOND_CHAN_NONE);
  sniffing = true;
}

static void stop_sniffer() {
  esp_wifi_set_promiscuous(false);
  sniffing = false;
}

/* ------------------------------------------------------------------ setup */

void setup() {
  Serial.begin(115200);
  delay(200);

  /* NULL mode: the radio can receive, and cannot join or announce anything. */
  wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
  esp_wifi_init(&cfg);
  esp_wifi_set_storage(WIFI_STORAGE_RAM);
  esp_wifi_set_mode(WIFI_MODE_NULL);
  esp_wifi_start();

  Serial.printf("EHELLO,%s\n", ECHO_FW_VERSION);
  start_sniffer();  // auto-arm; the Flipper can STOP/START on demand
}

static void handle_command(String cmd) {
  cmd.trim();
  if (cmd == "START") {
    if (!sniffing) start_sniffer();
  } else if (cmd == "STOP") {
    if (sniffing) stop_sniffer();
  } else if (cmd == "PING") {
    Serial.printf("EHELLO,%s\n", ECHO_FW_VERSION);
  } else if (cmd.startsWith("CHAN:")) {
    int c = cmd.substring(5).toInt();
    if (c <= 0) {
      hop_mode = true;
    } else {
      hop_mode = false;
      locked_channel = (c > 13) ? 13 : (uint8_t)c;
      cur_channel = locked_channel;
      if (sniffing) esp_wifi_set_channel(cur_channel, WIFI_SECOND_CHAN_NONE);
    }
  }
}

void loop() {
  if (Serial.available()) {
    handle_command(Serial.readStringUntil('\n'));
  }

  unsigned long now = millis();

  if (sniffing && hop_mode && (now - last_hop >= HOP_INTERVAL_MS)) {
    last_hop = now;
    cur_channel++;
    if (cur_channel > 13) cur_channel = 1;
    esp_wifi_set_channel(cur_channel, WIFI_SECOND_CHAN_NONE);
  }

  /* A heartbeat, so the Flipper's link light means "the board is alive" rather
   * than "somebody nearby happened to send a probe in the last three seconds". */
  if (now - last_stat >= STAT_INTERVAL_MS) {
    last_stat = now;
    Serial.printf("ESTAT,%u,%u\n", heard, cur_channel);
  }

  delay(2);
}
