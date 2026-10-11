#!/usr/bin/env python3
"""Summarize real C++ simulation CSVs; optionally render comparison plots."""

import argparse
import csv
import json
from pathlib import Path


def read_case(path):
    with path.open() as stream:
        return list(csv.DictReader(stream))


def summarize(name, rows):
    # Use only the final minute, well after the external step/manual intervention.
    window = [row for row in rows if int(row["time_ms"]) >= 240000]
    applied = [int(row["applied_brightness"]) for row in window]
    targets = [int(row["target_brightness"]) for row in window]
    directions = [1 if b > a else -1 for a, b in zip(applied, applied[1:]) if a != b]
    return {
        "case": name,
        "learning": rows[0]["learning"],
        "coupling_lux_per_percent": float(rows[0]["coupling"]),
        "preferred_brightness": int(rows[0]["preferred_brightness"]),
        "initial_baseline_lux": float(rows[0]["baseline_lux"]),
        "final_baseline_lux": float(rows[-1]["baseline_lux"]),
        "final_score": float(rows[-1]["score"]),
        "last60s_min_brightness": min(applied),
        "last60s_max_brightness": max(applied),
        "last60s_target_changes": sum(a != b for a, b in zip(targets, targets[1:])),
        "last60s_direction_reversals": sum(a != b for a, b in zip(directions, directions[1:])),
    }


def render_plots(directory, cases):
    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt
    from matplotlib import font_manager

    font = Path("/System/Library/Fonts/Supplemental/AppleGothic.ttf")
    if font.exists():
        font_manager.fontManager.addfont(str(font))
        plt.rcParams["font.family"] = font_manager.FontProperties(fname=str(font)).get_name()
    plt.rcParams.update({"axes.unicode_minus": False, "font.size": 11})
    blue, orange, slate = "#2875d5", "#dd8426", "#637386"

    def line(axis, name, column, label, color, start=0, stop=80):
        rows = [row for row in cases[name] if start <= int(row["time_ms"])/1000 <= stop]
        axis.plot([int(row["time_ms"])/1000 for row in rows],
                  [float(row[column]) for row in rows], label=label, color=color, linewidth=1.6)

    fig, axes = plt.subplots(3, 1, figsize=(11, 9), constrained_layout=True)
    fig.suptitle("수정한 C++ 알고리즘: 출력 변경을 분리하고 안정적으로 복귀", fontsize=18, fontweight="bold")
    prefix = "closed-loop-p100-k0.5-"
    line(axes[0], prefix+"learn", "applied_brightness", "현재 자동 학습", blue, 60, 65)
    line(axes[0], prefix+"freeze", "applied_brightness", "시험용 기준 고정", orange, 60, 65)
    axes[0].set_title("① 외부 밝기 증가가 유지되는 동안 완화 밝기를 안정적으로 유지", loc="left")
    axes[0].set_ylabel("조명 출력 (%)")
    axes[0].set_ylim(0, 105)
    axes[0].set_xlabel("시간 (초), 외부 밝기 변화가 끝난 뒤 구간")
    prefix = "manual-return-p100-k0.5-"
    line(axes[1], prefix+"learn", "baseline_lux", "현재 자동 학습 기준", blue)
    line(axes[1], prefix+"freeze", "baseline_lux", "시험용 고정 기준", orange)
    axes[1].axvspan(10, 40, color="#dae4f2", alpha=.5, label="수동 밝기 25%")
    axes[1].set_title("② 수동으로 어둡게 한 30초 동안 자체 출력 변화는 비교 기준에서 제외", loc="left")
    axes[1].set_ylabel("학습 기준 (lx)")
    line(axes[2], prefix+"learn", "applied_brightness", "현재 자동 학습", blue)
    line(axes[2], prefix+"freeze", "applied_brightness", "시험용 기준 고정", orange)
    axes[2].axvspan(10, 40, color="#dae4f2", alpha=.5, label="수동 밝기 25%")
    axes[2].axvline(40, color=slate, linestyle="--", linewidth=1)
    axes[2].set_title("③ 수동 사용 후 자동 전환: 평소 밝기로 복귀", loc="left")
    axes[2].set_ylabel("조명 출력 (%)")
    axes[2].set_ylim(0, 105)
    axes[2].set_xlabel("시간 (초), 40초에 자동 모드 복귀")
    for axis in axes:
        axis.grid(alpha=.18)
        axis.legend(loc="lower right", fontsize=10)
    fig.savefig(directory/"feedback-comparison.png", dpi=150)
    plt.close(fig)

    fig, axes = plt.subplots(3, 1, figsize=(11, 8), constrained_layout=True)
    fig.suptitle("주변 입력만 바꾸는 비교 시험 (조명 빛의 센서 기여 없음)", fontsize=17, fontweight="bold")
    scenarios = [("daylight-slow-learn", "서서히 밝아짐: 100→110 lx, 0.05 lx/초"),
                 ("daylight-fast-learn", "빠르게 밝아진 뒤 유지: 100→200 lx, 2 lx/초"),
                 ("dim-restore-learn", "어두운 입력 30초 후 원래 밝기 복원: 100→40→100 lx")]
    for axis, (name, title) in zip(axes, scenarios):
        line(axis, name, "lux", "센서 입력", slate, stop=300)
        line(axis, name, "baseline_lux", "현재 학습 기준", blue, stop=300)
        axis.set_title(title, loc="left")
        axis.set_ylabel("밝기 (lx)")
        axis.grid(alpha=.18)
        axis.legend(loc="best")
    axes[-1].set_xlabel("시간 (초)")
    fig.savefig(directory/"environment-baselines.png", dpi=150)
    plt.close(fig)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("directory", type=Path)
    parser.add_argument("--plot", action="store_true", help="Requires matplotlib; CSV/JSON summary does not.")
    parser.add_argument("--check", action="store_true", help="Assert steady output, manual return and recovery in the modeled cases.")
    args = parser.parse_args()
    cases = {path.stem: read_case(path) for path in sorted(args.directory.glob("*.csv")) if path.name != "summary.csv"}
    if not cases:
        parser.error("No simulation CSVs found")
    summaries = [summarize(name, rows) for name, rows in cases.items()]
    if args.check:
        for summary in summaries:
            assert summary["last60s_target_changes"] == 0, summary
            assert summary["last60s_direction_reversals"] == 0, summary
            if summary["case"].startswith(("manual-return-", "closed-restore-")):
                assert summary["last60s_min_brightness"] == summary["preferred_brightness"], summary
            if summary["case"].startswith("manual-return-"):
                assert abs(summary["initial_baseline_lux"] - summary["final_baseline_lux"]) < .001, summary
        print(f"Steady output and recovery assertions passed: {len(summaries)} cases")
    with (args.directory/"summary.csv").open("w") as stream:
        writer = csv.DictWriter(stream, fieldnames=list(summaries[0]))
        writer.writeheader()
        writer.writerows(summaries)
    (args.directory/"summary.json").write_text(json.dumps(summaries, ensure_ascii=False, indent=2)+"\n")
    if args.plot:
        render_plots(args.directory, cases)
    print(f"Summarized {len(cases)} cases: {args.directory}")


if __name__ == "__main__":
    main()
