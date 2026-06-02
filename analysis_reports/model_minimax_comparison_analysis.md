# Model-guided minimax comparison

Note: this was the first ad-hoc comparison before `src/q_search.c` was added.
Use `hybrid_q_search_stage1_analysis.md` for the current stage-1 implementation
and the larger 40-game result.

## Goal

Compare the original minimax depth-2 AI with a model-guided minimax depth-2 AI.
The original AI is still called through `gungi_get_ai_move()`. The new
comparison tool keeps the minimax search structure, but changes the leaf
evaluation to call the trained value model.

## Tool

- Tool source: `tools/compare_model_minimax.c`
- Model: `models/v_weights_100000.bin`
- Depth: 2 for both players
- Fixed seed: 24680
- Model leaf value scale: 10000
- Original minimax function: `gungi_get_ai_move(state, depth)`
- New search: minimax with alpha-beta, model-based move ordering, and model leaf
  evaluation

The model value is side-to-move positive, so the tool converts it to a
black-positive minimax score:

```c
float v = gungi_v_evaluate(model, state);
float black_score = state->current_player == GUNGI_PLAYER_BLACK ? v : -v;
```

Terminal states are handled before calling the model because
`gungi_v_evaluate()` returns 0 for non-ongoing games.

## Variants

| Variant | Leaf evaluation |
| --- | --- |
| `hybrid` | `gungi_evaluate_board(state) + model_score` |
| `model-only` | `model_score` |

`hybrid` preserves the old material/repetition evaluator and adds the model
signal. `model-only` tests whether the trained model can fully replace the old
evaluation function.

## Results

Each row uses 5 games with model-guided minimax as black and 5 games as white.

| Variant | Max ply | Model-guided wins | Original minimax wins | Draws/timeouts | Log |
| --- | ---: | ---: | ---: | ---: | --- |
| `hybrid` | 600 | 2 | 1 | 7 | `model_minimax_hybrid_100000_depth2_600ply_5x.csv` |
| `hybrid` | 1000 | 4 | 2 | 4 | `model_minimax_hybrid_100000_depth2_1000ply_5x.csv` |
| `model-only` | 600 | 0 | 4 | 6 | `model_minimax_modelonly_100000_depth2_600ply_5x.csv` |

## Detailed hybrid 1000-ply games

| Model side | Result | Ply |
| --- | --- | ---: |
| black | original win | 175 |
| black | timeout | 1000 |
| black | timeout | 1000 |
| black | draw | 105 |
| black | model win | 392 |
| white | model win | 85 |
| white | original win | 242 |
| white | timeout | 1000 |
| white | model win | 509 |
| white | model win | 529 |

## Interpretation

This direction is more promising than directly training a new model to imitate
minimax depth 2. The `hybrid` search produced positive head-to-head results
against the original minimax depth-2 function in both tested ply limits.

The `model-only` result is negative and should not be used. The trained model is
not strong enough to replace material/repetition evaluation by itself.

The remaining issue is still draw rate. `hybrid` at 1000 ply reduced
draws/timeouts from 7 in the 600-ply run to 4, but this sample is still small and
not enough to promote the search as default.

## Recommendation

- Continue with `hybrid`, not `model-only`.
- Keep the original `gungi_get_ai_move()` unchanged for now.
- Add a proper reusable search module only after larger evaluation confirms the
  result.
- Add anti-draw terms to the hybrid leaf score before the next run:
  repetition pressure, no-progress penalty, and late-game ply pressure.
- Next evaluation should run a larger fixed-seed match set, for example 20 games
  per side at depth 2 and max ply 1000.
