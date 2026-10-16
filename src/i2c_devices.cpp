// I2C0 デバイス: DS3231 / MS5837-30BA / TSYS01 / INA226
//   計算式の出典: MS5837-30BA データシート (TE ENG_DS_MS5837-30BA_B1) の 1次+2次補正、
//                 TSYS01 データシート の 4次多項式、INA226 データシート (LSB: bus 1.25mV / shunt 2.5uV)
#include "i2c_devices.h"
#include <Wire.h>
#include <math.h>
#include "child_main_pins.h"

static I2cReadings R;
static bool have_rtc = false, have_prs = false, have_tsys = false, have_ina = false, have_lis = false;
static uint16_t prs_C[8] = {0};
static uint16_t tsys_C[8] = {0};
static uint32_t prs_D1 = 0, prs_D2 = 0;
static uint8_t  step = 0;
static uint32_t next_ms = 0;

// ---------------------------------------------------------------- 低レベル
static bool wr(uint8_t addr, const uint8_t* buf, size_t n) {
    Wire.beginTransmission(addr);
    Wire.write(buf, n);
    return Wire.endTransmission() == 0;
}
static bool wr1(uint8_t addr, uint8_t b) { return wr(addr, &b, 1); }
static int rd(uint8_t addr, uint8_t* buf, size_t n) {
    size_t got = Wire.requestFrom((int)addr, (int)n);
    for (size_t i = 0; i < got && i < n; i++) buf[i] = Wire.read();
    return (int)got;
}
static bool rd_reg(uint8_t addr, uint8_t reg, uint8_t* buf, size_t n) {
    if (!wr1(addr, reg)) return false;
    return rd(addr, buf, n) == (int)n;
}
static bool present(uint8_t addr) {
    Wire.beginTransmission(addr);
    return Wire.endTransmission() == 0;
}
static bool read_prom(uint8_t addr, uint16_t* C, int words) {   // MS5837=7語(0xA0-0xAC) / TSYS01=8語
    for (int i = 0; i < words; i++) {
        uint8_t b[2];
        if (!rd_reg(addr, 0xA0 + i * 2, b, 2)) return false;
        C[i] = ((uint16_t)b[0] << 8) | b[1];
    }
    return true;
}
static bool read_adc24(uint8_t addr, uint32_t* v) {
    uint8_t b[3];
    if (!rd_reg(addr, 0x00, b, 3)) return false;
    *v = ((uint32_t)b[0] << 16) | ((uint32_t)b[1] << 8) | b[2];
    return true;
}
static uint8_t bcd(uint8_t v) { return (v >> 4) * 10 + (v & 0x0F); }

// ---------------------------------------------------------------- 公開
void i2c_init() {
    Wire.setSDA(PIN_I2C_SDA);
    Wire.setSCL(PIN_I2C_SCL);
    Wire.begin();
    Wire.setClock(I2C_HZ);
    Wire.setTimeout(20);
    memset(&R, 0, sizeof(R));
    R.rtc_temp = R.prs_mbar = R.prs_temp = R.tsys_temp = R.ina_v = R.ina_ma = NAN;
    R.lis_x = R.lis_y = R.lis_z = NAN; R.lis_addr = 0;
    strcpy(R.rtc_time, "-");
}

int i2c_scan(uint8_t* found, int max_found) {
    int n = 0;
    for (uint8_t a = 0x08; a <= 0x77; a++) {
        if (present(a) && n < max_found) found[n++] = a;
    }
    return n;
}

void i2c_print_scan() {
    uint8_t f[16];
    int n = i2c_scan(f, 16);
    Serial.printf("# I2C scan (SDA=GP%d SCL=GP%d %lukHz): %d found:", PIN_I2C_SDA, PIN_I2C_SCL,
                  (unsigned long)(I2C_HZ / 1000), n);
    for (int i = 0; i < n; i++) Serial.printf(" 0x%02X", f[i]);
    Serial.println();
    Serial.printf("# I2C expected: 0x40 INA226(C-PWR) 0x57 EEPROM 0x68 DS3231 0x76 MS5837 0x77 TSYS01 (0x3E LCD は未接続なら無し)\n");
}

void i2c_sensors_setup() {
    have_rtc  = present(I2C_ADDR_RTC);
    have_ina  = present(I2C_ADDR_INA);
    have_prs  = present(I2C_ADDR_PRS);
    have_tsys = present(I2C_ADDR_TSYS);
    // LIS3MDL (親機の J_MAG1。子機には無いのが正常)
    have_lis = false; R.lis_addr = 0;
    static const uint8_t lis_addrs[2] = { I2C_ADDR_LIS_A, I2C_ADDR_LIS_B };
    for (uint8_t a : lis_addrs) {
        uint8_t who;
        if (present(a) && rd_reg(a, 0x0F, &who, 1)) {
            Serial.printf("# LIS3MDL @0x%02X WHO_AM_I=0x%02X (expect 0x3D)\n", a, who);
            if (who == 0x3D) {
                uint8_t c1[2] = {0x20, 0x70}, c2[2] = {0x21, 0x00}, c3[2] = {0x22, 0x00}, c4[2] = {0x23, 0x0C};
                wr(a, c1, 2); wr(a, c2, 2); wr(a, c3, 2); wr(a, c4, 2);   // UHP XY/Z, 10Hz, +-4 gauss, continuous
                have_lis = true; R.lis_addr = a; break;
            }
        }
    }

    if (have_ina) {
        uint8_t b[2];
        if (rd_reg(I2C_ADDR_INA, 0xFE, b, 2)) {
            uint16_t id = ((uint16_t)b[0] << 8) | b[1];
            Serial.printf("# INA226 manufacturer ID 0x%04X (expect 0x5449)\n", id);
            if (id != 0x5449) have_ina = false;
        }
    }
    if (have_prs) {
        wr1(I2C_ADDR_PRS, 0x1E); delay(10);
        if (read_prom(I2C_ADDR_PRS, prs_C, 7) && prs_C[1] != 0 && prs_C[1] != 0xFFFF) {
            Serial.printf("# MS5837 PROM C1..C6 = %u %u %u %u %u %u (ver=%u)\n", prs_C[1], prs_C[2], prs_C[3],
                          prs_C[4], prs_C[5], prs_C[6], (prs_C[0] >> 5) & 0x7F);
        } else { Serial.println("# ERROR MS5837 PROM read failed"); have_prs = false; }
    }
    if (have_tsys) {
        wr1(I2C_ADDR_TSYS, 0x1E); delay(10);
        if (read_prom(I2C_ADDR_TSYS, tsys_C, 8) && tsys_C[1] != 0 && tsys_C[1] != 0xFFFF) {
            Serial.printf("# TSYS01 PROM k4..k0 = %u %u %u %u %u\n", tsys_C[1], tsys_C[2], tsys_C[3], tsys_C[4], tsys_C[5]);
        } else { Serial.println("# ERROR TSYS01 PROM read failed"); have_tsys = false; }
    }
    if (have_rtc) {
        uint8_t st;
        if (rd_reg(I2C_ADDR_RTC, 0x0F, &st, 1)) {
            Serial.printf("# DS3231 status=0x%02X%s\n", st, (st & 0x80) ? " (OSF=1: 時刻未設定/電池切れ後の初期値)" : "");
        }
    }
    Serial.printf("# sensors: rtc=%d prs=%d tsys=%d ina=%d lis=%d\n", have_rtc, have_prs, have_tsys, have_ina, have_lis);
    step = 0; next_ms = millis();
}

static void read_fast() {
    // DS3231 時刻 + 温度
    R.rtc_ok = false;
    if (have_rtc) {
        uint8_t t[7], tp[2];
        if (rd_reg(I2C_ADDR_RTC, 0x00, t, 7) && rd_reg(I2C_ADDR_RTC, 0x11, tp, 2)) {
            uint8_t hour = (t[2] & 0x40) ? (bcd(t[2] & 0x1F) + ((t[2] & 0x20) ? 12 : 0)) : bcd(t[2] & 0x3F);
            snprintf(R.rtc_time, sizeof(R.rtc_time), "20%02u-%02u-%02uT%02u:%02u:%02u",
                     bcd(t[6]), bcd(t[5] & 0x1F), bcd(t[4]), hour, bcd(t[1]), bcd(t[0]));
            R.rtc_temp = (int8_t)tp[0] + (tp[1] >> 6) * 0.25f;
            R.rtc_ok = true;
        }
    }
    // LIS3MDL (+-4 gauss: 6842 LSB/gauss -> uT = raw / 68.42)
    R.lis_ok = false;
    if (have_lis) {
        uint8_t b[6];
        if (rd_reg(R.lis_addr, 0x80 | 0x28, b, 6)) {
            int16_t x = (int16_t)((b[1] << 8) | b[0]), y = (int16_t)((b[3] << 8) | b[2]), z = (int16_t)((b[5] << 8) | b[4]);
            R.lis_x = x / 68.42f; R.lis_y = y / 68.42f; R.lis_z = z / 68.42f;
            R.lis_ok = true;
        }
    }
    // INA226
    R.ina_ok = false;
    if (have_ina) {
        uint8_t vb[2], vs[2];
        if (rd_reg(I2C_ADDR_INA, 0x02, vb, 2) && rd_reg(I2C_ADDR_INA, 0x01, vs, 2)) {
            uint16_t bus = ((uint16_t)vb[0] << 8) | vb[1];
            int16_t  sh  = (int16_t)(((uint16_t)vs[0] << 8) | vs[1]);
            R.ina_v  = bus * 1.25e-3f;
            R.ina_ma = (sh * 2.5f) / INA_SHUNT_MOHM;    // uV / mΩ = mA
            R.ina_ok = true;
        }
    }
}

static void compute_prs() {
    // MS5837-30BA: 1次 + 2次温度補正 (データシート p.7-8)
    int64_t C1 = prs_C[1], C2 = prs_C[2], C3 = prs_C[3], C4 = prs_C[4], C5 = prs_C[5], C6 = prs_C[6];
    int64_t dT   = (int64_t)prs_D2 - C5 * 256;
    int64_t SENS = C1 * 32768 + (C3 * dT) / 256;
    int64_t OFF  = C2 * 65536 + (C4 * dT) / 128;
    int64_t TEMP = 2000 + (dT * C6) / 8388608;
    int64_t Ti, OFFi, SENSi;
    if (TEMP < 2000) {
        Ti    = (3 * dT * dT) / 8589934592LL;
        OFFi  = (3 * (TEMP - 2000) * (TEMP - 2000)) / 2;
        SENSi = (5 * (TEMP - 2000) * (TEMP - 2000)) / 8;
        if (TEMP < -1500) {
            OFFi  += 7 * (TEMP + 1500) * (TEMP + 1500);
            SENSi += 4 * (TEMP + 1500) * (TEMP + 1500);
        }
    } else {
        Ti    = (2 * dT * dT) / 137438953472LL;
        OFFi  = ((TEMP - 2000) * (TEMP - 2000)) / 16;
        SENSi = 0;
    }
    int64_t OFF2 = OFF - OFFi, SENS2 = SENS - SENSi, TEMP2 = TEMP - Ti;
    int64_t P = (((int64_t)prs_D1 * SENS2) / 2097152 - OFF2) / 8192;   // 0.1 mbar 単位 (30BA)
    R.prs_mbar = P / 10.0f;
    R.prs_temp = TEMP2 / 100.0f;
    R.prs_ok = (prs_D1 != 0 && prs_D2 != 0);
}

static void compute_tsys(uint32_t adc24) {
    float adc = (float)(adc24 >> 8);
    float k4 = tsys_C[1], k3 = tsys_C[2], k2 = tsys_C[3], k1 = tsys_C[4], k0 = tsys_C[5];
    R.tsys_temp = -2.0f * k4 * 1e-21f * powf(adc, 4) + 4.0f * k3 * 1e-16f * powf(adc, 3)
                  - 2.0f * k2 * 1e-11f * powf(adc, 2) + 1.0f * k1 * 1e-6f * adc - 1.5f * k0 * 1e-2f;
    R.tsys_ok = (adc24 != 0);
}

// 1 s 周期の非ブロッキング読み出し:
//   step0: RTC/INA 読み + MS5837 D1 変換開始 + TSYS01 変換開始
//   step1 (+25ms): MS5837 D1 読み + D2 変換開始, TSYS01 ADC 読み
//   step2 (+25ms): MS5837 D2 読み → 計算
void i2c_sensors_tick(uint32_t now) {
    if ((int32_t)(now - next_ms) < 0) return;
    switch (step) {
        case 0:
            read_fast();
            if (have_prs)  wr1(I2C_ADDR_PRS, 0x48);   // D1 OSR=4096
            if (have_tsys) wr1(I2C_ADDR_TSYS, 0x48);  // start ADC
            step = 1; next_ms = now + SENSOR_CONV_MS;
            break;
        case 1: {
            uint32_t v;
            if (have_prs) {
                prs_D1 = read_adc24(I2C_ADDR_PRS, &v) ? v : 0;
                wr1(I2C_ADDR_PRS, 0x58);               // D2 OSR=4096
            }
            if (have_tsys) {
                if (read_adc24(I2C_ADDR_TSYS, &v)) compute_tsys(v); else R.tsys_ok = false;
            }
            step = 2; next_ms = now + SENSOR_CONV_MS;
            break;
        }
        default: {
            uint32_t v;
            if (have_prs) {
                prs_D2 = read_adc24(I2C_ADDR_PRS, &v) ? v : 0;
                compute_prs();
            }
            step = 0; next_ms += SENSOR_PERIOD_MS - 2 * SENSOR_CONV_MS;
            if ((int32_t)(next_ms - now) < 0) next_ms = now + SENSOR_PERIOD_MS;
            break;
        }
    }
}

const I2cReadings& i2c_readings() { return R; }

static uint8_t to_bcd(uint8_t v) { return ((v / 10) << 4) | (v % 10); }

bool i2c_rtc_set(uint16_t y, uint8_t mo, uint8_t d, uint8_t h, uint8_t mi, uint8_t s) {
    if (!have_rtc) return false;
    uint8_t buf[8] = { 0x00, to_bcd(s), to_bcd(mi), to_bcd(h) /* 24h */, 1, to_bcd(d), to_bcd(mo), to_bcd(y % 100) };
    if (!wr(I2C_ADDR_RTC, buf, 8)) return false;
    uint8_t st;
    if (rd_reg(I2C_ADDR_RTC, 0x0F, &st, 1)) {          // OSF クリア
        uint8_t b[2] = { 0x0F, (uint8_t)(st & 0x7F) };
        wr(I2C_ADDR_RTC, b, 2);
    }
    return true;
}
