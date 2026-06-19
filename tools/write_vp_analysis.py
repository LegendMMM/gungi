import argparse
import csv
from pathlib import Path


def count_rows(path):
    if not Path(path).exists():
        return 0
    with open(path, newline="") as handle:
        reader = csv.reader(handle)
        next(reader, None)
        return sum(1 for _row in reader)


def last_csv_row(path):
    if not Path(path).exists():
        return None
    last = None
    with open(path, newline="") as handle:
        reader = csv.DictReader(handle)
        for row in reader:
            last = row
    return last


def summarize_compare(path):
    if not Path(path).exists():
        return None
    wins = losses = draws = timeouts = 0
    black = {"wins": 0, "losses": 0, "draws": 0}
    white = {"wins": 0, "losses": 0, "draws": 0}
    rows = 0
    with open(path, newline="") as handle:
        reader = csv.DictReader(handle)
        for row in reader:
            rows += 1
            side = row.get("model_side", "")
            timed_out = row.get("timed_out", "0") == "1"
            model_win = row.get("model_win", "0") == "1"
            model_loss = row.get("model_loss", "0") == "1"
            if timed_out or (not model_win and not model_loss):
                draws += 1
                if side == "B":
                    black["draws"] += 1
                elif side == "W":
                    white["draws"] += 1
            elif model_win:
                wins += 1
                if side == "B":
                    black["wins"] += 1
                elif side == "W":
                    white["wins"] += 1
            else:
                losses += 1
                if side == "B":
                    black["losses"] += 1
                elif side == "W":
                    white["losses"] += 1
            if timed_out:
                timeouts += 1
    return {
        "path": path,
        "rows": rows,
        "wins": wins,
        "losses": losses,
        "draws": draws,
        "timeouts": timeouts,
        "black": black,
        "white": white,
    }


def parse_args():
    parser = argparse.ArgumentParser(description="Write a VP analysis markdown file from generated CSV data.")
    parser.add_argument("--sample-csv", default="train_vp_hybrid_student_100000.csv")
    parser.add_argument("--candidate-csv", default="train_vp_hybrid_student_100000_candidates.csv")
    parser.add_argument("--gpu-csv", default="train_vp_hybrid_student_100000_gpu.csv")
    parser.add_argument("--model", default="models/vp_hybrid_student_100000.bin")
    parser.add_argument("--compare-csv", action="append", default=[])
    parser.add_argument("--out", default="analysis_reports/vp_hybrid_student_100000_analysis.md")
    return parser.parse_args()


def main():
    args = parse_args()
    sample_rows = count_rows(args.sample_csv)
    candidate_rows = count_rows(args.candidate_csv)
    gpu_last = last_csv_row(args.gpu_csv)
    compare_summaries = [summary for summary in (summarize_compare(path) for path in args.compare_csv) if summary]

    out_path = Path(args.out)
    out_path.parent.mkdir(parents=True, exist_ok=True)

    lines = [
        "# VP Hybrid Student 100000 Analysis",
        "",
        "## Artifact Index",
        "",
        f"- Sample summary CSV: `{args.sample_csv}` ({sample_rows} rows)",
        f"- Candidate CSV: `{args.candidate_csv}` ({candidate_rows} rows)",
        f"- GPU metrics CSV: `{args.gpu_csv}`",
        f"- Model: `{args.model}`",
        "",
        "## Training Metrics",
        "",
    ]

    if gpu_last:
        lines.extend(
            [
                f"- Last epoch: {gpu_last.get('epoch')}",
                f"- Value loss: {gpu_last.get('value_loss')}",
                f"- Policy loss: {gpu_last.get('policy_loss')}",
                f"- Total loss: {gpu_last.get('total_loss')}",
                f"- Device: {gpu_last.get('device')}",
            ]
        )
    else:
        lines.append("- GPU metrics are not available yet.")

    lines.extend(
        [
            "",
            "## Baseline",
            "",
            "- Current student-hybrid 100x: 48 wins / 21 losses / 31 draws.",
            "- Black side baseline: 19-21-10.",
            "- White side baseline: 29-0-21.",
            "",
            "## Compare Results",
            "",
        ]
    )

    if compare_summaries:
        for summary in compare_summaries:
            lines.extend(
                [
                    f"### `{summary['path']}`",
                    "",
                    f"- Games: {summary['rows']}",
                    f"- Overall: {summary['wins']} wins / {summary['losses']} losses / {summary['draws']} draws",
                    f"- Timeouts: {summary['timeouts']}",
                    f"- Black side: {summary['black']['wins']}-{summary['black']['losses']}-{summary['black']['draws']}",
                    f"- White side: {summary['white']['wins']}-{summary['white']['losses']}-{summary['white']['draws']}",
                    "",
                ]
            )
    else:
        lines.append("- Compare CSVs were not provided yet.")

    lines.extend(
        [
            "## Notes",
            "",
            "- This report is generated from retained CSV/log artifacts; inspect the raw files before citing final claims.",
            "- Full acceptance requires the planned 300-game 1000-ply evaluation plus the 600-ply check.",
            "",
        ]
    )

    out_path.write_text("\n".join(lines), encoding="utf-8")
    print(f"analysis written: {out_path}")


if __name__ == "__main__":
    main()
