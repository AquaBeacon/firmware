// AquaBeacon メイン基板 動作確認ファーム (C-MAIN 既定 / -DBOARD_PARENT で P-MAIN)
//   確認対象: Pico 2 W 自身 / LED_STATUS / 3ボタン ADC ラダー / BNO085 (SPI0, 子機) / I2C センサ / microSD /
//             GNSS・PPS・TX 温度・LIS3MDL (親機)
//   出力: 115200bps。'#' 始まりは診断行、それ以外は CSV (列は CSV_HEADER 固定。scripts/analyze_log.py が依存)
//   シリアルコマンド: h=ヘッダ再送  r=BNO085 再初期化  f=V3D 判定を無視して BNO085 init 強制(切り分け用)
//                     l=LED 点灯/消灯トグル(手動確認用)
#include <Arduino.h>
#include <SPI.h>
#ifndef BOARD_PARENT
#include <Adafruit_BNO08x.h>
#endif
#include <math.h>
#include "child_main_pins.h"
#include "i2c_devices.h"
#include "sd_test.h"
#include "parent_devices.h"
#include "rs485_test.h"

#define FW_NAME "AquaBeacon MAIN self-test"
#define FW_VERSION "1.4.0"

// 列は末尾追加のみ (scripts/serial_monitor.py, analyze_log.py と同期)
static const char CSV_HEADER[] =
    "seq,t_ms,v3d,btn,btn_mv,bno,qw,qx,qy,qz,yaw_deg,pitch_deg,roll_deg,"
    "ax,ay,az,gx,gy,gz,mx,my,mz,quat_n,acc_n,gyro_n,mag_n,reset_n,"
    "rtc,rtc_time,rtc_temp,prs_mbar,prs_temp,tsys_temp,ina_v,ina_ma,"
    "sd,sd_cd,sd_w,"
    "lis_x,lis_y,lis_z,gnss_n,gnss_fix,gnss_sat,pps_n,txt_mv,"
    "rs_tx,rs_rx,rs_crc,rs_bad,rs_age_ms";

enum Btn { BTN_NONE = 0, BTN_UP, BTN_ENTER, BTN_DOWN };
static const char* BTN_NAME[] = { "NONE", "UP", "ENTER", "DOWN" };

#ifndef BOARD_PARENT
static Adafruit_BNO08x bno(PIN_BNO_RST);
#endif

static bool     bno_ok = false;
static bool     v3d_ok = false;
static uint32_t v3d_last_check_ms = 0;
static uint32_t seq = 0;
static uint32_t last_log_ms = 0;
static uint32_t last_led_ms = 0;
static bool     led_state = false;
static bool     led_manual = false;   // 'l' コマンドで固定点灯
static Btn      btn_prev = BTN_NONE;

// 直近 100ms 区間の集計
static uint32_t quat_n = 0, acc_n = 0, gyro_n = 0, mag_n = 0, reset_n = 0;
static uint32_t reset_total = 0;
static float qw = 1, qx = 0, qy = 0, qz = 0;
static float ax = 0, ay = 0, az = 0;
static float gx = 0, gy = 0, gz = 0;
static float mx = 0, my = 0, mz = 0;

// ---------------------------------------------------------------- ADC / ボタン
static uint32_t read_btn_mv() {
    uint32_t sum = 0;
    for (int i = 0; i < ADC_OVERSAMPLE; i++) sum += analogRead(PIN_BTN_ADC);
    uint32_t raw = sum / ADC_OVERSAMPLE;
    return (raw * (uint32_t)ADC_VREF_MV) / ((1u << ADC_BITS) - 1);
}

static Btn classify_btn(uint32_t mv) {
    if (mv < BTN_UP_MAX_MV)    return BTN_UP;
    if (mv < BTN_ENTER_MAX_MV) return BTN_ENTER;
    if (mv < BTN_DOWN_MAX_MV)  return BTN_DOWN;
    return BTN_NONE;
}

// ---------------------------------------------------------------- BNO085 (子機のみ)
#ifndef BOARD_PARENT
static bool bno_enable_reports() {
    bool ok = true;
    ok &= bno.enableReport(SH2_GAME_ROTATION_VECTOR, BNO_REPORT_US);
    ok &= bno.enableReport(SH2_ACCELEROMETER,        BNO_REPORT_US);
    ok &= bno.enableReport(SH2_GYROSCOPE_CALIBRATED, BNO_REPORT_US);
    ok &= bno.enableReport(SH2_MAGNETIC_FIELD_CALIBRATED, BNO_MAG_REPORT_US);
    if (!ok) Serial.println("# ERROR BNO085 enableReport failed");
    return ok;
}

static void bno_print_ids() {
    Serial.printf("# BNO085 prodIds entries=%u\n", (unsigned)bno.prodIds.numEntries);
    for (int i = 0; i < bno.prodIds.numEntries; i++) {
        const sh2_ProductId_t& e = bno.prodIds.entry[i];
        Serial.printf("# BNO085 part=%lu sw=%u.%u.%u build=%lu\n",
                      (unsigned long)e.swPartNumber, e.swVersionMajor, e.swVersionMinor,
                      e.swVersionPatch, (unsigned long)e.swBuildNumber);
    }
}

static bool bno_init() {
    SPI.setRX(PIN_BNO_MISO);
    SPI.setSCK(PIN_BNO_SCK);
    SPI.setTX(PIN_BNO_MOSI);
    Serial.printf("# BNO085 init: SPI0 MISO=%d SCK=%d MOSI=%d CS=%d INT=%d RST=%d %luHz\n",
                  PIN_BNO_MISO, PIN_BNO_SCK, PIN_BNO_MOSI, PIN_BNO_CS, PIN_BNO_INT, PIN_BNO_RST,
                  (unsigned long)BNO_SPI_HZ);
    if (!bno.begin_SPI(PIN_BNO_CS, PIN_BNO_INT, &SPI)) {
        Serial.println("# ERROR BNO085 not detected (begin_SPI failed). "
                       "check: V3D at J_BNO pin1, P0/P1=V3D, INT/RST wiring");
        return false;
    }
    bno_print_ids();
    (void)bno.wasReset();   // 初期化に伴うリセットフラグを消費 (以降の wasReset は予期しないリセットだけ)
    if (!bno_enable_reports()) return false;
    Serial.println("# BNO085 OK: reports enabled (GRV/ACC/GYRO 50Hz, MAG 20Hz)");
    return true;
}

static void bno_poll() {
    if (!bno_ok) return;
    if (bno.wasReset()) {
        reset_n++; reset_total++;
        Serial.printf("# WARN BNO085 was reset (total %lu) -> re-enable reports\n",
                      (unsigned long)reset_total);
        bno_enable_reports();
    }
    sh2_SensorValue_t v;
    // 1ループで溜まった分をすべて吸い出す
    for (int guard = 0; guard < 16 && bno.getSensorEvent(&v); guard++) {
        switch (v.sensorId) {
            case SH2_GAME_ROTATION_VECTOR:
                qw = v.un.gameRotationVector.real;
                qx = v.un.gameRotationVector.i;
                qy = v.un.gameRotationVector.j;
                qz = v.un.gameRotationVector.k;
                quat_n++;
                break;
            case SH2_ACCELEROMETER:
                ax = v.un.accelerometer.x; ay = v.un.accelerometer.y; az = v.un.accelerometer.z;
                acc_n++;
                break;
            case SH2_GYROSCOPE_CALIBRATED:
                gx = v.un.gyroscope.x; gy = v.un.gyroscope.y; gz = v.un.gyroscope.z;
                gyro_n++;
                break;
            case SH2_MAGNETIC_FIELD_CALIBRATED:
                mx = v.un.magneticField.x; my = v.un.magneticField.y; mz = v.un.magneticField.z;
                mag_n++;
                break;
            default: break;
        }
    }
}

#else
static void bno_print_ids() {}
static bool bno_init() { return false; }
static void bno_poll() {}
#endif

static void quat_to_euler(float w, float x, float y, float z, float& yaw, float& pitch, float& roll) {
    const float sqw = w * w, sqx = x * x, sqy = y * y, sqz = z * z;
    yaw   = atan2f(2.0f * (x * y + z * w), sqx - sqy - sqz + sqw) * 180.0f / (float)M_PI;
    float s = -2.0f * (x * z - y * w) / (sqx + sqy + sqz + sqw);
    if (s > 1.0f) s = 1.0f; if (s < -1.0f) s = -1.0f;
    pitch = asinf(s) * 180.0f / (float)M_PI;
    roll  = atan2f(2.0f * (y * z + x * w), -sqx - sqy + sqz + sqw) * 180.0f / (float)M_PI;
}

// ---------------------------------------------------------------- ヘッダ / V3D
static void print_header() {
    Serial.printf("# %s v%s (built %s %s)\n", FW_NAME, FW_VERSION, __DATE__, __TIME__);
#ifndef BOARD_PARENT
    Serial.printf("# board=%s mcu=Pico2W LED=GP%d BTN_ADC=GP%d BNO=SPI0(GP%d/%d/%d/%d) INT=GP%d RST=GP%d\n",
                  AQB_BOARD_NAME, PIN_LED_STATUS, PIN_BTN_ADC, PIN_BNO_MISO, PIN_BNO_CS, PIN_BNO_SCK, PIN_BNO_MOSI,
                  PIN_BNO_INT, PIN_BNO_RST);
#else
    Serial.printf("# board=%s mcu=Pico2W LED=GP%d BTN_ADC=GP%d (BNO085 なし)\n", AQB_BOARD_NAME, PIN_LED_STATUS, PIN_BTN_ADC);
    parent_print_state();
#endif
    Serial.printf("# btn thresholds mV: UP<%d ENTER<%d DOWN<%d else NONE ; V3D present if idle BTN_ADC>=%d\n",
                  BTN_UP_MAX_MV, BTN_ENTER_MAX_MV, BTN_DOWN_MAX_MV, V3D_PRESENT_MIN_MV);
    Serial.printf("# state: v3d=%d bno=%d bno_reset_total=%lu\n", v3d_ok, bno_ok, (unsigned long)reset_total);
    if (bno_ok) bno_print_ids();
    i2c_print_scan();
    {
        const SdState& sd = sd_state();
        Serial.printf("# SD state: mounted=%d verify=%d spi=%lu size=%.2fGB cd_raw=%d writes_ok=%lu fail=%lu err=%s\n",
                      sd.mounted, sd.verify_ok, (unsigned long)sd.hz, sd.size_bytes / 1e9, sd.cd_raw,
                      (unsigned long)sd.writes_ok, (unsigned long)sd.writes_fail, sd.error);
    }
    Serial.println(CSV_HEADER);
}

// V3D の受動プローブ (何も駆動しない)
//   Adafruit 4754 は SCL(=SCK) / SDA(=MISO) に 10k プルアップ (モジュール内 3Vo) が付いている。
//   Pico 側を INPUT_PULLDOWN (約 50-80k) にして読むと、モジュールに電源があれば High、無ければ Low になる。
//   BTN_ADC (V3D→R_LAD 10k) と合わせて「V3D 無し」と「UP ボタン短絡」を切り分ける。
struct V3dProbe { uint32_t btn_mv; bool miso_hi; bool sck_hi; bool int_hi; };

static V3dProbe probe_v3d() {
    V3dProbe p;
#ifndef BOARD_PARENT
    if (!bno_ok) {
        pinMode(PIN_BNO_MISO, INPUT_PULLDOWN);
        pinMode(PIN_BNO_SCK,  INPUT_PULLDOWN);
        pinMode(PIN_BNO_INT,  INPUT_PULLDOWN);
        delay(2);
        p.miso_hi = digitalRead(PIN_BNO_MISO);
        p.sck_hi  = digitalRead(PIN_BNO_SCK);
        p.int_hi  = digitalRead(PIN_BNO_INT);
    } else {
        p.miso_hi = p.sck_hi = p.int_hi = true;
    }
#else
    p.miso_hi = p.sck_hi = p.int_hi = false;   // 親機に BNO085 は無い
#endif
    p.btn_mv = read_btn_mv();
    return p;
}

// V3D 未給電なら BNO085 を触らない (無給電デバイスに GPIO から電圧を掛けない)
static void check_v3d_and_init_bno(bool force = false) {
    V3dProbe p = probe_v3d();
    bool by_btn = p.btn_mv >= V3D_PRESENT_MIN_MV;
    bool by_bno = p.miso_hi && p.sck_hi;
    bool present = by_btn || by_bno;
    static bool warned_absent = false, warned_btn = false;
    static uint8_t probe_prints = 0;
    if (present != v3d_ok || probe_prints < 3) {   // 状態変化時と最初の 3 回だけ (2 s ごとの再確認で埋めない)
        probe_prints++;
#ifndef BOARD_PARENT
        Serial.printf("# V3D probe: BTN_ADC=%lu mV, BNO MISO=%d SCK=%d INT=%d (pull-down read) -> %s\n",
                      (unsigned long)p.btn_mv, p.miso_hi, p.sck_hi, p.int_hi, present ? "present" : "ABSENT");
#else
        Serial.printf("# V3D probe: BTN_ADC=%lu mV -> %s\n", (unsigned long)p.btn_mv, present ? "present" : "ABSENT");
#endif
    }
    if (present && !by_btn && !warned_btn) {
        warned_btn = true;
        Serial.println("# WARN V3D は有るが BTN_ADC が低い -> UP ボタン短絡 / R_LAD 未実装 / BTN_ADC-GND 短絡を疑う");
    }
    if (!present && !warned_absent) {
        warned_absent = true;
#ifndef BOARD_PARENT
        Serial.printf("# WARN V3D ABSENT: J_PWR1 pin2 (V3D) に 3.3V が来ていない。"
                      "C-PWR 未接続なら外部 3.3V を pin2/GND(pin4) へ、または Pico 3V3(物理36)→V3D をベンチ用ジャンパ。"
                      "BNO085 init を保留 (%d ms ごとに再確認)\n", V3D_RETRY_MS);
#else
        Serial.printf("# WARN V3D ABSENT: J_PWR1 pin5/6 (V3D) に 3.3V が来ていない。"
                      "P-PWR 未接続なら外部 3.3V を pin5/6, GND=pin3/4 へ、または Pico 3V3(物理36)→V3D をベンチ用ジャンパ。"
                      "I2C/SD/GNSS は V3D 給電なので応答しない (%d ms ごとに再確認)\n", V3D_RETRY_MS);
#endif
    }
    if (present) warned_absent = false;
    v3d_ok = present;
#ifndef BOARD_PARENT
    if ((v3d_ok || force) && !bno_ok) {
        if (force && !v3d_ok) Serial.println("# cmd f: V3D 無しのまま BNO085 init を強制");
        bno_ok = bno_init();
    }
#else
    (void)force;
#endif
}

// ---------------------------------------------------------------- コマンド
static void handle_serial() {
    while (Serial.available()) {
        int c = Serial.read();
        switch (c) {
            case 'h': print_header(); break;
            case 'r':
                Serial.println("# cmd r: BNO085 re-init");
                bno_ok = false;
                v3d_last_check_ms = 0;
                break;
            case 'f':   // V3D 判定を無視して BNO085 init を 1 回試す (切り分け用。無給電なら応答しない)
                bno_ok = false;
                check_v3d_and_init_bno(true);
                v3d_last_check_ms = millis();
                break;
            case 'l':
                led_manual = !led_manual;
                Serial.printf("# cmd l: LED manual=%d\n", led_manual);
                break;
            case 'i':   // I2C 再スキャン + センサ再初期化
                i2c_print_scan();
                i2c_sensors_setup();
                break;
            case 's':   // microSD 再マウント + 書き込み/読み戻し検証
                sd_setup();
                break;
            case 'T': { // TYYYYMMDDHHMMSS : DS3231 に時刻を書く (OSF もクリア)
                char d[15] = {0};
                uint32_t t0 = millis();
                int n = 0;
                while (n < 14 && millis() - t0 < 500) {
                    if (Serial.available()) { int ch = Serial.read(); if (ch >= '0' && ch <= '9') d[n++] = (char)ch; else break; }
                }
                if (n == 14) {
                    uint16_t y = atoi(String(d).substring(0, 4).c_str());
                    uint8_t mo = atoi(String(d).substring(4, 6).c_str()), da = atoi(String(d).substring(6, 8).c_str());
                    uint8_t h = atoi(String(d).substring(8, 10).c_str()), mi = atoi(String(d).substring(10, 12).c_str());
                    uint8_t s = atoi(String(d).substring(12, 14).c_str());
                    bool ok = i2c_rtc_set(y, mo, da, h, mi, s);
                    Serial.printf("# cmd T: DS3231 set %04u-%02u-%02uT%02u:%02u:%02u -> %s\n", y, mo, da, h, mi, s, ok ? "OK" : "FAILED");
                } else {
                    Serial.printf("# cmd T: 14 桁の数字が必要 (got %d)\n", n);
                }
                break;
            }
            default: break;
        }
    }
}

// ---------------------------------------------------------------- setup / loop
void setup() {
    pinMode(PIN_LED_STATUS, OUTPUT);
    digitalWrite(PIN_LED_STATUS, HIGH);   // 起動直後は点灯 (電源投入の目視確認)
    analogReadResolution(ADC_BITS);

    Serial.begin(SERIAL_BAUD);
    uint32_t t0 = millis();
    while (!Serial && millis() - t0 < SERIAL_WAIT_MS) delay(10);

    check_v3d_and_init_bno();
    v3d_last_check_ms = millis();
    i2c_init();
    print_header();          // I2C スキャン結果もここで出る
    i2c_sensors_setup();
    sd_setup();
    parent_setup();
    rs485_setup();
    digitalWrite(PIN_LED_STATUS, LOW);
}

void loop() {
    handle_serial();
    bno_poll();
    i2c_sensors_tick(millis());
    parent_tick(millis());
    rs485_tick(millis());

    uint32_t now = millis();
    if (!bno_ok && now - v3d_last_check_ms >= V3D_RETRY_MS) {
        v3d_last_check_ms = now;
        check_v3d_and_init_bno();
    }

    uint32_t btn_mv = read_btn_mv();
    Btn btn = classify_btn(btn_mv);
    if (btn != btn_prev) {
        Serial.printf("# BTN %s -> %s (%lu mV)\n", BTN_NAME[btn_prev], BTN_NAME[btn], (unsigned long)btn_mv);
        btn_prev = btn;
    }

    // LED: 手動固定 > ボタン押下中は点灯固定 > 1Hz ハートビート
    if (led_manual) {
        digitalWrite(PIN_LED_STATUS, HIGH);
    } else if (btn != BTN_NONE) {
        digitalWrite(PIN_LED_STATUS, HIGH);
    } else if (now - last_led_ms >= LED_HEARTBEAT_MS) {
        last_led_ms = now;
        led_state = !led_state;
        digitalWrite(PIN_LED_STATUS, led_state ? HIGH : LOW);
    }

    if (now - last_log_ms >= LOG_PERIOD_MS) {
        last_log_ms += LOG_PERIOD_MS;
        if (now - last_log_ms > LOG_PERIOD_MS) last_log_ms = now;  // 大きく遅れたら追従
        float yaw = 0, pitch = 0, roll = 0;
        if (bno_ok) quat_to_euler(qw, qx, qy, qz, yaw, pitch, roll);
        const I2cReadings& s = i2c_readings();
        const SdState& sd = sd_state();
        const ParentState& pd = parent_state();
        const Rs485State& rs = rs485_state();
        static char row[512];
        snprintf(row, sizeof(row),
                 "%lu,%lu,%d,%s,%lu,%d,%.4f,%.4f,%.4f,%.4f,%.1f,%.1f,%.1f,"
                 "%.3f,%.3f,%.3f,%.4f,%.4f,%.4f,%.2f,%.2f,%.2f,%lu,%lu,%lu,%lu,%lu,"
                 "%d,%s,%.2f,%.1f,%.2f,%.2f,%.3f,%.1f,%d,%d,%lu,"
                 "%.1f,%.1f,%.1f,%lu,%u,%u,%lu,%lu,%lu,%lu,%lu,%lu,%lu",
                 (unsigned long)seq++, (unsigned long)now, v3d_ok ? 1 : 0, BTN_NAME[btn],
                 (unsigned long)btn_mv, bno_ok ? 1 : 0,
                 qw, qx, qy, qz, yaw, pitch, roll,
                 ax, ay, az, gx, gy, gz, mx, my, mz,
                 (unsigned long)quat_n, (unsigned long)acc_n, (unsigned long)gyro_n,
                 (unsigned long)mag_n, (unsigned long)reset_n,
                 s.rtc_ok ? 1 : 0, s.rtc_ok ? s.rtc_time : "-",
                 s.rtc_ok ? s.rtc_temp : NAN, s.prs_ok ? s.prs_mbar : NAN, s.prs_ok ? s.prs_temp : NAN,
                 s.tsys_ok ? s.tsys_temp : NAN, s.ina_ok ? s.ina_v : NAN, s.ina_ok ? s.ina_ma : NAN,
                 sd.mounted ? 1 : 0, sd.cd_raw, (unsigned long)sd.writes_ok,
                 s.lis_ok ? s.lis_x : NAN, s.lis_ok ? s.lis_y : NAN, s.lis_ok ? s.lis_z : NAN,
                 (unsigned long)pd.gnss_lines, pd.gnss_fix, pd.gnss_sats,
                 (unsigned long)pd.pps_count, (unsigned long)pd.txt_mv,
                 (unsigned long)rs.tx_ok, (unsigned long)rs.rx_ok,
                 (unsigned long)rs.crc_fail, (unsigned long)rs.malformed,
                 (unsigned long)(rs.last_rx_ms ? now - rs.last_rx_ms : 0xFFFFFFFFu));
        Serial.println(row);
        sd_tick(now, row);   // 5 s ごとに CMLOG.CSV へ追記
        quat_n = acc_n = gyro_n = mag_n = reset_n = 0;
    }
}
