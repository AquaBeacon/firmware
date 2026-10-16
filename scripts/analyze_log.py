#!/usr/bin/env python3
"""
CHILD_MAIN 動作確認ログの解析・合否判定

使い方:
  python scripts/analyze_log.py reports/<dir>/実験結果.csv                 # 静置ベンチ (既定)
  python scripts/analyze_log.py reports/<dir>/実験結果.csv --expect-buttons  # 3ボタンを押した記録を要求
  python scripts/analyze_log.py <csv> --no-graph                             # PNG を作らない

判定 (README.md「合格基準」と同一):
  A1 行数        : 期待行数(期間×10Hz)の 80% 以上
  A2 V3D        : 全行 v3d=1
  A3 BNO 初期化  : bno=1 の行が 95% 以上
  A4 BNO レート  : GRV/ACC/GYRO 平均 ≥40Hz, MAG ≥15Hz
  A5 四元数ノルム: bno=1 行で |q| が 0.98..1.02
  A6 加速度ノルム: bno=1 行の |a| 中央値が 9.0..10.6 m/s²
  A7 BNO リセット: 初期化後の reset_n 合計 = 0
  A8 ボタン無押下: btn=NONE 行の btn_mv 中央値が 3100..3400 mV
  B1 (--expect-buttons) UP/ENTER/DOWN それぞれ 1 回以上検出、各押下時の mV が期待値 ±150mV
  終了コード: PASS=0 / FAIL=1
"""

import argparse
import csv
import math
import re
import statistics
import sys
from pathlib import Path

EXPECT_MV = {"UP": 0, "ENTER": 820, "DOWN": 1650, "NONE": 3300}
BTN_TOL_MV = 150


def load(path: Path):
    rows, diag = [], []
    with path.open(encoding="utf-8-sig") as f:
        header = None
        for line in f:
            line = line.strip()
            if not line:
                continue
            if line.startswith("#"):
                diag.append(line)
                continue
            if header is None:
                header = line.split(",")
                continue
            parts = line.split(",")
            if len(parts) != len(header):
                continue
            rows.append(dict(zip(header, parts)))
    return rows, diag


def f(row, key):
    try:
        return float(row[key])
    except (KeyError, ValueError):
        return math.nan


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("csv")
    ap.add_argument("--expect-buttons", action="store_true")
    ap.add_argument("--no-graph", action="store_true")
    ap.add_argument("--summary", default=None, help="判定結果を書く Markdown パス (省略時: CSV と同じフォルダの 判定結果.md)")
    args = ap.parse_args()

    path = Path(args.csv)
    if path.is_dir():
        path = path / "実験結果.csv"   # run_test.bat はフォルダを渡す (bat を ASCII に保つため)
    rows, diag = load(path)
    checks = []  # (id, ok, text)
    info = []

    def check(cid, ok, text):
        checks.append((cid, bool(ok), text))

    if not rows:
        print("[FAIL] CSV 行が 0 行。ファームが起動していないか COM ポート違い")
        return 1

    board_line = next((d for d in diag if d.startswith("# board=")), "")
    is_parent = "P-MAIN" in board_line
    info.append("基板: " + (board_line.lstrip("# ") if board_line else "不明 (診断行なし。C-MAIN 扱い)"))

    t0, t1 = f(rows[0], "t_ms"), f(rows[-1], "t_ms")
    span_s = max((t1 - t0) / 1000.0, 0.1)
    expected_rows = span_s * 10
    check("A1", len(rows) >= 0.8 * expected_rows,
          f"行数 {len(rows)} / 期待 {expected_rows:.0f} ({span_s:.1f}s×10Hz)")

    v3d_all = all(r["v3d"] == "1" for r in rows)
    check("A2", v3d_all, "V3D 給電: " + ("全行 OK" if v3d_all else
          f"v3d=0 が {sum(r['v3d'] != '1' for r in rows)} 行 → J_PWR1 pin2 に 3.3V が無い可能性"))

    bno_rows = [r for r in rows if r["bno"] == "1"]
    if is_parent:
        info.append("A3〜A7 (BNO085) は親機に無いので評価しない")
        bno_rows = []
    else:
        check("A3", len(bno_rows) >= 0.95 * len(rows), f"BNO085 初期化済み行 {len(bno_rows)}/{len(rows)}")

    if bno_rows:
        bt0, bt1 = f(bno_rows[0], "t_ms"), f(bno_rows[-1], "t_ms")
        bspan = max((bt1 - bt0) / 1000.0 + 0.1, 0.1)
        rate = {k: sum(f(r, k) for r in bno_rows) / bspan for k in ("quat_n", "acc_n", "gyro_n", "mag_n")}
        check("A4", rate["quat_n"] >= 40 and rate["acc_n"] >= 40 and rate["gyro_n"] >= 40 and rate["mag_n"] >= 15,
              "BNO レート Hz: GRV {quat_n:.1f} ACC {acc_n:.1f} GYRO {gyro_n:.1f} MAG {mag_n:.1f}".format(**rate))

        # 最初の四元数が届く前の行は除外 (qw=1,q=0 の初期値)
        live = [r for r in bno_rows if f(r, "quat_n") > 0 or f(r, "qx") != 0 or f(r, "qy") != 0 or f(r, "qz") != 0]
        norms = [math.sqrt(sum(f(r, k) ** 2 for k in ("qw", "qx", "qy", "qz"))) for r in live]
        bad = [n for n in norms if not (0.98 <= n <= 1.02)]
        check("A5", live and not bad, f"四元数ノルム: {len(live)} 行中 範囲外 {len(bad)} 行"
              + (f" (min {min(norms):.3f} max {max(norms):.3f})" if norms else ""))

        anorm = [math.sqrt(f(r, "ax") ** 2 + f(r, "ay") ** 2 + f(r, "az") ** 2) for r in bno_rows if f(r, "acc_n") > 0 or f(r, "az") != 0]
        amed = statistics.median(anorm) if anorm else math.nan
        check("A6", 9.0 <= amed <= 10.6, f"加速度ノルム中央値 {amed:.3f} m/s² (期待 9.0..10.6)")

        gnorm = [math.sqrt(f(r, "gx") ** 2 + f(r, "gy") ** 2 + f(r, "gz") ** 2) for r in bno_rows]
        gmed = statistics.median(gnorm) if gnorm else math.nan
        info.append(f"角速度ノルム中央値 {gmed:.4f} rad/s (静置なら <0.05 が目安。参考値)")
        mnorm = [math.sqrt(f(r, "mx") ** 2 + f(r, "my") ** 2 + f(r, "mz") ** 2) for r in bno_rows if f(r, "mag_n") > 0]
        if mnorm:
            info.append(f"地磁気ノルム中央値 {statistics.median(mnorm):.1f} uT (地磁気 25..65uT が目安。参考値)")
        yaw = [f(r, "yaw_deg") for r in live]
        if len(yaw) > 10:
            info.append(f"yaw 範囲 {min(yaw):.1f}..{max(yaw):.1f} deg / pitch 範囲 "
                        f"{min(f(r,'pitch_deg') for r in live):.1f}..{max(f(r,'pitch_deg') for r in live):.1f} deg")

        resets = sum(f(r, "reset_n") for r in bno_rows)
        check("A7", resets == 0, f"BNO085 初期化後のリセット回数 {resets:.0f}")
    elif not is_parent:
        for cid in ("A4", "A5", "A6", "A7"):
            check(cid, False, "BNO085 未初期化のため評価不能")

    none_mv = [f(r, "btn_mv") for r in rows if r["btn"] == "NONE"]
    nmed = statistics.median(none_mv) if none_mv else math.nan
    check("A8", 3100 <= nmed <= 3400, f"無押下 BTN_ADC 中央値 {nmed:.0f} mV ({len(none_mv)} 行, 期待 3100..3400)")

    seen = {}
    for r in rows:
        seen.setdefault(r["btn"], []).append(f(r, "btn_mv"))
    dist = ", ".join(f"{k}:{len(v)}行(中央値 {statistics.median(v):.0f}mV)" for k, v in seen.items())
    info.append(f"ボタン分布: {dist}")
    if args.expect_buttons:
        for b in ("UP", "ENTER", "DOWN"):
            mvs = seen.get(b, [])
            ok = bool(mvs) and abs(statistics.median(mvs) - EXPECT_MV[b]) <= BTN_TOL_MV
            check(f"B1-{b}", ok, f"{b}: {len(mvs)} 行"
                  + (f", 中央値 {statistics.median(mvs):.0f} mV (期待 {EXPECT_MV[b]}±{BTN_TOL_MV})" if mvs else " (未検出)"))
    # ---- C: I2C 周辺 (v1.1.0 以降の列があるときだけ) ----
    if "rtc" in rows[0]:
        scan = next((d for d in diag if d.startswith("# I2C scan")), "")
        found = set(int(x, 16) for x in re.findall(r"0x([0-9A-Fa-f]{2})", scan.split("found:")[-1])) if "found:" in scan else set()
        expected = {0x40: "INA226", 0x57: "EEPROM", 0x68: "DS3231", 0x76: "MS5837", 0x77: "TSYS01"}
        if is_parent:
            expected[0x1C] = "LIS3MDL"
        missing = [f"0x{a:02X} {n}" for a, n in expected.items() if a not in found and not (is_parent and a == 0x1C and 0x1E in found)]
        if is_parent:
            # 親機は搭載状況が実験ごとに違うので、DS3231 が見えれば C1 OK。欠落は情報として残す
            check("C1", 0x68 in found, "I2C スキャン: " + " ".join(f"0x{a:02X}" for a in sorted(found))
                  + (f" / 未検出: {', '.join(missing)}" if missing else " (期待すべて検出)"))
        else:
            check("C1", not missing, "I2C スキャン: " + " ".join(f"0x{a:02X}" for a in sorted(found))
                  + (f" / 欠落: {', '.join(missing)}" if missing else " (期待 5 個すべて検出)"))

        times = [r["rtc_time"] for r in rows if r["rtc"] == "1"]
        distinct = len(set(times))
        check("C2", times and distinct >= 0.8 * span_s, f"DS3231 時刻: {len(times)} 行, 異なる時刻 {distinct} 種 (期待 ≥{0.8*span_s:.0f}) "
              + (f"{times[0]} → {times[-1]}" if times else "読めず"))
        osf = next((d for d in diag if "OSF=1" in d), None)
        if osf: info.append("DS3231 OSF=1: 時刻が未設定 (電池切れ後の初期値)。時刻合わせが必要")

        pm = [f(r, "prs_mbar") for r in rows if not math.isnan(f(r, "prs_mbar"))]
        pt = [f(r, "prs_temp") for r in rows if not math.isnan(f(r, "prs_temp"))]
        pmed, ptmed = (statistics.median(pm) if pm else math.nan), (statistics.median(pt) if pt else math.nan)
        if is_parent and not pm:
            info.append("C3 MS5837: データ無し (未接続) → 評価しない")
        else:
            check("C3", pm and 850 <= pmed <= 1100 and 5 <= ptmed <= 45,
                  f"MS5837: 圧力中央値 {pmed:.1f} mbar (期待 850..1100 大気圧), 温度 {ptmed:.2f} degC (期待 5..45)")

        tt = [f(r, "tsys_temp") for r in rows if not math.isnan(f(r, "tsys_temp"))]
        tmed = statistics.median(tt) if tt else math.nan
        if is_parent and not tt:
            info.append("C4 TSYS01: データ無し (未接続) → 評価しない")
        else:
            check("C4", tt and 5 <= tmed <= 45 and (math.isnan(ptmed) or abs(tmed - ptmed) <= 3.0),
                  f"TSYS01: 温度中央値 {tmed:.2f} degC (期待 5..45, MS5837 との差 ≤3.0)")

        iv = [f(r, "ina_v") for r in rows if not math.isnan(f(r, "ina_v"))]
        ia = [f(r, "ina_ma") for r in rows if not math.isnan(f(r, "ina_ma"))]
        vmed, amed_i = (statistics.median(iv) if iv else math.nan), (statistics.median(ia) if ia else math.nan)
        if is_parent and not iv:
            info.append("C5 INA226: データ無し (P-PWR 未接続) → 評価しない")
        else:
          check("C5", iv and 9.0 <= vmed <= 12.7 and 10 <= amed_i <= 500,
              f"INA226: VBAT {vmed:.3f} V (期待 9.0..12.7 = 3S LiPo), 電流 {amed_i:.1f} mA (期待 10..500)"
              + (f", 範囲 {min(ia):.1f}..{max(ia):.1f} mA" if ia else ""))
        rt = [f(r, "rtc_temp") for r in rows if not math.isnan(f(r, "rtc_temp"))]
        if rt: info.append(f"DS3231 内蔵温度中央値 {statistics.median(rt):.2f} degC (参考)")

    # ---- P: 親機固有 (v1.3.0 以降の列があり、親機のとき) ----
    if is_parent and "gnss_n" in rows[0]:
        lx = [f(r, "lis_x") for r in rows if not math.isnan(f(r, "lis_x"))]
        if lx:
            ln = [math.sqrt(f(r, "lis_x") ** 2 + f(r, "lis_y") ** 2 + f(r, "lis_z") ** 2) for r in rows if not math.isnan(f(r, "lis_x"))]
            lmed = statistics.median(ln)
            check("P1", 20 <= lmed <= 80, f"LIS3MDL: 地磁気ノルム中央値 {lmed:.1f} uT ({len(lx)} 行, 期待 20..80)")
        else:
            info.append("P1 LIS3MDL: データ無し (J_MAG1 未接続) → 評価しない")
        g0, g1 = f(rows[0], "gnss_n"), f(rows[-1], "gnss_n")
        fixes = [f(r, "gnss_fix") for r in rows]
        sats = [f(r, "gnss_sat") for r in rows]
        gline = next((d for d in diag if "GNSS first NMEA" in d), "")
        if g1 > 0:
            check("P2", g1 - g0 >= 0.5 * span_s, f"GNSS: NMEA {g1 - g0:.0f} 行 / {span_s:.0f} s (期待 ≥0.5 行/s), fix 最大 {max(fixes):.0f}, 衛星 最大 {max(sats):.0f}"
                  + (f" / {gline.lstrip('# ')}" if gline else ""))
            info.append("GNSS の fix は屋内では 0 が普通。屋外で fix≥1・衛星≥4 を確認する")
        else:
            info.append("P2 GNSS: NMEA 受信 0 行 (J_GNSS 未接続 or ボーレート不一致 38400/9600) → 評価しない")
        p0, p1 = f(rows[0], "pps_n"), f(rows[-1], "pps_n")
        if p1 > 0:
            check("P3", abs((p1 - p0) - span_s) <= max(2, 0.05 * span_s), f"PPS: {p1 - p0:.0f} パルス / {span_s:.0f} s (期待 1 Hz)")
        else:
            info.append("P3 PPS: パルス 0 (GNSS 未接続 or 未 fix) → 評価しない")
        tx = [f(r, "txt_mv") for r in rows]
        txmed = statistics.median(tx) if tx else math.nan
        if txmed < 100:
            info.append(f"P4 TX_TEMP_ADC: {txmed:.0f} mV = J_TXT1 未接続 (R_TTD 100k が引き落とし) → 評価しない")
        else:
            check("P4", 300 <= txmed <= 3200, f"TX_TEMP_ADC: {txmed:.0f} mV (25℃≈1650 / 85℃≈320 / NTC 断線≈3300)")

    # ---- D: microSD (v1.2.0 以降の列があるときだけ) ----
    card_absent = "sd_cd" in rows[0] and all(r["sd_cd"] == "1" for r in rows)
    if "sd" in rows[0] and not card_absent:
        mounted = all(r["sd"] == "1" for r in rows)
        check("D1", mounted, "microSD マウント: " + ("全行 OK" if mounted else f"sd=0 が {sum(r['sd'] != '1' for r in rows)} 行"))
        ver = next((d for d in diag if d.startswith("# SD verify")), "")
        check("D2", "OK" in ver, "書き込み→読み戻し: " + (ver.lstrip("# ") if ver else "診断行なし (s コマンドの出力が元ログに要る)"))
        w0, w1 = f(rows[0], "sd_w"), f(rows[-1], "sd_w")
        expect_w = max(int(span_s / 5) - 1, 1)
        check("D3", w1 - w0 >= expect_w, f"CMLOG.CSV 追記 {w1 - w0:.0f} 回 / 期待 ≥{expect_w} (5 s 周期)")
        cds = sorted(set(r["sd_cd"] for r in rows))
        info.append(f"SD_CD (GP15) 生値: {cds} (カード挿入時の論理として記録)")
        for d in diag:
            if d.startswith("# SD mounted"):
                info.append("診断: " + d.lstrip("# ")); break
    elif "sd" in rows[0]:
        info.append("D1〜D3 microSD: SD_CD=High (カード未挿入) → 評価しない")

    # ---- E: RS485 親子間リンク (v1.4.0 以降) ----
    if "rs_rx" in rows[0]:
        tx0, tx1 = f(rows[0], "rs_tx"), f(rows[-1], "rs_tx")
        rx0, rx1 = f(rows[0], "rs_rx"), f(rows[-1], "rs_rx")
        crc1, bad1 = f(rows[-1], "rs_crc"), f(rows[-1], "rs_bad")
        expected = max(int(span_s * 0.8), 1)
        if is_parent:
            ok = tx1 - tx0 >= expected and rx1 - rx0 >= expected and crc1 == 0 and bad1 == 0
            check("E1", ok, f"RS485 PING/PONG: TX {tx1-tx0:.0f}, RX {rx1-rx0:.0f} / {span_s:.0f}s "
                  f"(期待 各≥{expected}), CRCエラー {crc1:.0f}, 不正フレーム {bad1:.0f}")
        else:
            ok = rx1 - rx0 >= expected and tx1 - tx0 >= expected and crc1 == 0 and bad1 == 0
            check("E1", ok, f"RS485 PING受信/PONG送信: RX {rx1-rx0:.0f}, TX {tx1-tx0:.0f} / {span_s:.0f}s "
                  f"(期待 各≥{expected}), CRCエラー {crc1:.0f}, 不正フレーム {bad1:.0f}")

    seen_diag = set()
    for line in diag:
        if ("BNO085 part=" in line or "ERROR" in line or "WARN" in line or "V3D probe" in line) \
                and line not in seen_diag:
            seen_diag.add(line)
            info.append("診断: " + line.lstrip("# "))

    passed = all(ok for _, ok, _ in checks)
    verdict = "PASS" if passed else "FAIL"

    lines = [f"# 判定結果: {verdict}", "", f"- 対象: `{path}`", f"- 行数: {len(rows)} / 期間 {span_s:.1f} s", "",
             "| # | 判定 | 内容 |", "|---|---|---|"]
    for cid, ok, text in checks:
        lines.append(f"| {cid} | {'OK' if ok else 'NG'} | {text} |")
    lines += ["", "参考値:"] + [f"- {t}" for t in info]
    out = "\n".join(lines)
    print(out)
    summary = Path(args.summary) if args.summary else path.with_name("判定結果.md")
    summary.write_text(out + "\n", encoding="utf-8")

    if not args.no_graph:
        try:
            make_graphs(rows, path.parent)
        except Exception as e:  # matplotlib 無しでも判定は出す
            print(f"[warn] グラフ作成失敗: {e}")

    print(f"\n[{verdict}]")
    return 0 if passed else 1


def make_graphs(rows, outdir: Path):
    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt
    from matplotlib import font_manager

    for name in ("Yu Gothic", "Meiryo", "MS Gothic"):
        if any(fm.name == name for fm in font_manager.fontManager.ttflist):
            plt.rcParams["font.family"] = name
            break

    t = [(f(r, "t_ms") - f(rows[0], "t_ms")) / 1000.0 for r in rows]
    fig, axes = plt.subplots(4, 1, figsize=(10, 11), sharex=True)
    axes[0].plot(t, [f(r, "yaw_deg") for r in rows], label="yaw")
    axes[0].plot(t, [f(r, "pitch_deg") for r in rows], label="pitch")
    axes[0].plot(t, [f(r, "roll_deg") for r in rows], label="roll")
    axes[0].set_ylabel("deg"); axes[0].legend(loc="upper right"); axes[0].set_title("BNO085 姿勢 (Game Rotation Vector)")
    for k in ("ax", "ay", "az"):
        axes[1].plot(t, [f(r, k) for r in rows], label=k)
    axes[1].set_ylabel("m/s²"); axes[1].legend(loc="upper right"); axes[1].set_title("加速度")
    for k in ("gx", "gy", "gz"):
        axes[2].plot(t, [f(r, k) for r in rows], label=k)
    axes[2].set_ylabel("rad/s"); axes[2].legend(loc="upper right"); axes[2].set_title("角速度")
    axes[3].plot(t, [f(r, "btn_mv") for r in rows], color="C3")
    for name, mv in EXPECT_MV.items():
        axes[3].axhline(mv, color="gray", lw=0.5, ls="--")
        axes[3].text(t[-1], mv, name, fontsize=8, va="bottom", ha="right", color="gray")
    axes[3].set_ylabel("mV"); axes[3].set_xlabel("s"); axes[3].set_title("BTN_ADC (GP27)")
    fig.tight_layout()
    fig.savefig(outdir / "姿勢・加速度・角速度・ボタン.png", dpi=110)
    plt.close(fig)

    fig, ax = plt.subplots(figsize=(10, 3.5))
    for k, lab in (("quat_n", "GRV"), ("acc_n", "ACC"), ("gyro_n", "GYRO"), ("mag_n", "MAG")):
        ax.plot(t, [f(r, k) * 10 for r in rows], label=lab)
    ax.set_ylabel("Hz (100ms 窓×10)"); ax.set_xlabel("s"); ax.legend(loc="upper right")
    ax.set_title("BNO085 レポートレート")
    fig.tight_layout()
    fig.savefig(outdir / "BNOレート.png", dpi=110)
    plt.close(fig)


if __name__ == "__main__":
    sys.exit(main())
