# 100000-game Q training analysis

## Run record

- Command output log: `train_100000.log`
- Model: `models/v_weights_100000.bin`
- Analysis JSON: `train_100000_analysis.json`
- Started: 2026-06-02 13:33:54
- Finished: 2026-06-02 16:25:04
- Duration: about 2h 51m
- Profile: `balanced-value`
- Stderr: 0 bytes

## Completeness checks

- Summary lines: 200 / 200
- Per-game weight lines: 100000 / 100000
- Final line: `Episode 100000/100000`
- Final result counts: 15631 + 10327 + 74042 = 100000
- Timeouts: 0
- Bad weight lines: 0
- NaN/Inf weights: 0

## Final 100000 result

| Metric | Value |
| --- | ---: |
| Black wins | 15631 (15.63%) |
| White wins | 10327 (10.33%) |
| Draws | 74042 (74.04%) |
| Timeouts | 0 (0.00%) |
| Average ply | 664.0 |
| Final epsilon | 0.050 |
| Final checksum | 0.771393 |
| Max abs weight | w15 = 0.101754 |

## Model file check

| Check | Value |
| --- | --- |
| Path | `models/v_weights_100000.bin` |
| Size | 188 bytes |
| Magic | `GUNGIV3\\0` |
| Features | 44 |
| SHA256 | `BE27C4187E4366188C29B26F35D3963C5BC3100DA8797913EEA6C06AD071C16A` |
| Binary checksum | 0.771393 |
| Matches log checksum | yes |

`eval_q` validation passed: the 100000-game model loaded successfully and completed a Q self-play game as a draw in 214 ply with no timeout.

## Compared with 30000 baseline

| Metric | 30000 model | 100000 model | Delta |
| --- | ---: | ---: | ---: |
| Black win rate | 19.40% | 15.63% | -3.77 pp |
| White win rate | 12.88% | 10.33% | -2.55 pp |
| Draw rate | 67.72% | 74.04% | +6.32 pp |
| Timeout rate | 0.00% | 0.00% | 0.00 pp |
| Average ply | 614.4 | 664.0 | +49.6 |
| Checksum | 0.706982 | 0.771393 | +0.064411 |
| Max abs weight | 0.091651 | 0.101754 | +0.010103 |

Interpretation: the 100000-game run did not explode or collapse. It shifted further toward draws and longer games, while keeping the model weight scale close to the 30000-game baseline.

## 100000-run checkpoints

| Episode | Epsilon | B rate | W rate | Draw rate | Avg ply |
| ---: | ---: | ---: | ---: | ---: | ---: |
| 30000 | 0.225 | 18.73% | 12.92% | 68.35% | 648.7 |
| 50000 | 0.175 | 17.84% | 12.12% | 70.03% | 636.8 |
| 75000 | 0.113 | 16.89% | 11.35% | 71.76% | 633.8 |
| 100000 | 0.050 | 15.63% | 10.33% | 74.04% | 664.0 |

The 100000 run's episode 30000 checkpoint is not directly comparable to the 30000-run final model because epsilon was still 0.225 there. The clean comparison is final-to-final at epsilon 0.050.

## Risk notes

- No hard failures: complete log, complete model save, valid binary format, no bad weights.
- No timeout pattern: final `T` is 0 and every summary checkpoint stayed at 0 timeouts.
- Weight scale is safe: final checksum is 0.771393, close to the 30000 baseline and far below explosion thresholds.
- Draw rate increased to 74.04%, so the model may be learning more conservative play rather than more decisive wins.
- Average ply rose by 8.07% versus the 30000 baseline, still below the 25% drift threshold.
- The largest 500-game approximate window average was 1189.3 ply for episodes 92501-93000. This is a late-run long-game spike worth watching in future larger runs, but it did not cause timeouts.
