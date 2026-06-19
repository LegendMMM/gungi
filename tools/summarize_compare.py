#!/usr/bin/env python3
import argparse
import csv
import statistics
from pathlib import Path


def summarize_csv(path: Path):
    wins = losses = draws = timeouts = rows = ply_total = 0
    black = {"wins": 0, "losses": 0, "draws": 0, "timeouts": 0}
    white = {"wins": 0, "losses": 0, "draws": 0, "timeouts": 0}
    status_counts = {}
    plies = []

    with path.open(newline="") as handle:
        reader = csv.DictReader(handle)
        for row in reader:
            rows += 1
            side = row.get("model_side", "")
            timed_out = row.get("timed_out", "0") == "1"
            model_win = row.get("model_win", "0") == "1"
            model_loss = row.get("model_loss", "0") == "1"
            status = row.get("status", "")
            ply = int(row.get("ply", "0") or 0)
            ply_total += ply
            plies.append(ply)
            status_counts[status] = status_counts.get(status, 0) + 1

            if timed_out or (not model_win and not model_loss):
                outcome = "draws"
                draws += 1
            elif model_win:
                outcome = "wins"
                wins += 1
            else:
                outcome = "losses"
                losses += 1

            if side == "B":
                black[outcome] += 1
            elif side == "W":
                white[outcome] += 1

            if timed_out:
                timeouts += 1
                if side == "B":
                    black["timeouts"] += 1
                elif side == "W":
                    white["timeouts"] += 1

    avg_ply = (ply_total / rows) if rows else 0.0
    median_ply = statistics.median(plies) if plies else 0.0
    sorted_plies = sorted(plies)
    p90_ply = sorted_plies[int(0.9 * (len(sorted_plies) - 1))] if sorted_plies else 0
    return {
        "path": str(path),
        "games": rows,
        "wins": wins,
        "losses": losses,
        "draws": draws,
        "timeouts": timeouts,
        "non_timeout_draws": draws - timeouts,
        "black": black,
        "white": white,
        "avg_ply": avg_ply,
        "median_ply": median_ply,
        "p90_ply": p90_ply,
        "status_counts": status_counts,
    }


def rate(value: int, games: int):
    if games <= 0:
        return "0.0%"
    return f"{(100.0 * value / games):.1f}%"


def format_summary(summary):
    games = summary["games"]
    black = summary["black"]
    white = summary["white"]
    return [
        f"file={summary['path']}",
        f"games={games}",
        (
            f"overall={summary['wins']}W/{summary['losses']}L/{summary['draws']}D "
            f"({rate(summary['wins'], games)} win, {rate(summary['losses'], games)} loss, "
            f"{rate(summary['draws'], games)} draw)"
        ),
        f"timeouts={summary['timeouts']} ({rate(summary['timeouts'], games)})",
        f"non_timeout_draws={summary['non_timeout_draws']}",
        f"black={black['wins']}-{black['losses']}-{black['draws']} timeouts={black['timeouts']}",
        f"white={white['wins']}-{white['losses']}-{white['draws']} timeouts={white['timeouts']}",
        f"avg_ply={summary['avg_ply']:.3f} median_ply={summary['median_ply']:.1f} p90_ply={summary['p90_ply']}",
        "status_counts=" + ",".join(f"{key}:{value}" for key, value in sorted(summary["status_counts"].items())),
    ]


def main():
    parser = argparse.ArgumentParser(description="Summarize compare_model_minimax CSV outputs.")
    parser.add_argument("csv", nargs="+", type=Path)
    parser.add_argument("--markdown", action="store_true", help="Print a markdown table.")
    args = parser.parse_args()

    summaries = [summarize_csv(path) for path in args.csv]
    if args.markdown:
        print("| file | games | W-L-D | timeout | non-timeout D | black | white | avg | median | p90 | status counts |")
        print("|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---|")
        for summary in summaries:
            games = summary["games"]
            black = summary["black"]
            white = summary["white"]
            status_counts = ",".join(f"{key}:{value}" for key, value in sorted(summary["status_counts"].items()))
            print(
                f"| `{summary['path']}` | {games} | "
                f"{summary['wins']}-{summary['losses']}-{summary['draws']} | "
                f"{summary['timeouts']} ({rate(summary['timeouts'], games)}) | "
                f"{summary['non_timeout_draws']} | "
                f"{black['wins']}-{black['losses']}-{black['draws']} T{black['timeouts']} | "
                f"{white['wins']}-{white['losses']}-{white['draws']} T{white['timeouts']} | "
                f"{summary['avg_ply']:.1f} | "
                f"{summary['median_ply']:.1f} | "
                f"{summary['p90_ply']} | "
                f"{status_counts} |"
            )
    else:
        for summary in summaries:
            print("\n".join(format_summary(summary)))
            print()


if __name__ == "__main__":
    main()
