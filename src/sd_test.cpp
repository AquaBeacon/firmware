// microSD (TF-01A, SPI1: SCK GP10 / MOSI GP11 / MISO GP12 / CS GP13, CD GP15)
#include "sd_test.h"
#include <SPI.h>
#include <SD.h>
#include "child_main_pins.h"

static SdState S = { false, false, 0, 0, 0, 0, 0, "NOT_STARTED" };
static uint32_t next_log_ms = 0;

static bool mount(uint32_t hz) {
    SD.end();
    if (SD.begin(PIN_SD_CS, hz, SPI1)) { S.hz = hz; return true; }
    return false;
}

static bool verify_file() {
    char msg[64];
    snprintf(msg, sizeof(msg), "CHILD_MAIN sd test t=%lu\n", (unsigned long)millis());
    SD.remove(SD_TEST_FILE);
    File f = SD.open(SD_TEST_FILE, FILE_WRITE);
    if (!f) { S.error = "OPEN_WRITE_FAILED"; return false; }
    size_t w = f.print(msg);
    f.close();
    if (w != strlen(msg)) { S.error = "SHORT_WRITE"; return false; }
    f = SD.open(SD_TEST_FILE, FILE_READ);
    if (!f) { S.error = "OPEN_READ_FAILED"; return false; }
    char buf[64] = {0};
    size_t r = f.readBytes(buf, sizeof(buf) - 1);
    f.close();
    if (r != strlen(msg) || strcmp(buf, msg) != 0) { S.error = "READBACK_MISMATCH"; return false; }
    return true;
}

void sd_setup() {
    pinMode(PIN_SD_CD, INPUT);           // 基板の R_CD 47k がプルアップ。内蔵プルは使わず論理を素で見る
    S.cd_raw = digitalRead(PIN_SD_CD);
    pinMode(PIN_SD_CS, OUTPUT);
    digitalWrite(PIN_SD_CS, HIGH);
    SPI1.setRX(PIN_SD_MISO);
    SPI1.setTX(PIN_SD_MOSI);
    SPI1.setSCK(PIN_SD_SCK);
    Serial.printf("# SD init: SPI1 SCK=%d MOSI=%d MISO=%d CS=%d CD=GP%d(raw=%d)\n",
                  PIN_SD_SCK, PIN_SD_MOSI, PIN_SD_MISO, PIN_SD_CS, PIN_SD_CD, S.cd_raw);

    S.mounted = mount(SD_SPI_HZ_FIRST);
    if (!S.mounted) {
        S.error = "MOUNT_FAILED_1MHZ";
        Serial.println("# ERROR SD mount failed at 1MHz (カード挿入/向き, R_SDCS, 配線, カードのフォーマット FAT16/32 を確認)");
        return;
    }
    // 1MHz で通ったら既定速度 (SD ライブラリ既定 = 4MHz 相当) を試す。失敗したら 1MHz に戻す
    if (!mount(SPI_HALF_SPEED)) {
        Serial.println("# WARN SD mount at default speed failed -> 1MHz に戻す");
        S.mounted = mount(SD_SPI_HZ_FIRST);
        if (!S.mounted) { S.error = "REMOUNT_FAILED"; return; }
    }
    S.size_bytes = SD.size64();
    const char* ft = SD.fatType() == 32 ? "FAT32" : SD.fatType() == 16 ? "FAT16" : SD.fatType() == 64 ? "exFAT" : "?";
    Serial.printf("# SD mounted: %s %.2f GB spi=%lu Hz\n", ft, S.size_bytes / 1e9, (unsigned long)S.hz);

    S.verify_ok = verify_file();
    Serial.printf("# SD verify (%s write->read): %s%s%s\n", SD_TEST_FILE, S.verify_ok ? "OK" : "FAILED",
                  S.verify_ok ? "" : " ", S.verify_ok ? "" : S.error);
    // ルートの一覧 (最大 10)
    File root = SD.open("/");
    int n = 0;
    if (root) {
        for (File e = root.openNextFile(); e && n < 10; e = root.openNextFile(), n++) {
            Serial.printf("# SD file: %s %lu bytes\n", e.name(), (unsigned long)e.size());
            e.close();
        }
        root.close();
    }
    S.error = S.verify_ok ? "NONE" : S.error;
    next_log_ms = millis() + SD_LOG_PERIOD_MS;
}

void sd_tick(uint32_t now, const char* csv_row) {
    S.cd_raw = digitalRead(PIN_SD_CD);
    if (!S.mounted || (int32_t)(now - next_log_ms) < 0) return;
    next_log_ms += SD_LOG_PERIOD_MS;
    File f = SD.open(SD_LOG_FILE, FILE_WRITE);   // 追記モード
    if (!f) { S.writes_fail++; S.error = "LOG_OPEN_FAILED"; return; }
    size_t w = f.print(csv_row);
    w += f.print("\n");
    f.close();
    if (w == strlen(csv_row) + 1) S.writes_ok++; else { S.writes_fail++; S.error = "LOG_SHORT_WRITE"; }
}

const SdState& sd_state() { return S; }
