// AquaBeacon メイン基板 ピン定義・判定しきい値 — このファイルが唯一の正
//   既定 = CHILD_MAIN (C-MAIN v4.5)。-DBOARD_PARENT で PARENT_MAIN (P-MAIN v5.8)。
// 根拠: docs/AquaBeacon_MAIN_UI_GPIO_コネクタ表.md「C-MAIN / P-MAIN Pico 2 W GPIO割当」
//       Kicad_project/pcb/build_child_main.py / build_parent_main.py (PICO1 ピン割当 / ADC ラダー定数)
//       Kicad_project/pcb/BRINGUP_BOARDS.md §3-5 / §3-6
#pragma once

#ifdef BOARD_PARENT
#define AQB_BOARD_NAME          "P-MAIN v5.8"
#define PIN_LED_STATUS      2           // 状態 LED (GP2)
#define PIN_BTN_ADC         26          // ADC0
#else
#define AQB_BOARD_NAME          "C-MAIN v4.5"
#define PIN_LED_STATUS      14          // 状態 LED (赤 0805, R_ST 1k, GP14 High で点灯)
#define PIN_BTN_ADC         27          // ADC1
#endif

// ---- 3ボタン ADC 抵抗ラダー (V3D -> R_LAD 10k -> BTN_ADC) 両基板共通 ----
//   無押下 = 3.3V / UP = 0V / ENTER = 0.82V (R_E 3.3k) / DOWN = 1.65V (R_D 10k)
#define ADC_BITS            12
#define ADC_VREF_MV         3300
#define ADC_OVERSAMPLE      16
// 分類境界 (各期待値の中点): UP<410 / ENTER<1235 / DOWN<2475 / それ以上 NONE
#define BTN_UP_MAX_MV       410
#define BTN_ENTER_MAX_MV    1235
#define BTN_DOWN_MAX_MV     2475

// ---- V3D 給電判定 ----
//   V3D は J_PWR1 (電源基板の LDO) からしか来ない。Pico 3V3(物理36) は NC。
//   無押下時の BTN_ADC ≈ V3D なので、起動時に BTN_ADC がこれ未満なら V3D 未給電(または UP 押下中)とみなし、
//   無給電の BNO085 に GPIO から電圧を掛けないよう SPI 初期化を保留する (子機)。
#define V3D_PRESENT_MIN_MV  2800
#define V3D_RETRY_MS        2000

#ifndef BOARD_PARENT
// ---- BNO085 (Adafruit 4754, 専用 SPI0, P0/P1 = V3D 固定で SPI モード) 子機のみ ----
#define PIN_BNO_MISO        16          // SPI0 RX  <- BNO SDA(MISO)
#define PIN_BNO_CS          17          // GPIO out
#define PIN_BNO_SCK         18          // SPI0 SCK <- BNO SCL(SCK)
#define PIN_BNO_MOSI        19          // SPI0 TX  -> BNO DI(MOSI)
#define PIN_BNO_INT         20          // GPIO in  (H_INTN, データレディ)
#define PIN_BNO_RST         21          // GPIO out (NRST)
// Adafruit 4754 は SCL/SDA がレベルシフタ+10k プルアップ経由なので SPI クロックは 1MHz 以下
// (Adafruit_BNO08x ライブラリの既定 = 1MHz。ここは記録用で、ライブラリ側の値が実効値)
#define BNO_SPI_HZ          1000000
#define BNO_REPORT_US       20000       // 各レポート 50Hz
#define BNO_MAG_REPORT_US   50000       // 地磁気 20Hz
#else
// ---- 親機固有 ----
#define PIN_GNSS_PPS        6           // NEO-M9N PPS (1Hz)。R 100Ω 直列で SPRESENSE にも分配
#define PIN_EVENT_MARK      7           // SPRESENSE イベントマーカー出力 (本ファームでは Low 固定)
#define PIN_GNSS_TX         8           // UART1 TX -> NEO-M9N RX
#define PIN_GNSS_RX         9           // UART1 RX <- NEO-M9N TX
#define PIN_TX_PWM          16          // PWM8A -> J_TX1 (本ファームでは Low 固定 = 送信停止)
#define PIN_TX_TEMP_ADC     27          // ADC1: TX NTC 分圧 (25℃≈1.65V / 85℃≈0.32V / 未接続≈0V)
#define GNSS_BAUD_FIRST     38400       // NEO-M9N 既定。NMEA が来なければ 9600 を試す
#define GNSS_BAUD_SECOND    9600
#define GNSS_AUTOBAUD_MS    3000
#endif

// ---- ログ ----
#define SERIAL_BAUD         115200
#define SERIAL_WAIT_MS      3000        // USB CDC 接続待ち上限
#define LOG_PERIOD_MS       100         // CSV 1行 / 100ms
#define LED_HEARTBEAT_MS    500         // 通常時 1Hz 点滅 (ボタン押下中は点灯固定)

// ---- I2C0 バス (DS3231+AT24C32 / MS5837 / TSYS01 / INA226(電源基板) / LCD / LIS3MDL(親機)) ----
#define PIN_I2C_SDA         4
#define PIN_I2C_SCL         5
#define I2C_HZ              100000
#define I2C_ADDR_LIS_A      0x1C        // LIS3MDL (Adafruit 4479 既定。親機 J_MAG1)
#define I2C_ADDR_LIS_B      0x1E        // LIS3MDL (SDO=High のとき)
#define I2C_ADDR_LCD        0x3E        // NHD-C0216CiZ (未接続なら不在で正常)
#define I2C_ADDR_INA        0x40        // INA226 (電源基板上。XH 経由)
#define I2C_ADDR_EEPROM     0x57        // AT24C32 (DS3231 モジュール同梱)
#define I2C_ADDR_RTC        0x68        // DS3231
#define I2C_ADDR_PRS        0x76        // MS5837-30BA
#define I2C_ADDR_TSYS       0x77        // TSYS01 (Celsius)
#define INA_SHUNT_MOHM      20          // C-PWR / P-PWR R_SH 20mΩ (build_*_pwr.py)
#define SENSOR_PERIOD_MS    1000        // 低速センサの読み出し周期
#define SENSOR_CONV_MS      25          // MS5837 OSR4096 (≤18.1ms) / TSYS01 (≤9.1ms) の変換待ち

// ---- RS485 DATA (Waveshare 絶縁・自動方向, UART0) ----
#define PIN_RS485_TX         0
#define PIN_RS485_RX         1
#define RS485_BAUD           115200
#define RS485_PING_MS        1000
#define RS485_REPLY_GUARD_MS 5

// ---- microSD (TF-01A, 専用 SPI1) 両基板共通 ----
#define PIN_SD_SCK          10
#define PIN_SD_MOSI         11
#define PIN_SD_MISO         12
#define PIN_SD_CS           13          // R_SDCS 10k で V3D にプルアップ
#define PIN_SD_CD           15          // R_CD 47k で V3D にプルアップ。実測: カード挿入で Low (C-MAIN 2026-09-13)
#define SD_SPI_HZ_FIRST     1000000     // まず 1MHz でマウント (配線長 ≈35mm)。通れば既定速度で再試行
#define SD_LOG_PERIOD_MS    5000        // CMLOG.CSV へ追記する周期
#define SD_TEST_FILE        "CMTEST.TXT"
#define SD_LOG_FILE         "CMLOG.CSV"
