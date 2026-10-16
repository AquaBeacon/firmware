// 親機 (P-MAIN) 固有: GNSS (UART1 NMEA) / PPS (GP6) / TX 温度 ADC (GP27)
//   子機ビルドでは全部スタブ (値は 0 / nan)
#pragma once
#include <Arduino.h>

struct ParentState {
    bool     gnss_present;   // NMEA 行が 1 行でも来た
    uint32_t gnss_baud;      // 使用中のボーレート (0 = 未確定)
    uint32_t gnss_lines;     // 受信した '$' 行の累計
    uint8_t  gnss_fix;       // GGA fix quality (0 = no fix)
    uint8_t  gnss_sats;      // GGA 衛星数
    char     gnss_time[11];  // GGA の UTC hhmmss.ss
    uint32_t pps_count;      // PPS 立ち上がり累計
    uint32_t txt_mv;         // TX_TEMP_ADC [mV]
};

void parent_setup();
void parent_tick(uint32_t now_ms);
void parent_print_state();
const ParentState& parent_state();
