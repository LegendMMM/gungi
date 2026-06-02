# Hybrid student 50000 analysis

## Run record

- Teacher model: `models/v_weights_100000.bin`
- Student base model: `models/v_weights_100000.bin`
- Output model: `models/v_weights_hybrid_student_50000.bin`
- Trainer: `tools/train_q_from_hybrid.c`
- Samples: 50000
- Teacher search: hybrid q_search depth 2
- Teacher config: `model_scale=30000`, draw/repetition/late penalties disabled
- Max ply per generated game: 1000
- Seed: 424242
- Learning rate: 0.002
- Training log: `train_hybrid_student_50000.log`
- Training CSV: `train_hybrid_student_50000.csv`
- Evaluation summary: `hybrid_student_50000_summary.csv`

The trainer uses two separate models:

- immutable teacher: `v_weights_100000.bin`
- mutable student: starts from `v_weights_100000.bin`, then receives value updates

## Completeness and health

| Check | Value |
| --- | --- |
| Trainer exit | success |
| Stderr | 0 bytes |
| CSV checkpoints | 10 |
| Final sample | 50000 / 50000 |
| Model size | 188 bytes |
| Final checksum | 3.027677 |
| SHA256 | `1829605D35DD508C25335EC08EBCF089B469834A524BEE23623D1C889313EDF0` |
| Total agreement | 1.468% |
| Total resets | 87 |

The run is technically healthy: the log is complete, the model reloads, and no
NaN/Inf failure was reported. The checksum is much larger than the base
`v_weights_100000.bin` checksum of 0.771393, but it did not explode.

## Training checkpoints

| Sample | Target avg | Target min | Target max | Abs error avg | Agreement | Checksum |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 5000 | -0.017055 | -0.577233 | 1.000000 | 0.087637 | 1.30% | 1.070760 |
| 10000 | -0.016580 | -0.474633 | 1.000000 | 0.079507 | 1.28% | 1.293741 |
| 15000 | -0.015586 | -0.601100 | 1.000000 | 0.103299 | 1.50% | 1.878719 |
| 20000 | -0.016130 | -0.503067 | 1.000000 | 0.062873 | 1.24% | 2.140531 |
| 25000 | -0.016762 | -0.247533 | 0.187600 | 0.034808 | 1.26% | 2.233519 |
| 30000 | -0.015897 | -0.446133 | 1.000000 | 0.057127 | 1.70% | 2.462585 |
| 35000 | -0.016552 | -0.572600 | 1.000000 | 0.071386 | 2.18% | 2.680149 |
| 40000 | -0.016477 | -0.417367 | 1.000000 | 0.057650 | 1.12% | 2.809178 |
| 45000 | -0.015061 | -0.578433 | 1.000000 | 0.058424 | 1.96% | 2.982522 |
| 50000 | -0.016847 | -0.500700 | 1.000000 | 0.040034 | 1.14% | 3.027677 |

The student did not learn to match the teacher's selected move often. That is
expected because this is value distillation, not policy imitation, but the low
agreement rate is still a warning sign.

## Evaluation results

| Run | Mode | Games | Student wins | Original wins | Draws/timeouts | Avg ply | Result |
| --- | --- | ---: | ---: | ---: | ---: | ---: | --- |
| `hybrid_student_modelonly_depth2_1000ply_10x.csv` | model-only | 20 | 5 | 10 | 5 | 505.95 | record only |
| `hybrid_student_qsearch_depth2_1000ply_20x.csv` | student-hybrid | 40 | 11 | 11 | 18 | 473.72 | fail |
| `hybrid_student_qsearch_depth2_600ply_10x.csv` | student-hybrid | 20 | 9 | 7 | 4 | 233.90 | fail |

By side:

| Run | Student black | Student white |
| --- | --- | --- |
| model-only 1000 | 4-2-4 | 1-8-1 |
| student-hybrid 1000 | 3-9-8 | 8-2-10 |
| student-hybrid 600 | 2-7-1 | 7-0-3 |

The student is highly side-skewed. At 1000 ply it is weak as black but strong as
white. At 600 ply the same pattern is even sharper.

## Acceptance check

Baseline from stage 1:

```text
1000 ply baseline: 11 wins / 8 losses / 21 draws over 40 games
600 ply baseline: 3 wins / 8 losses / 9 draws over 20 games
```

Student result:

```text
1000 ply: 11 wins / 11 losses / 18 draws
600 ply: 9 wins / 7 losses / 4 draws
```

Acceptance:

| Criterion | Required | Actual | Pass |
| --- | --- | --- | --- |
| Health | no illegal moves, no bad weights, checksum >= 0.30 | checksum 3.027677, stderr 0 | yes |
| 1000-ply wins | >= 12 | 11 | no |
| 1000-ply losses | <= 8 | 11 | no |
| 1000-ply draws | <= 20 | 18 | yes |
| 600-ply losses | <= 5 | 7 | no |

The student does not pass promotion criteria.

## Interpretation

The student reduced draws, especially at 600 ply, but it converted too many of
those games into losses. This is not a safe upgrade over the stage-1 hybrid.

The likely issue is that pure value regression to the hybrid score is
over-amplifying positional preferences. The final checksum rose to 3.027677, and
the evaluation became strongly side-dependent. The student learned a sharper
value function, but not a better one.

## Recommendation

Do not promote `models/v_weights_hybrid_student_50000.bin`.

Keep it as an experiment artifact. The next student attempt should reduce
update aggressiveness and preserve the teacher's tactical behavior more
directly:

- lower learning rate from 0.002 to 0.0005 or 0.001
- reduce target clamp impact by scaling to `black_score / 60000`
- add a small policy/ranking term only for top teacher move vs student move
- evaluate at 10000, 25000, and 50000 samples instead of only final 50000
- select by acceptance metrics, not by training error
