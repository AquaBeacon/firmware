#include "rs485_test.h"
#include "child_main_pins.h"

static Rs485State S = {0, 0, 0, 0, 0};
static char line[64];
static size_t used = 0;
static uint32_t next_ping_ms = 0, ping_seq = 0;
static bool reply_pending = false;
static uint32_t reply_seq = 0, reply_at_ms = 0;

static uint8_t checksum(const char* p) {
    uint8_t v = 0;
    while (*p) v ^= (uint8_t)*p++;
    return v;
}

static void send_frame(char kind, uint32_t seq) {
    char body[32];
    snprintf(body, sizeof(body), "AQB,%c,%lu", kind, (unsigned long)seq);
    Serial1.printf("%s,%02X\n", body, checksum(body));
    Serial1.flush();
    S.tx_ok++;
}

static void process_line(uint32_t now) {
    line[used] = 0;
    char prefix[4] = {0};
    char kind = 0;
    unsigned long seq = 0;
    unsigned int got_crc = 0;
    if (sscanf(line, "%3[^,],%c,%lu,%2x", prefix, &kind, &seq, &got_crc) != 4 || strcmp(prefix, "AQB") != 0) {
        S.malformed++;
        return;
    }
    char body[32];
    snprintf(body, sizeof(body), "AQB,%c,%lu", kind, seq);
    if ((uint8_t)got_crc != checksum(body)) { S.crc_fail++; return; }
    if (kind != 'P' && kind != 'R') { S.malformed++; return; }
    S.rx_ok++;
    S.last_rx_ms = now;
#ifndef BOARD_PARENT
    if (kind == 'P') {
        reply_pending = true;
        reply_seq = (uint32_t)seq;
        reply_at_ms = now + RS485_REPLY_GUARD_MS;
    }
#endif
}

void rs485_setup() {
    Serial1.setTX(PIN_RS485_TX);
    Serial1.setRX(PIN_RS485_RX);
    Serial1.begin(RS485_BAUD);
    while (Serial1.available()) Serial1.read();
    next_ping_ms = millis() + 500;
    Serial.printf("# RS485 init: UART0 TX=GP%d RX=GP%d %d baud role=%s\n",
                  PIN_RS485_TX, PIN_RS485_RX, RS485_BAUD,
#ifdef BOARD_PARENT
                  "PING"
#else
                  "PONG"
#endif
    );
}

void rs485_tick(uint32_t now) {
    while (Serial1.available()) {
        int c = Serial1.read();
        if (c == '\n' || c == '\r') {
            if (used) { process_line(now); used = 0; }
        } else if (used < sizeof(line) - 1) {
            line[used++] = (char)c;
        } else {
            used = 0;
            S.malformed++;
        }
    }
#ifdef BOARD_PARENT
    if ((int32_t)(now - next_ping_ms) >= 0) {
        next_ping_ms += RS485_PING_MS;
        send_frame('P', ping_seq++);
    }
#else
    if (reply_pending && (int32_t)(now - reply_at_ms) >= 0) {
        reply_pending = false;
        send_frame('R', reply_seq);
    }
#endif
}

const Rs485State& rs485_state() { return S; }
