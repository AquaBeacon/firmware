#!/usr/bin/env python3
"""
CHILD_MAIN 動作確認ファームのシリアルモニター
指定秒数だけ受信して、CSV 行を 実験結果.csv に、全行(診断行込み)を 元ログ.csv に保存する。

使い方:
  python scripts/serial_monitor.py --port COM10 --duration 30 --report-dir reports/20260912_120000_bench
  python scripts/serial_monitor.py --port COM10 --duration 30 --out logs/cm_001.csv   (旧来形式)

起動直後に 'h' を送ってヘッダ(ファーム版・ピン割当・BNO085 prodId)を取り直す。
"""

import argparse
import csv
import sys
import time
from datetime import datetime
from pathlib import Path

import serial

CSV_HEADER = (
    "seq,t_ms,v3d,btn,btn_mv,bno,qw,qx,qy,qz,yaw_deg,pitch_deg,roll_deg,"
    "ax,ay,az,gx,gy,gz,mx,my,mz,quat_n,acc_n,gyro_n,mag_n,reset_n,"
    "rtc,rtc_time,rtc_temp,prs_mbar,prs_temp,tsys_temp,ina_v,ina_ma,"
    "sd,sd_cd,sd_w,"
    "lis_x,lis_y,lis_z,gnss_n,gnss_fix,gnss_sat,pps_n,txt_mv,"
    "rs_tx,rs_rx,rs_crc,rs_bad,rs_age_ms"
)
CSV_COLUMNS = len(CSV_HEADER.split(","))
CSV_COLUMNS_OLD = (27, 35, 38)   # v1.0.0 / v1.1.0 / v1.2.0 の列数も受け付ける


def monitor(port: str, baud: float, duration: float, result_path: Path, raw_path: Path,
            quiet: bool = False, wait_for_btn: float = 0.0, set_rtc: bool = False) -> int:
    """wait_for_btn > 0 のとき、最初の '# BTN' 行 (ボタン変化) を検出してから duration 秒を計る。
    最大 wait_for_btn 秒待っても押されなければそのまま終了する (記録は開始時点から残す)。"""
    result_path.parent.mkdir(parents=True, exist_ok=True)
    raw_path.parent.mkdir(parents=True, exist_ok=True)

    print(f"[monitor] ポート: {port}, {baud}bps, {duration}秒間受信")
    print(f"[monitor] 実験結果: {result_path}")
    print(f"[monitor] 元ログ  : {raw_path}")

    try:
        ser = serial.Serial(port, baud, timeout=0.2)
    except serial.SerialException as e:
        print(f"[ERROR] シリアルポートを開けません: {e}")
        print(f"  → COMポートを確認してください (現在: {port})")
        raise

    time.sleep(0.3)
    ser.reset_input_buffer()
    if set_rtc:
        stamp = datetime.now().strftime("%Y%m%d%H%M%S")
        ser.write(b"T" + stamp.encode())   # DS3231 に PC 時刻 (ローカル) を書く
        time.sleep(0.3)
    ser.write(b"i")  # I2C 再スキャン + センサ再初期化 (診断行を元ログに残す)
    time.sleep(0.3)
    ser.write(b"s")  # microSD 再マウント + 書き込み/読み戻し検証 (診断行を元ログに残す)
    time.sleep(1.0)
    ser.write(b"h")  # ヘッダ再送

    start = time.time()
    rows = 0
    diag = 0
    header_seen = False
    armed = wait_for_btn <= 0          # False の間は「最初のボタン押下待ち」
    deadline = start + (wait_for_btn if wait_for_btn > 0 else duration)
    if not armed:
        print(f"[monitor] ボタン押下待ち (最大 {wait_for_btn:.0f}s)。押されてから {duration:.0f}s 記録します")

    with raw_path.open("w", encoding="utf-8-sig", newline="") as raw_f, \
            result_path.open("w", encoding="utf-8", newline="") as res_f:
        raw_w = csv.writer(raw_f)
        raw_w.writerow(["timestamp", "line"])
        res_f.write("# CHILD_MAIN self-test log\n")
        res_f.write(f"# Captured: {datetime.now().isoformat()}\n")
        res_f.write(f"# Port: {port}\n")
        res_f.write(CSV_HEADER + "\n")

        while time.time() < deadline:
            try:
                raw = ser.readline()
            except serial.SerialException as e:
                print(f"[ERROR] 受信中にポートが切れました: {e}")
                break
            if not raw:
                continue
            line = raw.decode("utf-8", errors="replace").strip()
            if not line:
                continue
            raw_w.writerow([datetime.now().isoformat(timespec="milliseconds"), line])
            raw_f.flush()

            if line.startswith("#"):
                diag += 1
                if not armed and line.startswith("# BTN"):
                    armed = True
                    deadline = time.time() + duration
                    print(f"[monitor] ボタン押下を検出。ここから {duration:.0f}s 記録します")
                print(f"  {line}")
                res_f.write(line + "\n")
                res_f.flush()
                continue
            if line.startswith("seq,t_ms,"):
                header_seen = True
                if line != CSV_HEADER:
                    print(f"  [monitor] ファームの列が既定と違う: {len(line.split(','))} 列 (既定 {CSV_COLUMNS})")
                    # 実験結果.csv のヘッダをファーム側に合わせて書き直す
                    res_f.seek(0); res_f.truncate()
                    res_f.write("# CHILD_MAIN self-test log\n")
                    res_f.write(f"# Captured: {datetime.now().isoformat()}\n")
                    res_f.write(f"# Port: {port}\n")
                    res_f.write(line + "\n")
                continue
            parts = line.split(",")
            if (len(parts) == CSV_COLUMNS or len(parts) in CSV_COLUMNS_OLD) and parts[0].isdigit():
                rows += 1
                if not quiet and rows % 10 == 1:
                    print(f"  {line}")
                res_f.write(line + "\n")
                res_f.flush()
            else:
                print(f"  ?? {line}")

    ser.close()
    elapsed = time.time() - start
    print(f"\n[monitor] 完了: {elapsed:.1f}秒, CSV {rows}行, 診断 {diag}行, header={'あり' if header_seen else 'なし'}")
    return rows


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", default="COM10", help="Pico の COM ポート (C-MAIN=COM10 / P-MAIN=COM5 の実績)")
    parser.add_argument("--baud", default=115200, type=int)
    parser.add_argument("--duration", default=30, type=float, help="受信秒数")
    parser.add_argument("--report-dir", default=None,
                        help="reports/<日時_実験名>/ を指定すると 実験結果.csv / 元ログ.csv をそこへ書く")
    parser.add_argument("--out", default=None, help="(旧来) CSV 出力パス。元ログは <out>_events.csv")
    parser.add_argument("--quiet", action="store_true", help="CSV 行の画面表示を省く")
    parser.add_argument("--wait-for-btn", default=0.0, type=float, metavar="SEC",
                        help="最初のボタン押下を最大 SEC 秒待ち、押されてから --duration 秒を計る")
    parser.add_argument("--set-rtc", action="store_true", help="開始時に DS3231 へ PC の現在時刻を書く")
    args = parser.parse_args()

    if args.report_dir:
        d = Path(args.report_dir)
        result_path = d / "実験結果.csv"
        raw_path = d / "元ログ.csv"
    else:
        if args.out is None:
            ts = datetime.now().strftime("%Y%m%d_%H%M%S")
            args.out = f"logs/cm_{ts}.csv"
        result_path = Path(args.out)
        raw_path = result_path.with_name(result_path.stem + "_events.csv")

    rows = monitor(args.port, args.baud, args.duration, result_path, raw_path, args.quiet,
                   args.wait_for_btn, args.set_rtc)
    return 0 if rows > 0 else 1


if __name__ == "__main__":
    sys.exit(main())
