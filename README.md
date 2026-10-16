# CHILD_MAIN_test — C-MAIN v4.5 基板 動作確認

AquaBeacon 子機メイン基板 (`hardware/pcb/CHILD_MAIN`, C-MAIN v4.5) のブリングアップ用テストベンチ。
`TD04A_test` と同じ流儀 (PlatformIO + Python 監視/解析 + `reports/` へ実験レポート) で、
**Pico 2 W / LED_STATUS / 3 ボタン ADC ラダー / BNO085 (SPI0)** を確認する。

## 構成

```
firmware/
├── platformio.ini            ← env:pico2w (TD04A_test と同じ platform/board)
├── include/child_main_pins.h ← ピン・しきい値の唯一の正 (GPIO 表準拠)
├── src/main.cpp              ← 自己診断ファーム (CSV 10Hz + 診断行)
├── scripts/
│   ├── run_test.bat          ← build → flash → monitor → analyze
│   ├── serial_monitor.py     ← 受信して 実験結果.csv / 元ログ.csv を書く
│   ├── analyze_log.py        ← 合否判定 + 判定結果.md + PNG
│   └── requirements.txt
└── reports/YYYYMMDD_HHMMSS_<実験名>/  ← 実験ごとに 前提条件.md / 簡易レポート.md / CSV / PNG
```

## 前提 (重要): V3D の給電

C-MAIN の **V3D は J_PWR1 pin2 (C-PWR の LDO) からしか来ない**。Pico の 3V3 (物理36) は基板上 NC。
BNO085 の VIN・ボタンラダー (R_LAD) は V3D なので、**Pico を USB で動かすだけでは BNO085 もボタンも動かない**。
単体ベンチでは次のどちらかで V3D を供給する。

| 方法 | 手順 | 注意 |
|---|---|---|
| 外部 3.3V | J_PWR1 pin2 (V3D) / pin4 (GND) へ。電流制限 100 mA | 推奨 |
| ベンチ用ジャンパ | Pico 物理36 (3V3) ↔ J_PWR1 pin2 (V3D) | **C-PWR を繋ぐ前に必ず外す** (LDO 出力同士が衝突) |

ファームは起動時に V3D の有無を **受動的に** 判定する (BTN_ADC ≥ 2.8 V、または BNO085 側 10k プルアップ線
MISO/SCK をプルダウン読みして High)。V3D が無ければ無給電の BNO085 に GPIO から電圧を掛けないよう
SPI 初期化を保留し、2 s ごとに再確認する。

## 使い方

```bat
REM COM ポートは scripts\run_test.bat 冒頭の CM_PORT (子機, 既定 COM10) / PM_PORT (親機, 既定 COM5) を実機に合わせる
pip install -r scripts\requirements.txt

scripts\run_test.bat                          REM 子機 静置ベンチ 30s → reports\<日時>_child_bench\
scripts\run_test.bat buttons --expect-buttons REM 子機 最初の押下から 30s 記録 (最大 300s 待つ) → B1 判定
scripts\run_test.bat parent                   REM 親機 (env:parent, PM_PORT) 静置ベンチ
scripts\run_test.bat parent --expect-buttons  REM 親機 ボタン
```

## 親機 (P-MAIN v5.8) 対応 (v1.3.0〜)

同じソースを `-DBOARD_PARENT` (env:parent) でビルドすると親機用になる。違いは `include/child_main_pins.h` の
BOARD_PARENT 側だけ: LED GP2 / BTN_ADC GP26、BNO085 なし、LIS3MDL (0x1C/0x1E) を I²C で読む、
GNSS を UART1 (GP8/GP9, 38400→9600 autobaud) で受けて NMEA 行数・GGA の fix/衛星数を数える、PPS (GP6) の立ち上がりを数える、
TX_TEMP_ADC (GP27) を mV で読む。TX_PWM (GP16) と EVENT_MARK (GP7) は Low 固定。
親機の判定は A3〜A7 を省き、C3〜C5 / D / P1〜P4 は「データがあるときだけ」評価する (搭載状況が実験ごとに違うため)。
V3D は J_PWR1 pin5/6 (P-PWR の LDO) からしか来ない点は子機と同じ。

`--expect-buttons` のときは `serial_monitor.py --wait-for-btn 300` が付き、最初のボタン変化 (`# BTN` 行) を
検出してから `--duration` 秒を計る。押すタイミングを気にせず実行できる。

手動:

```bat
pio run
pio run -t upload --upload-port COM10
python scripts\serial_monitor.py --port COM10 --duration 30 --report-dir reports\<日時>_<名前>
python scripts\analyze_log.py reports\<日時>_<名前> [--expect-buttons]
```

シリアルコマンド (115200bps): `h` ヘッダ再送 / `r` BNO085 再初期化 / `f` V3D 判定を無視して BNO085 init 強制 (切り分け用) / `l` LED 固定点灯トグル / `i` I²C 再スキャン+センサ再初期化 / `s` microSD 再マウント+検証 / `T`+14桁 DS3231 時刻設定。

## 確認対象とピン

| 対象 | GPIO | 期待 |
|---|---|---|
| LED_STATUS (赤) | GP14 | 通常 1 Hz 点滅、ボタン押下中は点灯固定 |
| BTN_ADC ラダー | GP27 (ADC1) | 無押下 3.3 V / UP 0 V / ENTER 0.82 V / DOWN 1.65 V |
| BNO085 (Adafruit 4754) | SPI0: MISO GP16 / CS GP17 / SCK GP18 / MOSI GP19, INT GP20, RST GP21 | prodId 応答、GRV/ACC/GYRO 50 Hz、MAG 20 Hz |

## 合格基準 (`analyze_log.py`)

| # | 内容 |
|---|---|
| A1 | CSV 行数が期待 (期間×10 Hz) の 80% 以上 |
| A2 | 全行 v3d=1 (V3D 給電あり) |
| A3 | bno=1 の行が 95% 以上 |
| A4 | GRV / ACC / GYRO 平均 ≥ 40 Hz、MAG ≥ 15 Hz |
| A5 | 四元数ノルム 0.98..1.02 |
| A6 | 加速度ノルム中央値 9.0..10.6 m/s² |
| A7 | 初期化後の BNO085 リセット回数 0 |
| A8 | 無押下 BTN_ADC 中央値 3100..3400 mV |
| B1 | (`--expect-buttons`) UP / ENTER / DOWN それぞれ検出、押下時 mV が期待値 ±150 |
| C1 | (v1.1.0〜) I²C スキャンで 0x40 INA226 / 0x57 EEPROM / 0x68 DS3231 / 0x76 MS5837 / 0x77 TSYS01 を検出 |
| C2 | DS3231 の時刻が進んでいる (異なる時刻が期間の 80% 以上) |
| C3 | MS5837 圧力中央値 850..1100 mbar、温度 5..45 ℃ |
| C4 | TSYS01 温度 5..45 ℃、MS5837 との差 3 ℃ 以内 |
| C5 | INA226 の VBAT 9.0..12.7 V (3S LiPo)、電流 10..500 mA |
| D1 | (v1.2.0〜) microSD が全行マウント済み |
| D2 | `CMTEST.TXT` の書き込み→読み戻しが一致 (`s` コマンドの診断行) |
| D3 | `CMLOG.CSV` への 5 s 周期追記が期間分できている |

C1〜C5 は CSV に I²C 列があるとき (ファーム v1.1.0 以降)、D1〜D3 は SD 列があるとき (v1.2.0 以降) だけ評価する。
LED の点滅と、ボタン押下中の点灯固定は目視で確認する (ログでは判定しない)。

## microSD (v1.2.0〜)

SPI1 (SCK GP10 / MOSI GP11 / MISO GP12 / CS GP13)、カード検出 GP15 (R_CD 47 kΩ プルアップ、実測: 挿入で 0)。
起動時と `s` コマンドで 1 MHz → 4 MHz の順にマウントし、`CMTEST.TXT` を書いて読み戻し比較、以降 5 s ごとに CSV 行を
`CMLOG.CSV` へ追記する。CSV の `sd_w` が累計追記回数。

## I²C 周辺 (v1.1.0〜)

I2C0 (SDA GP4 / SCL GP5, 100 kHz)。起動時と `i` コマンドでスキャンし、存在するデバイスだけ 1 s 周期で読む
(変換待ちはブロッキングせず 25 ms ステップで回すので BNO085 の 50 Hz を邪魔しない)。

| デバイス | アドレス | 読むもの |
|---|---|---|
| DS3231 (RTC モジュール) | 0x68 | 時刻、内蔵温度、OSF フラグ (時刻未設定の検出) |
| AT24C32 (同モジュール) | 0x57 | スキャンのみ |
| MS5837-30BA (Bar30) | 0x76 | 圧力 mbar、温度 (OSR 4096、2 次補正あり) |
| TSYS01 (Celsius) | 0x77 | 温度 (4 次多項式) |
| INA226 (C-PWR、XH9 経由) | 0x40 | VBAT_M 電圧、シャント電流 (R_SH 20 mΩ) |

## CSV 列 (変更しない。`analyze_log.py` が依存。追加は末尾のみ)

```
seq,t_ms,v3d,btn,btn_mv,bno,qw,qx,qy,qz,yaw_deg,pitch_deg,roll_deg,ax,ay,az,gx,gy,gz,mx,my,mz,quat_n,acc_n,gyro_n,mag_n,reset_n,rtc,rtc_time,rtc_temp,prs_mbar,prs_temp,tsys_temp,ina_v,ina_ma,sd,sd_cd,sd_w
```

`*_n` は直前 100 ms 窓で届いたレポート数 (×10 で Hz)。`reset_n` は BNO085 の予期しないリセット回数。
I²C 列は 1 s 周期で更新され、読めないデバイスは `nan` (rtc_time は `-`)。v1.0.0 (27 列) / v1.1.0 (35 列) の CSV も解析できる。

## 参考資料

- `../docs/AquaBeacon_MAIN_UI_GPIO_コネクタ表.md` — GPIO 割当の正
- `../hardware/pcb/BRINGUP_BOARDS.md` §3 — C-MAIN ブリングアップ手順 (電圧期待値)
- `../hardware/pcb/CHILD_MAIN/部品一覧.md` — BNO085 4754 の SPI 配線・1 MHz 上限の根拠

## 開発とライセンス

[開発ガイド](DEVELOPMENT.md)にセットアップ・入口・確認方法をまとめています。自作部分は [Apache License 2.0](LICENSE)。[第三者素材・依存の扱い](THIRD_PARTY.md)も参照してください。
