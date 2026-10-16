# CHILD_MAIN_test — Claude Code 向け補足

## First Steps
毎回、作業前に以下を読むこと。
1. `AGENTS.md` (作業ルール・完了条件・レポート規則)
2. `README.md` (V3D 給電の前提、合格基準)
3. `include/child_main_pins.h` (ピン・しきい値の唯一の定義場所)

## 環境
- OS: Windows 11 / PlatformIO CLI (`~/.platformio/penv/Scripts/pio.exe`。PATH に無い場合はフルパス)
- board: rpipico2w (platform = maxgerhardt/platform-raspberrypi、TD04A_test と同じ)
- Python 3.13 / pyserial / matplotlib。コンソール文字化け対策に `PYTHONIOENCODING=utf-8`

## COM ポート (実機接続後に変更すること)
CM_PORT=COM10   (USB\VID_2E8A&PID_F00F = Pico 2 W CDC)

確認方法: デバイスマネージャー →「ポート (COMとLPT)」、または PowerShell で
`Get-CimInstance Win32_PnPEntity | ? Name -match 'COM\d+' | select Name`

## ハードの落とし穴
- **V3D は J_PWR1 pin2 からしか来ない** (Pico 3V3 物理36 は NC)。USB だけでは BNO085 もボタンも動かない
- ベンチ用に Pico 3V3 → V3D をジャンパしたら、**C-PWR を繋ぐ前に必ず外す**
- BNO085 の P0/P1 は基板上で V3D 固定 = SPI モード。WAKE は使えない
- BNO085 SPI クロック ≤ 1 MHz

## 実機実験レポート
`AGENTS.md` の「Experiment Report Artifacts」が正。`reports/YYYYMMDD_HHMMSS_<実験名>/` を実験ごとに新規作成し、
`前提条件.md` / `元ログ.csv` / `実験結果.csv` / `判定結果.md` / `簡易レポート.md` / `*.png` を揃える。
