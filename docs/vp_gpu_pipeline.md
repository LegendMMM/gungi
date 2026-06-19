# GPU-Assisted VP Pipeline

This pipeline keeps Gungi rules/search on CPU and uses PyTorch/CUDA only for
batch training the linear value + policy heads.

## Build

```bat
build.bat
```

Build outputs:

- `tests\test_vp_model.exe`
- `tools\train_vp_from_hybrid.exe`
- `tools\compare_model_minimax.exe`

## GPU Environment

```powershell
.\tools\setup_vp_gpu_env.ps1
```

The setup script creates `.venv-gpu`, installs `numpy` and CUDA PyTorch, then
prints the CUDA availability and GPU name.

## Sample Generation

Smoke:

```powershell
.\tools\train_vp_from_hybrid.exe 1000 train_vp_smoke.samples.bin train_vp_smoke.csv train_vp_smoke.log models\v_weights_hybrid_student_50000.bin 515151 1000 2 train_vp_smoke_candidates.csv 64
```

Full:

```powershell
.\tools\train_vp_from_hybrid.exe 100000 train_vp_hybrid_student_100000.samples.bin train_vp_hybrid_student_100000.csv train_vp_hybrid_student_100000.log models\v_weights_hybrid_student_50000.bin 515151 1000 2 train_vp_hybrid_student_100000_candidates.csv 64
```

Retained data:

- `*.samples.bin`: fixed-record GPU training dataset.
- `*.csv`: per-position summary.
- `*_candidate.csv`: sampled candidate move rows for reporting/debugging.
- `*.log`: generation config and progress.

## GPU Training

Smoke:

```powershell
.\.venv-gpu\Scripts\python.exe .\tools\train_vp_gpu.py --dataset train_vp_smoke.samples.bin --out-model models\vp_smoke.bin --metrics-csv train_vp_smoke_gpu.csv --log train_vp_smoke_gpu.log --epochs 1 --batch-size 256
```

Full:

```powershell
.\.venv-gpu\Scripts\python.exe .\tools\train_vp_gpu.py --dataset train_vp_hybrid_student_100000.samples.bin --out-model models\vp_hybrid_student_100000.bin --metrics-csv train_vp_hybrid_student_100000_gpu.csv --log train_vp_hybrid_student_100000_gpu.log --epochs 10 --batch-size 4096
```

## Compare

Smoke:

```powershell
.\tools\compare_model_minimax.exe models\vp_smoke.bin vp-policy 2 1000 24680 vp_smoke_policy_vs_minimax2.csv vp_smoke_policy_vs_minimax2.log 2
.\tools\compare_model_minimax.exe models\vp_smoke.bin vp-hybrid 2 1000 24680 vp_smoke_hybrid_vs_minimax2.csv vp_smoke_hybrid_vs_minimax2.log 2
```

Formal evaluation should use the A-E matrix from the implementation plan and
keep every CSV/log pair.

## Analysis Draft

```powershell
.\.venv-gpu\Scripts\python.exe .\tools\write_vp_analysis.py --compare-csv vp_smoke_policy_vs_minimax2.csv --compare-csv vp_smoke_hybrid_vs_minimax2.csv
```

Default output:

- `analysis_reports\vp_hybrid_student_100000_analysis.md`
