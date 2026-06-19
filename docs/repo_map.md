# Gungi Repo Map

This file is the working index for the current Gungi codebase. It separates
source code, build/test entry points, AI training tools, and experiment
artifacts so future changes do not have to rediscover the layout.

## Primary Entry Points

- `README.md`: player setup, manual build instructions, and basic gameplay.
- `build.bat`: Windows build for the game, tests, and C training/evaluation tools.
- `docs/rules_notes.md`: rules implementation notes.
- `docs/vp_gpu_pipeline.md`: CPU teacher generation plus PyTorch/CUDA VP training flow.

## Source Layout

| Path | Role |
|---|---|
| `src/gungi_rules.c`, `src/gungi_rules.h` | Rules engine and legal move generation. |
| `src/main.c` | raylib UI and player/AI controls. |
| `src/ai_core.c`, `src/ai_core.h` | Shared AI helpers. |
| `src/q_model.c`, `src/q_model.h` | Linear value/Q model loading and scoring. |
| `src/q_search.c`, `src/q_search.h` | Greedy, minimax, and hybrid search paths. |
| `src/vp_model.c`, `src/vp_model.h` | `GUNGIP4` value+policy model support with `GUNGIV3` compatibility. |

## Tests

- `tests/test_rules.c`: rules smoke/regression tests.
- `tests/test_vp_model.c`: VP model load/save and feature compatibility tests.

Fast validation after source changes:

```bat
build.bat
tests\test_rules.exe
tests\test_vp_model.exe
```

## Tools

| Path | Role |
|---|---|
| `tools/train_q.c` | Legacy value/Q model training. |
| `tools/eval_q.c` | Value/Q model evaluation. |
| `tools/train_vp_from_hybrid.c` | Generate VP teacher samples from the hybrid search path. |
| `tools/train_vp_gpu.py` | Train VP value+policy weights with PyTorch/CUDA. |
| `tools/compare_model_minimax.c` | Compare trained models against minimax2. |
| `tools/generate_resolution_dataset.c` | Generate resolution/anti-draw datasets. |
| `tools/train_resolution_policy.py` | Train resolution/anti-draw policy variants. |
| `tools/blend_vp_policy.py` | Blend VP policy weights for anti-draw experiments. |
| `tools/summarize_compare.py` | Summarize compare CSV outputs. |
| `tools/write_vp_analysis.py` | Generate VP analysis markdown reports. |
| `tools/setup_vp_gpu_env.ps1` | Create `.venv-gpu` and install CUDA PyTorch dependencies. |

## Model And Artifact Areas

- `models/`: checked-in model binaries and config files used by the current experiments.
- `analysis_reports/`: human-readable experiment conclusions and acceptance decisions.
- `analysis/`: archived value-model experiment bundles.
- Root `*.csv` and `*.log`: checked-in formal evaluation outputs that back the reports.

The root-level formal CSV/log files are intentionally kept because the analysis
reports cite them. Do not move them without updating the related report and tool
commands at the same time.

## Regenerable Local Outputs

These files are local build or training outputs and are ignored by `.gitignore`:

- `gungi.exe`
- `tests\*.exe`
- `tools\*.exe`
- `.venv-gpu\`
- `.deps\`
- `build\`
- `out\`
- `*.samples.bin`

Large `*.samples.bin` datasets are expensive enough to keep around locally while
active, but they should stay out of git. If a dataset becomes report-critical,
record its exact generation command and checksum in `analysis_reports/` instead
of committing the binary.

## AI Workflow Boundaries

- The old value path remains available through `GUNGIV3`/Q model loading.
- The VP path is parallel: `GUNGIP4` adds policy weights and can load old value
  models by preserving value weights and zeroing policy weights.
- Policy-only play is an evaluation mode. The stronger intended runtime path is
  VP-assisted hybrid search, where policy ranks moves and alpha-beta still makes
  the final choice.
- Move identity should go through the existing helpers that account for captain
  betrayal variants; do not hand-roll move equality in new training/evaluation
  code.

## Current Experiment Reports

- `analysis_reports/vp_hybrid_student_100000_analysis.md`: first formal VP
  hybrid student training/evaluation result.
- `analysis_reports/vp_resolution_v2a_10000_analysis.md`: resolution policy
  experiment result.
- `analysis_reports/anti_draw_side_policy_experiment.md`: side-aware anti-draw
  exploration notes.
- `analysis_reports/anti_draw_side_policy_formal_300.md`: 300-game formal
  side-aware anti-draw result.
- `analysis_reports/gungi_ai_evolution_reflection_report.md`: broader AI
  iteration reflection.
