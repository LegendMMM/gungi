import argparse
import csv
import struct
import sys
from pathlib import Path

import numpy as np
import torch


VP_MAGIC = b"GUNGIP4\0"
V3_MAGIC = b"GUNGIV3\0"


def parse_args():
    parser = argparse.ArgumentParser(description="Train the VP policy head on resolution targets.")
    parser.add_argument("--dataset", default="resolution_v2a_10000.csv")
    parser.add_argument("--init-model", default="models/vp_hybrid_student_100000.bin")
    parser.add_argument("--out-model", default="models/vp_resolution_v2a_10000.bin")
    parser.add_argument("--metrics-csv", default="resolution_v2a_10000_gpu.csv")
    parser.add_argument("--log", default="resolution_v2a_10000_gpu.log")
    parser.add_argument("--epochs", type=int, default=8)
    parser.add_argument("--batch-size", type=int, default=4096)
    parser.add_argument("--lr", type=float, default=0.01)
    parser.add_argument("--margin", type=float, default=0.05)
    parser.add_argument("--allow-cpu", action="store_true")
    parser.add_argument("--seed", type=int, default=626262)
    return parser.parse_args()


def load_model(path):
    with open(path, "rb") as handle:
        magic = handle.read(8)
        if magic == VP_MAGIC:
            v_count, p_count = struct.unpack("<ii", handle.read(8))
            value = np.frombuffer(handle.read(4 * v_count), dtype="<f4").copy()
            policy = np.frombuffer(handle.read(4 * p_count), dtype="<f4").copy()
            return value.astype(np.float32), policy.astype(np.float32)
        if magic == V3_MAGIC:
            (v_count,) = struct.unpack("<i", handle.read(4))
            value = np.frombuffer(handle.read(4 * v_count), dtype="<f4").copy()
            return value.astype(np.float32), np.zeros((47,), dtype=np.float32)
    raise ValueError(f"unsupported model format: {path}")


def save_model(path, value, policy):
    Path(path).parent.mkdir(parents=True, exist_ok=True)
    with open(path, "wb") as handle:
        handle.write(struct.pack("<8sii", VP_MAGIC, len(value), len(policy)))
        handle.write(np.asarray(value, dtype="<f4").tobytes())
        handle.write(np.asarray(policy, dtype="<f4").tobytes())


def load_dataset(path):
    features = []
    targets = []
    position_ids = []
    with open(path, newline="") as handle:
        reader = csv.DictReader(handle)
        feature_names = [name for name in reader.fieldnames if name and name.startswith("p") and name[1:].isdigit()]
        feature_names.sort(key=lambda name: int(name[1:]))
        for row in reader:
            features.append([float(row[name]) for name in feature_names])
            targets.append(float(row["resolution_target"]))
            position_ids.append(int(row["position_id"]))
    if not features:
        raise ValueError("empty resolution dataset")
    return (
        np.asarray(features, dtype=np.float32),
        np.asarray(targets, dtype=np.float32),
        np.asarray(position_ids, dtype=np.int64),
    )


def build_pairs(features, targets, position_ids):
    best = []
    worst = []
    order = np.argsort(position_ids)
    sorted_ids = position_ids[order]
    start = 0
    while start < len(order):
        end = start + 1
        while end < len(order) and sorted_ids[end] == sorted_ids[start]:
            end += 1
        group_indices = order[start:end]
        group_targets = targets[group_indices]
        if len(group_indices) >= 2:
            hi = group_indices[int(np.argmax(group_targets))]
            lo = group_indices[int(np.argmin(group_targets))]
            if targets[hi] > targets[lo] + 0.02:
                best.append(features[hi])
                worst.append(features[lo])
        start = end
    if not best:
        raise ValueError("no usable pairwise resolution examples")
    return np.asarray(best, dtype=np.float32), np.asarray(worst, dtype=np.float32)


def select_device(allow_cpu):
    if torch.cuda.is_available():
        return torch.device("cuda")
    if allow_cpu:
        return torch.device("cpu")
    raise RuntimeError("CUDA is not available; pass --allow-cpu only for smoke tests.")


def main():
    args = parse_args()
    np.random.seed(args.seed)
    torch.manual_seed(args.seed)

    device = select_device(args.allow_cpu)
    value, initial_policy = load_model(args.init_model)
    features, targets, position_ids = load_dataset(args.dataset)
    best, worst = build_pairs(features, targets, position_ids)

    policy = torch.nn.Parameter(torch.as_tensor(initial_policy, dtype=torch.float32, device=device))
    optimizer = torch.optim.Adam([policy], lr=args.lr)
    x = torch.as_tensor(features, dtype=torch.float32, device=device)
    y = torch.as_tensor(targets, dtype=torch.float32, device=device)
    best_x = torch.as_tensor(best, dtype=torch.float32, device=device)
    worst_x = torch.as_tensor(worst, dtype=torch.float32, device=device)

    indices = np.arange(len(features))
    pair_indices = np.arange(len(best))
    with open(args.metrics_csv, "w", newline="") as metrics, open(args.log, "w") as log:
        writer = csv.writer(metrics)
        writer.writerow(["epoch", "rows", "pairs", "regression_loss", "pairwise_loss", "total_loss", "device"])
        log.write(
            f"dataset={args.dataset} rows={len(features)} pairs={len(best)} device={device} "
            f"epochs={args.epochs} batch_size={args.batch_size} lr={args.lr}\n"
        )
        if device.type == "cuda":
            log.write(f"cuda_device={torch.cuda.get_device_name(0)} torch={torch.__version__}\n")

        for epoch in range(1, args.epochs + 1):
            np.random.shuffle(indices)
            np.random.shuffle(pair_indices)
            reg_total = 0.0
            pair_total = 0.0
            total_total = 0.0
            seen = 0
            pair_seen = 0
            steps = max((len(indices) + args.batch_size - 1) // args.batch_size,
                        (len(pair_indices) + args.batch_size - 1) // args.batch_size)
            for step in range(steps):
                batch_idx = indices[step * args.batch_size:(step + 1) * args.batch_size]
                pair_idx = pair_indices[step * args.batch_size:(step + 1) * args.batch_size]
                if len(batch_idx) == 0 and len(pair_idx) == 0:
                    continue
                optimizer.zero_grad(set_to_none=True)
                losses = []
                reg_loss = torch.tensor(0.0, device=device)
                pair_loss = torch.tensor(0.0, device=device)
                if len(batch_idx) > 0:
                    pred = x[batch_idx].matmul(policy)
                    reg_loss = torch.mean((pred - y[batch_idx]) ** 2)
                    losses.append(reg_loss)
                if len(pair_idx) > 0:
                    good = best_x[pair_idx].matmul(policy)
                    bad = worst_x[pair_idx].matmul(policy)
                    pair_loss = torch.relu(args.margin - good + bad).mean()
                    losses.append(pair_loss)
                loss = sum(losses)
                loss.backward()
                optimizer.step()
                if not torch.isfinite(policy).all():
                    raise RuntimeError("non-finite policy weights")
                weight = max(len(batch_idx), 1)
                reg_total += float(reg_loss.detach().cpu()) * weight
                pair_total += float(pair_loss.detach().cpu()) * max(len(pair_idx), 1)
                total_total += float(loss.detach().cpu()) * weight
                seen += len(batch_idx)
                pair_seen += len(pair_idx)
            reg_avg = reg_total / max(1, seen)
            pair_avg = pair_total / max(1, pair_seen)
            total_avg = total_total / max(1, seen)
            writer.writerow([epoch, len(features), len(best), reg_avg, pair_avg, total_avg, str(device)])
            metrics.flush()
            log.write(
                f"epoch={epoch} rows={len(features)} pairs={len(best)} "
                f"regression_loss={reg_avg:.8f} pairwise_loss={pair_avg:.8f} total_loss={total_avg:.8f}\n"
            )
            log.flush()

    save_model(args.out_model, value, policy.detach().cpu().numpy().astype(np.float32))
    print(f"resolution VP model saved: {args.out_model}")
    print(f"metrics: {args.metrics_csv}")


if __name__ == "__main__":
    try:
        main()
    except Exception as exc:
        print(f"error: {exc}", file=sys.stderr)
        sys.exit(1)
