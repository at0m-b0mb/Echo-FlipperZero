#pragma once

#include <furi.h>

/**
 * Serial link to the Echo ESP32 companion on the Flipper USART (GPIO 13 TX /
 * 14 RX) at 115200 8N1. A worker thread parses the line protocol and hands
 * each probe to the app.
 *
 * Wire protocol (newline-terminated ASCII):
 *
 *   ESP32 -> Flipper
 *     EHELLO,<fw_version>                                on boot and on PING
 *     EPR,<mac12hex>,<ch>,<rssi>,<seq>,<fp8hex>,<ssid>   one probe request
 *     ESTAT,<heard>,<ch>                                 heartbeat, ~1 Hz
 *
 *   Flipper -> ESP32
 *     START            begin sniffing
 *     STOP             stop sniffing
 *     CHAN:<0-13>      0 = hop every channel, 1..13 = camp on one
 *     PING             re-announce EHELLO
 *
 * The SSID is deliberately the last field, so a network name containing a
 * comma survives the trip intact. The board still strips control characters,
 * because a name containing a newline would not.
 */
typedef struct UartLink UartLink;

typedef void (*UartLinkProbeCallback)(
    void* context,
    const uint8_t mac[6],
    uint8_t channel,
    int8_t rssi,
    uint16_t seq,
    uint32_t fingerprint,
    const char* ssid);

typedef void (*UartLinkHelloCallback)(void* context, const char* version);

typedef void (*UartLinkStatCallback)(void* context, uint32_t heard, uint8_t channel);

UartLink* uart_link_alloc(void);
void uart_link_free(UartLink* link);

void uart_link_set_callbacks(
    UartLink* link,
    UartLinkProbeCallback probe_cb,
    UartLinkHelloCallback hello_cb,
    UartLinkStatCallback stat_cb,
    void* context);

void uart_link_start(UartLink* link);
void uart_link_stop(UartLink* link);
bool uart_link_is_running(UartLink* link);

void uart_link_send_command(UartLink* link, const char* cmd);
