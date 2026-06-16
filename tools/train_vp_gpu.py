import argparse
import csv
import math
import os
import struct
import sys
from pathlib import Path

import numpy as np
import torch


DATASET_MAGIC = b"GVPDS1\0\0"
VP_MAGIC = b"GUNGIP4\0"
V3_MAGIC = b"GUNGIV3\0"


def parse_args():
    parser = argparse.ArgumentParser(description="Train a linear Gungi VP model with PyTorch/CUDA.")
    parser.add_argument("--dataset", default="train_vp_hybrid_student_100000.samples.bin")
    parser.add_argument("--init-model", default="models/v_weights_hybrid_student_50000.bin")
    parser.add_argument("--out-model", default="models/vp_hybrid_student_100000.bin")
    parser.add_argument("--value-v3-out", default="")
    parser.add_argument("--metrics-csv", default="train_vp_hybrid_student_100000_gpu.csv")
    parser.add_argument("--log", default="train_vp_hybrid_student_100000_gpu.log")
    parser.add_argument("--epochs", type=int, default=10)
    parser.add_argument("--batch-size", type=int, default=4096)
    parser.add_argument("--value-lr", type=float, default=0.0005)
    parser.add_argument("--policy-lr", type=float, default=0.002)
    parser.add_argument("--margin", type=float, default=0.05)
    parser.add_argument("--seed", type=int, default=515151)
    parser.add_argument("--allow-cpu", action="store_true")
    return parser.parse_args()


def read_header(path):
    with open(path, "rb") as handle:
        raw = handle.read(32)
    if len(raw) != 32:
        raise ValueError("dataset header is truncated")
    magic, version, samples, v_count, p_count, max_candidates, record_size = struct.unpack("<8s6i", raw)
    if magic != DATASET_MAGIC:
        raise ValueError(f"bad dataset magic: {magic!r}")
    if version != 1:
        raise ValueError(f"unsupported dataset version: {version}")
    if v_count <= 0 or p_count <= 0 or max_candidates <= 1:
        raise ValueError("dataset feature counts are invalid")
    expected_record_size = 6 * 4 + 4 + v_count * 4 + max_candidates * p_count * 4
    if record_size != expected_record_size:
        raise ValueError(f"record size mismatch: header={record_size} expected={expected_record_size}")
    return samples, v_count, p_count, max_candidates, record_size


def dataset_dtype(v_count, p_count, max_candidates):
    return np.dtype(
        [
            ("ply", "<i4"),
            ("player", "<i4"),
            ("legal_count", "<i4"),
            ("candidate_count", "<i4"),
            ("teacher_index", "<i4"),
            ("teacher_black_score", "<i4"),
            ("value_target", "<f4"),
            ("value_features", "<f4", (v_count,)),
            ("policy_features", "<f4", (max_candidates, p_count)),
        ]
    )


def open_dataset(path):
    samples, v_count, p_count, max_candidates, _record_size = read_header(path)
    dtype = dataset_dtype(v_count, p_count, max_candidates)
    data = np.memmap(path, dtype=dtype, mode="r", offset=32, shape=(samples,))
    return data, samples, v_count, p_count, max_candidates


def load_initial_model(path, v_count, p_count):
    value_weights = np.zeros((v_count,), dtype=np.float32)
    policy_weights = np.zeros((p_count,), dtype=np.float32)

    with open(path, "rb") as handle:
        magic = handle.read(8)
        if magic == V3_MAGIC:
            (feature_count,) = struct.unpack("<i", handle.read(4))
            if feature_count != v_count:
                raise ValueError(f"V3 feature count mismatch: {feature_count} != {v_count}")
            value_weights[:] = np.frombuffer(handle.read(4 * v_count), dtype="<f4")
        elif magic == VP_MAGIC:
            value_count, policy_count = struct.unpack("<ii", handle.read(8))
            if value_count != v_count or policy_count != p_count:
                raise ValueError("VP feature count mismatch")
            value_weights[:] = np.frombuffer(handle.read(4 * v_count), dtype="<f4")
            policy_weights[:] = np.frombuffer(handle.read(4 * p_count), dtype="<f4")
        else:
            raise ValueError(f"unsupported model magic: {magic!r}")

    return value_weights, policy_weights


def save_vp_model(path, value_weights, policy_weights):
    Path(path).parent.mkdir(parents=True, exist_ok=True)
    with open(path, "wb") as handle:
        handle.write(struct.pack("<8sii", VP_MAGIC, len(value_weights), len(policy_weights)))
        handle.write(np.asarray(value_weights, dtype="<f4").tobytes())
        handle.write(np.asarray(policy_weights, dtype="<f4").tobytes())


def save_v3_model(path, value_weights):
    Path(path).parent.mkdir(parents=True, exist_ok=True)
    with open(path, "wb") as handle:
        handle.write(struct.pack("<8si", V3_MAGIC, len(value_weights)))
        handle.write(np.asarray(value_weights, dtype="<f4").tobytes())


def select_device(allow_cpu):
    if torch.cuda.is_available():
        return torch.device("cuda")
    if allow_cpu:
        return torch.device("cpu")
    raise RuntimeError("CUDA is not available. Install a CUDA PyTorch wheel or pass --allow-cpu for a slow smoke run.")


def batch_to_tensors(records, device):
    value_features = torch.as_tensor(np.asarray(records["value_features"], dtype=np.float32), device=device)
    value_target = torch.as_tensor(np.asarray(records["value_target"], dtype=np.float32), device=device)
    policy_features = torch.as_tensor(np.asarray(records["policy_features"], dtype=np.float32), device=device)
    candidate_count = torch.as_tensor(np.asarray(records["candidate_count"], dtype=np.int64), device=device)
    teacher_index = torch.as_tensor(np.asarray(records["teacher_index"], dtype=np.int64), device=device)
    return value_features, value_target, policy_features, candidate_count, teacher_index


def main():
    args = parse_args()
    torch.manual_seed(args.seed)
    np.random.seed(args.seed)

    device = select_device(args.allow_cpu)
    data, samples, v_count, p_count, max_candidates = open_dataset(args.dataset)
    initial_value, initial_policy = load_initial_model(args.init_model, v_count, p_count)

    value_w = torch.nn.Parameter(torch.as_tensor(initial_value, dtype=torch.float32, device=device))
    policy_w = torch.nn.Parameter(torch.as_tensor(initial_policy, dtype=torch.float32, device=device))
    optimizer = torch.optim.SGD(
        [
            {"params": [value_w], "lr": args.value_lr},
            {"params": [policy_w], "lr": args.policy_lr},
        ]
    )

    Path(args.metrics_csv).parent.mkdir(parents=True, exist_ok=True) if Path(args.metrics_csv).parent != Path(".") else None
    Path(args.log).parent.mkdir(parents=True, exist_ok=True) if Path(args.log).parent != Path(".") else None
    with open(args.metrics_csv, "w", newline="") as metrics_handle, open(args.log, "w") as log_handle:
        writer = csv.writer(metrics_handle)
        writer.writerow(["epoch", "samples", "batch_size", "value_loss", "policy_loss", "total_loss", "device"])
        log_handle.write(
            f"dataset={args.dataset} samples={samples} value_features={v_count} policy_features={p_count} "
            f"max_candidates={max_candidates} device={device} epochs={args.epochs} batch_size={args.batch_size}\n"
        )
        if device.type == "cuda":
            log_handle.write(f"cuda_device={torch.cuda.get_device_name(0)} torch={torch.__version__}\n")

        indices = np.arange(samples)
        for epoch in range(1, args.epochs + 1):
            np.random.shuffle(indices)
            value_loss_total = 0.0
            policy_loss_total = 0.0
            total_loss_total = 0.0
            seen = 0

            for start in range(0, samples, args.batch_size):
                batch_indices = indices[start : start + args.batch_size]
                records = data[batch_indices]
                batch_size = len(records)
                vf, vt, pf, candidate_count, teacher_index = batch_to_tensors(records, device)

                optimizer.zero_grad(set_to_none=True)
                value_pred = vf.matmul(value_w)
                value_loss = torch.mean((value_pred - vt) ** 2)

                policy_scores = torch.matmul(pf, policy_w)
                candidate_positions = torch.arange(max_candidates, device=device).unsqueeze(0)
                valid_mask = candidate_positions < candidate_count.unsqueeze(1)
                policy_scores = policy_scores.masked_fill(~valid_mask, -1.0e30)
                teacher_scores = policy_scores.gather(1, teacher_index.unsqueeze(1)).squeeze(1)
                negative_scores = policy_scores.clone()
                negative_scores.scatter_(1, teacher_index.unsqueeze(1), -1.0e30)
                hard_negative_scores = torch.max(negative_scores, dim=1).values
                valid_pair = candidate_count > 1
                if torch.any(valid_pair):
                    policy_loss = torch.relu(args.margin - teacher_scores[valid_pair] + hard_negative_scores[valid_pair]).mean()
                else:
                    policy_loss = value_loss.new_tensor(0.0)

                total_loss = value_loss + policy_loss
                total_loss.backward()
                optimizer.step()

                if not torch.isfinite(value_w).all() or not torch.isfinite(policy_w).all():
                    raise RuntimeError("NaN or Inf detected in VP weights")

                value_loss_total += float(value_loss.detach().cpu()) * batch_size
                policy_loss_total += float(policy_loss.detach().cpu()) * batch_size
                total_loss_total += float(total_loss.detach().cpu()) * batch_size
                seen += batch_size

            value_loss_avg = value_loss_total / max(1, seen)
            policy_loss_avg = policy_loss_total / max(1, seen)
            total_loss_avg = total_loss_total / max(1, seen)
            writer.writerow([epoch, seen, args.batch_size, value_loss_avg, policy_loss_avg, total_loss_avg, str(device)])
            metrics_handle.flush()
            log_handle.write(
                f"epoch={epoch} samples={seen} value_loss={value_loss_avg:.8f} "
                f"policy_loss={policy_loss_avg:.8f} total_loss={total_loss_avg:.8f}\n"
            )
            log_handle.flush()

            if not math.isfinite(total_loss_avg):
                raise RuntimeError("non-finite epoch loss")

    value_np = value_w.detach().cpu().numpy().astype(np.float32)
    policy_np = policy_w.detach().cpu().numpy().astype(np.float32)
    save_vp_model(args.out_model, value_np, policy_np)
    if args.value_v3_out:
        save_v3_model(args.value_v3_out, value_np)

    print(f"VP model saved: {args.out_model}")
    print(f"metrics: {args.metrics_csv}")
    print(f"log: {args.log}")


if __name__ == "__main__":
    try:
        main()
    except Exception as exc:
        print(f"error: {exc}", file=sys.stderr)
        sys.exit(1)
