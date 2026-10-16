// 親機 (P-MAIN) 固有: GNSS (UART1 NMEA) / PPS (GP6) / TX 温度 ADC (GP27)
#include "parent_devices.h"
#include "child_main_pins.h"

static ParentState P = { false, 0, 0, 0, 0, "-", 0, 0 };

#ifdef BOARD_PARENT
static volatile uint32_t pps_isr_count = 0;
static void pps_isr() { pps_isr_count++; }

static char     line[120];
static uint8_t  line_len = 0;
static uint32_t autobaud_started = 0;
static bool     tried_second = false;

static void gnss_begin(uint32_t baud) {
    Serial2.end();
    Serial2.setTX(PIN_GNSS_TX);
    Serial2.setRX(PIN_GNSS_RX);
    Serial2.begin(baud);
    P.gnss_baud = baud;
    autobaud_started = millis();
}

// $xxGGA,hhmmss.ss,lat,N,lon,E,fix,sats,...
static void parse_gga(const char* s) {
    const char* f[10]; int n = 0;
    for (const char* p = s; *p && n < 10; p++) if (*p == ',') f[n++] = p + 1;
    if (n < 7) return;
    strncpy(P.gnss_time, f[0], 10); P.gnss_time[10] = 0;
    char* c = strchr(P.gnss_time, ','); if (c) *c = 0;
    P.gnss_fix  = (uint8_t)atoi(f[5]);
    P.gnss_sats = (uint8_t)atoi(f[6]);
}

void parent_setup() {
    pinMode(PIN_EVENT_MARK, OUTPUT); digitalWrite(PIN_EVENT_MARK, LOW);
    pinMode(PIN_TX_PWM, OUTPUT);     digitalWrite(PIN_TX_PWM, LOW);      // 送信停止
    pinMode(PIN_GNSS_PPS, INPUT_PULLDOWN);
    attachInterrupt(digitalPinToInterrupt(PIN_GNSS_PPS), pps_isr, RISING);
    gnss_begin(GNSS_BAUD_FIRST);
    tried_second = false;
    Serial.printf("# parent: GNSS UART1 TX=GP%d RX=GP%d %lu bps (autobaud -> %lu), PPS=GP%d, TX_TEMP=GP%d, TX_PWM=GP%d Low, EVENT=GP%d Low\n",
                  PIN_GNSS_TX, PIN_GNSS_RX, (unsigned long)GNSS_BAUD_FIRST, (unsigned long)GNSS_BAUD_SECOND,
                  PIN_GNSS_PPS, PIN_TX_TEMP_ADC, PIN_TX_PWM, PIN_EVENT_MARK);
}

void parent_tick(uint32_t now) {
    // GNSS 受信
    while (Serial2.available()) {
        char ch = (char)Serial2.read();
        if (ch == '\n' || ch == '\r') {
            if (line_len > 6 && line[0] == '$') {
                line[line_len] = 0;
                P.gnss_lines++;
                if (!P.gnss_present) {
                    P.gnss_present = true;
                    Serial.printf("# GNSS first NMEA at %lu bps: %s\n", (unsigned long)P.gnss_baud, line);
                }
                if (strncmp(line + 3, "GGA", 3) == 0) parse_gga(line);
            }
            line_len = 0;
        } else if (line_len < sizeof(line) - 1) {
            line[line_len++] = ch;
        } else {
            line_len = 0;   // ゴミ (ボーレート違い) は捨てる
        }
    }
    if (!P.gnss_present && !tried_second && now - autobaud_started > GNSS_AUTOBAUD_MS) {
        tried_second = true;
        Serial.printf("# GNSS no NMEA at %lu bps -> try %lu bps\n", (unsigned long)GNSS_BAUD_FIRST, (unsigned long)GNSS_BAUD_SECOND);
        gnss_begin(GNSS_BAUD_SECOND);
    }
    // PPS
    noInterrupts(); P.pps_count = pps_isr_count; interrupts();
    // TX 温度
    static uint32_t last_adc = 0;
    if (now - last_adc >= 200) {
        last_adc = now;
        uint32_t sum = 0;
        for (int i = 0; i < ADC_OVERSAMPLE; i++) sum += analogRead(PIN_TX_TEMP_ADC);
        P.txt_mv = ((sum / ADC_OVERSAMPLE) * (uint32_t)ADC_VREF_MV) / ((1u << ADC_BITS) - 1);
    }
}

void parent_print_state() {
    Serial.printf("# parent state: gnss=%d baud=%lu lines=%lu fix=%u sats=%u utc=%s pps=%lu tx_temp=%lu mV\n",
                  P.gnss_present, (unsigned long)P.gnss_baud, (unsigned long)P.gnss_lines, P.gnss_fix, P.gnss_sats,
                  P.gnss_time, (unsigned long)P.pps_count, (unsigned long)P.txt_mv);
}
#else
void parent_setup() {}
void parent_tick(uint32_t) {}
void parent_print_state() {}
#endif

const ParentState& parent_state() { return P; }
