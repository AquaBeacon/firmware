# AGENTS.md

AI エージェント (Claude Code / Codex 等) がこのリポジトリで作業するときの前提・制約・確認手順・完了条件。

## Project Overview

- **Project name**: CHILD_MAIN_test (AquaBeacon C-MAIN v4.5 / P-MAIN v5.8 基板ブリングアップ。親機は env:parent = -DBOARD_PARENT)
- **Purpose**: 子機メイン基板に載せた Pico 2 W / LED_STATUS / 3 ボタン ADC ラダー / BNO085 (SPI0) の動作確認
- **対象ハード**: Raspberry Pi Pico 2 W (RP2350A) ×1、C-MAIN v4.5、Adafruit BNO085 (4754)
- **開発環境**: Windows 11 / PlatformIO / VS Code。Python 3 (pyserial, matplotlib)
- **実機通信**: USB-シリアル 115200bps

## First Steps

1. `README.md` (前提: V3D 給電、合格基準、コマンド)
2. `include/child_main_pins.h` (ピン・しきい値の唯一の定義場所 = 値の正)
3. `src/main.cpp`
4. `scripts/run_test.bat` (テストループの流れ)
5. `reports/` の最新フォルダ (直近の結果と次の確認事項)

上位資料: `../docs/AquaBeacon_MAIN_UI_GPIO_コネクタ表.md` (GPIO 割当の正)、
`../hardware/pcb/BRINGUP_BOARDS.md` §3 (ブリングアップ手順・期待電圧)。

## Core Rules

- **ピン番号・しきい値は `include/child_main_pins.h` だけで定義する。main.cpp に数値を直書きしない**
- **CSV の列順・列名を変えない** (`scripts/analyze_log.py` が依存)。列を足すときは末尾に追加し、analyze 側も同時に直す
- **無給電のデバイスに GPIO から電圧を掛けない**: V3D 無しのとき BNO085 の SPI を初期化しない設計を壊さない
- BNO085 の SPI クロックは 1 MHz 以下 (Adafruit 4754 の SCL/SDA はレベルシフタ+10k プルアップ経由)
- 依頼された範囲に集中し、関係ない変更を避ける。変更は小さくレビューしやすい単位に
- 実在しないコマンドを推測で実行しない (`platformio.ini` と `scripts/` を確認)
- 最後に、変更内容・検証結果・残課題を報告する

## Commands

```bat
pio run                                        REM ビルド
pio run -t upload --upload-port COM10          REM 書き込み
scripts\run_test.bat [name] [--expect-buttons] REM build→flash→monitor→analyze
python scripts\analyze_log.py reports\<dir>    REM 解析のみ
```

## Experiment Report Artifacts (実機実験ごとに必須)

実機で計測したら、成功・失敗・中断にかかわらず `reports/YYYYMMDD_HHMMSS_<実験名>/` を新規作成し、
既存フォルダを上書きしない。ビルドのみ・静的解析のみは対象外。

```
reports/YYYYMMDD_HHMMSS_<実験名>/
├── 前提条件.md      # 日時・目的・搭載部品・電源 (V3D の供給方法!)・GPIO・COM・制約・元ログのパス
├── 元ログ.csv       # 診断行込みの全受信行 (serial_monitor.py が生成)
├── 実験結果.csv     # 解析対象 CSV (列固定)
├── 判定結果.md      # analyze_log.py が生成
├── 簡易レポート.md  # 結果、PASS / FAIL / INCONCLUSIVE、異常、次の確認事項
└── *.png            # analyze_log.py が生成 (姿勢・加速度・角速度・ボタン, BNO レート)
```

## Definition of Done

- `pio run` が警告なしで通る
- 実機で `scripts\run_test.bat` を実行し、`reports/` に上記一式がある
- `簡易レポート.md` に PASS / FAIL / INCONCLUSIVE と次の確認事項が書かれている
- 変更内容・検証結果・残課題を報告している
