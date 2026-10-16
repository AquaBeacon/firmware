// I2C0 上のデバイス (DS3231 / MS5837-30BA / TSYS01 / INA226) のスキャンと非ブロッキング読み出し
#pragma once
#include <Arduino.h>

struct I2cReadings {
    bool  rtc_ok, prs_ok, tsys_ok, ina_ok;
    char  rtc_time[20];     // "YYYY-MM-DDTHH:MM:SS"
    float rtc_temp;         // DS3231 内蔵温度 [degC]
    float prs_mbar;         // MS5837 圧力 [mbar]
    float prs_temp;         // MS5837 温度 [degC]
    float tsys_temp;        // TSYS01 温度 [degC]
    float ina_v;            // INA226 バス電圧 [V] (= VBAT_M)
    float ina_ma;           // INA226 シャント電流 [mA] (R_SH 20mΩ)
    bool  lis_ok;           // LIS3MDL (親機 J_MAG1) 読めた
    float lis_x, lis_y, lis_z;  // LIS3MDL 地磁気 [uT]
    uint8_t lis_addr;       // 検出アドレス (0 = 無し)
};

void   i2c_init();
int    i2c_scan(uint8_t* found, int max_found);   // 見つかったアドレスを返す
void   i2c_print_scan();                          // "# I2C scan: ..." を出す
void   i2c_sensors_setup();                       // PROM 読み出しなど (存在するものだけ)
void   i2c_sensors_tick(uint32_t now_ms);         // loop から毎回呼ぶ (ブロッキングしない)
const I2cReadings& i2c_readings();
bool   i2c_rtc_set(uint16_t y, uint8_t mo, uint8_t d, uint8_t h, uint8_t mi, uint8_t s);  // OSF もクリア
