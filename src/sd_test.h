// microSD (SPI1) のマウント・書き込み/読み戻し検証・定期追記
#pragma once
#include <Arduino.h>

struct SdState {
    bool     mounted;       // SD.begin 成功
    bool     verify_ok;     // CMTEST.TXT 書き込み→読み戻し一致
    uint32_t writes_ok;     // CMLOG.CSV への追記成功回数 (累計)
    uint32_t writes_fail;
    uint32_t hz;            // マウントに使った SPI クロック
    uint64_t size_bytes;    // カード容量
    uint8_t  cd_raw;        // GP15 の生の値
    const char* error;
};

void sd_setup();                        // マウント + 検証 (診断行を出す)
void sd_tick(uint32_t now_ms, const char* csv_row);  // 周期追記 (csv_row を CMLOG.CSV に書く)
const SdState& sd_state();
