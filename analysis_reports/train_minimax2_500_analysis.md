# Minimax depth-2 fine-tune, 500 games

## Setup

- Base model: `models/v_weights_100000.bin`
- Output model: `models/v_weights_minimax2_500.bin`
- Training games: 500
- Opponent: minimax depth 2
- Q side: alternating black/white
- Max ply: 600
- Learning rate: 0.001
- True draw reward: -0.5
- Timeout reward: -0.7
- Timeout material advantage reward: +0.25
- Timeout material disadvantage reward: -0.6
- Adjudication margin: 250 material score

## Training result

| Metric | Value |
| --- | ---: |
| Q terminal wins | 7 |
| Minimax terminal wins | 115 |
| True draws | 16 |
| Timeouts | 362 |
| Q material advantages at timeout | 29 |
| Minimax material advantages at timeout | 325 |
| Average ply | 488.45 |
| Final checksum | 1.070527 |
| Max abs weight | w15 = 0.158689 |

## Fixed-seed evaluation vs minimax depth 2

Evaluation: 5 games with Q as black, 5 games with Q as white.

| Model | Max ply | Q wins | Minimax wins | Draws/timeouts |
| --- | ---: | ---: | ---: | ---: |
| `v_weights_100000.bin` | 1000 | 0 | 4 | 6 |
| `v_weights_minimax2_500.bin` | 1000 | 0 | 3 | 7 |
| `v_weights_100000.bin` | 600 | 0 | 5 | 5 |
| `v_weights_minimax2_500.bin` | 600 | 0 | 0 | 10 |

## Conclusion

The fine-tune did not produce the desired behavior. It reduced immediate losses against minimax depth 2, but it did so by increasing timeout/draw outcomes rather than learning to win.

This is not a good candidate to replace the current default model. The result supports the earlier concern: training against minimax2 with terminal/timeout reward alone teaches the Q model to survive and stall, not to convert advantages.

## Next attempt

- Keep `v_weights_100000.bin` as the base.
- Add explicit win-seeking shaping, not just draw/timeout penalties.
- Track capture value, checks, marshal attacks, and repetition avoidance.
- Consider using minimax2 as a move teacher on Q turns for selected positions, rather than only as an opponent.
- Select models by `Q wins - Q losses`, not by lower draw rate alone.
