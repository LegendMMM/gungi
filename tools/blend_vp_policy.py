#!/usr/bin/env python3
import argparse
import math
import struct
from pathlib import Path

import numpy as np


VP_MAGIC = b"GUNGIP4\0"
V3_MAGIC = b"GUNGIV3\0"
VALUE_FEATURE_COUNT = 44
POLICY_FEATURE_COUNT = 47


def load_model(path: Path):
    with path.open("rb") as handle:
        magic = handle.read(8)
        if magic == V3_MAGIC:
            (value_count,) = struct.unpack("<i", handle.read(4))
            if value_count != VALUE_FEATURE_COUNT:
                raise ValueError(f"{path}: unexpected V3 value feature count {value_count}")
            value = np.frombuffer(handle.read(4 * value_count), dtype="<f4").astype(np.float32)
            policy = np.zeros((POLICY_FEATURE_COUNT,), dtype=np.float32)
            return value, policy

        if magic == VP_MAGIC:
            value_count, policy_count = struct.unpack("<ii", handle.read(8))
            if value_count != VALUE_FEATURE_COUNT or policy_count != POLICY_FEATURE_COUNT:
                raise ValueError(
                    f"{path}: unexpected VP feature counts value={value_count} policy={policy_count}"
                )
            value = np.frombuffer(handle.read(4 * value_count), dtype="<f4").astype(np.float32)
            policy = np.frombuffer(handle.read(4 * policy_count), dtype="<f4").astype(np.float32)
            return value, policy

    raise ValueError(f"{path}: unsupported model magic {magic!r}")


def save_vp_model(path: Path, value: np.ndarray, policy: np.ndarray):
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("wb") as handle:
        handle.write(struct.pack("<8sii", VP_MAGIC, VALUE_FEATURE_COUNT, POLICY_FEATURE_COUNT))
        handle.write(np.asarray(value, dtype="<f4").tobytes())
        handle.write(np.asarray(policy, dtype="<f4").tobytes())


def finite_or_fail(name: str, weights: np.ndarray):
    if not np.isfinite(weights).all():
        raise ValueError(f"{name} contains NaN/Inf")


def summarize(label: str, weights: np.ndarray):
    l2 = float(np.linalg.norm(weights))
    max_abs = float(np.max(np.abs(weights))) if weights.size else 0.0
    mean_abs = float(np.mean(np.abs(weights))) if weights.size else 0.0
    return f"{label}: l2={l2:.6f} max_abs={max_abs:.6f} mean_abs={mean_abs:.6f}"


def main():
    parser = argparse.ArgumentParser(
        description="Blend the policy head of two VP models while preserving a chosen value head."
    )
    parser.add_argument("--base", required=True, type=Path, help="Stable base VP/V3 model, normally V1.")
    parser.add_argument("--aux", required=True, type=Path, help="Auxiliary VP/V3 model, normally V2A.")
    parser.add_argument("--out", required=True, type=Path, help="Output GUNGIP4 model path.")
    parser.add_argument(
        "--alpha",
        required=True,
        type=float,
        help="Auxiliary policy ratio. 0.0 keeps base policy, 1.0 uses auxiliary policy.",
    )
    parser.add_argument(
        "--value-source",
        choices=("base", "aux", "blend"),
        default="base",
        help="Where value weights come from. Default keeps the stable base value head.",
    )
    parser.add_argument(
        "--policy-scale",
        type=float,
        default=1.0,
        help="Optional multiplier applied after policy blending.",
    )
    args = parser.parse_args()

    if not math.isfinite(args.alpha) or args.alpha < 0.0 or args.alpha > 1.0:
        raise ValueError("--alpha must be finite and between 0 and 1")
    if not math.isfinite(args.policy_scale):
        raise ValueError("--policy-scale must be finite")

    base_value, base_policy = load_model(args.base)
    aux_value, aux_policy = load_model(args.aux)

    if args.value_source == "base":
        value = base_value.copy()
    elif args.value_source == "aux":
        value = aux_value.copy()
    else:
        value = ((1.0 - args.alpha) * base_value + args.alpha * aux_value).astype(np.float32)

    policy = ((1.0 - args.alpha) * base_policy + args.alpha * aux_policy).astype(np.float32)
    policy = (policy * args.policy_scale).astype(np.float32)

    finite_or_fail("value", value)
    finite_or_fail("policy", policy)
    save_vp_model(args.out, value, policy)

    print(f"wrote {args.out}")
    print(f"alpha={args.alpha:.6f} value_source={args.value_source} policy_scale={args.policy_scale:.6f}")
    print(summarize("base_policy", base_policy))
    print(summarize("aux_policy", aux_policy))
    print(summarize("out_policy", policy))


if __name__ == "__main__":
    main()
