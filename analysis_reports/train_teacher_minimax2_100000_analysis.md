# Minimax depth-2 teacher training, 100000 samples

## Run record

- Base model: `models/v_weights_100000.bin`
- Output model: `models/v_weights_teacher_minimax2_100000.bin`
- Teacher: current minimax AI, depth 2
- Samples: 100000
- Max ply per teacher game: 600
- Training log: `train_teacher_minimax2_100000_output.txt`
- Training CSV: `train_teacher_minimax2_100000.csv`
- Stderr: `train_teacher_minimax2_100000.err` (0 bytes)
- TD learning rate: 0.001
- Preference learning rate: 0.003
- Preference shift: 0.20

## Method

For each sampled state, the trainer compared the current Q model's preferred
move with the minimax depth-2 teacher move. When they differed, it updated the
linear value model to prefer the teacher after-state. The actual game was then
advanced using the teacher move.

This is a teacher/imitation run, not a self-play run and not an opponent
fine-tune. Its main signal is Q/minimax agreement, followed by fixed-seed games
against minimax depth 2.

## Completeness checks

- Final sample line reached: 100000 / 100000
- Final saved model line present: yes
- Weight snapshots present in the output log: yes, every 5000 samples and at save
- CSV checkpoints present: 20
- Stderr size: 0 bytes

## Training result

| Metric | Value |
| --- | ---: |
| Agreements | 4893 |
| Disagreements | 95107 |
| Agreement rate | 4.89% |
| Teacher black wins during sampled games | 59 |
| Teacher white wins during sampled games | 30 |
| Teacher draws during sampled games | 140 |
| Game resets | 229 |
| Average teacher margin | -0.000131 |
| Minimum teacher margin | -0.481234 |
| Maximum teacher margin | 0.000000 |
| Final checksum | 0.146217 |

Agreement stayed very low through the run. The final model still selected a
different move from minimax depth 2 about 95% of the time.

## Final weights

Final saved weights from `train_teacher_minimax2_100000_output.txt`:

```text
checksum 0.146217
w0=-0.005419 w1=0.000000 w2=-0.000111 w3=-0.000814 w4=-0.000028 w5=-0.000066 w6=-0.000064 w7=-0.000025
w8=0.000022 w9=-0.004677 w10=-0.000054 w11=0.018348 w12=-0.000077 w13=-0.000028 w14=-0.000058 w15=0.109911
w16=-0.000015 w17=-0.000008 w18=-0.000051 w19=0.000008 w20=0.000031 w21=-0.000039 w22=0.000135 w23=0.000001
w24=0.000006 w25=-0.000019 w26=-0.000020 w27=-0.000029 w28=-0.000018 w29=0.000000 w30=0.000000 w31=-0.000732
w32=0.004867 w33=-0.000134 w34=0.000000 w35=-0.000021 w36=-0.000051 w37=0.000000 w38=-0.000130 w39=0.000000
w40=-0.000147 w41=-0.000050 w42=0.000000 w43=0.000000
```

## Model file check

| Check | Value |
| --- | --- |
| Path | `models/v_weights_teacher_minimax2_100000.bin` |
| Size | 188 bytes |
| SHA256 | `24F11809973979C24455B52AAFEA748EA43D8B31A2700FA8FBA8F5B1B661D945` |
| Binary/log checksum | 0.146217 |

Compared with the self-play 100000 model, the checksum dropped from 0.771393 to
0.146217. Most weights collapsed toward zero, while `w15` remained dominant at
0.109911. That is a warning sign for this teacher objective.

## Fixed-seed evaluation vs minimax depth 2

Evaluation uses 5 games with Q as black and 5 games with Q as white.

| Model | Max ply | Q wins | Minimax wins | Draws/timeouts |
| --- | ---: | ---: | ---: | ---: |
| `v_weights_100000.bin` | 1000 | 0 | 4 | 6 |
| `v_weights_minimax2_500.bin` | 1000 | 0 | 3 | 7 |
| `v_weights_teacher_minimax2_100000.bin` | 1000 | 0 | 3 | 7 |
| `v_weights_100000.bin` | 600 | 0 | 5 | 5 |
| `v_weights_minimax2_500.bin` | 600 | 0 | 0 | 10 |
| `v_weights_teacher_minimax2_100000.bin` | 600 | 0 | 6 | 4 |

Detailed teacher-model games:

| Max ply | Q side | Result |
| ---: | --- | --- |
| 1000 | black | 0 Q wins, 2 minimax wins, 3 timeouts |
| 1000 | white | 0 Q wins, 1 minimax win, 4 timeouts |
| 600 | black | 0 Q wins, 2 minimax wins, 3 timeouts |
| 600 | white | 0 Q wins, 4 minimax wins, 1 timeout |

## Conclusion

This depth-2 teacher run is not a good replacement for
`models/v_weights_100000.bin`.

It did not create wins against minimax depth 2. At 1000 ply it matched the
500-game opponent fine-tune only by producing more timeout draws, and at 600 ply
it was worse than the current 100000 self-play model. The low agreement rate and
weight collapse suggest the current 44-feature linear value model cannot imitate
minimax depth-2 move choice well with this after-state preference objective.

## Recommendation

- Keep `models/v_weights_100000.bin` as the current candidate.
- Do not promote `models/v_weights_teacher_minimax2_100000.bin`.
- Use minimax depth 2 mainly as an evaluator until the model has better
  win-seeking features.
- If training against minimax again, optimize for `Q wins - Q losses`, not lower
  loss rate through timeout draws.
- Add anti-draw and conversion features before another long minimax2 run:
  no-progress count, repetition pressure, material conversion, checks/threats,
  marshal safety, and late-game ply pressure.
