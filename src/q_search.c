#include "q_search.h"

#include "q_model.h"

#include <limits.h>
#include <stdlib.h>

#define HYBRID_WIN_SCORE 100000000

typedef struct HybridMoveOrder {
    Move move;
    int score;
} HybridMoveOrder;

const GungiHybridSearchConfig GUNGI_HYBRID_SEARCH_DEFAULT_CONFIG = {
    GUNGI_HYBRID_EVAL_HYBRID,
    30000.0f,
    0,
    0,
    0,
    600
};

static int compare_move_order_desc(const void *left, const void *right)
{
    const HybridMoveOrder *a = (const HybridMoveOrder *)left;
    const HybridMoveOrder *b = (const HybridMoveOrder *)right;

    if (a->score < b->score) {
        return 1;
    }
    if (a->score > b->score) {
        return -1;
    }
    return 0;
}

static const GungiHybridSearchConfig *effective_config(const GungiHybridSearchConfig *config)
{
    return config != NULL ? config : &GUNGI_HYBRID_SEARCH_DEFAULT_CONFIG;
}

static int terminal_black_score(const GameState *state, const GungiHybridSearchConfig *config)
{
    if (state->status == GUNGI_STATUS_BLACK_WIN ||
        (state->status == GUNGI_STATUS_RESIGNED && state->winner == GUNGI_PLAYER_BLACK)) {
        return HYBRID_WIN_SCORE;
    }
    if (state->status == GUNGI_STATUS_WHITE_WIN ||
        (state->status == GUNGI_STATUS_RESIGNED && state->winner == GUNGI_PLAYER_WHITE)) {
        return -HYBRID_WIN_SCORE;
    }
    if (state->status == GUNGI_STATUS_DRAW) {
        return state->current_player == GUNGI_PLAYER_BLACK ? config->draw_penalty : -config->draw_penalty;
    }
    return 0;
}

static int side_to_move_penalty_black_score(const GameState *state, int penalty)
{
    if (penalty <= 0) {
        return 0;
    }
    return state->current_player == GUNGI_PLAYER_BLACK ? -penalty : penalty;
}

static int repetition_pressure_black_score(const GameState *state, const GungiHybridSearchConfig *config)
{
    int reps;
    int penalty;

    if (config->repetition_penalty <= 0) {
        return 0;
    }

    reps = gungi_count_repetition(state, gungi_position_hash(state));
    if (reps <= 1) {
        return 0;
    }

    penalty = config->repetition_penalty * (reps - 1);
    return side_to_move_penalty_black_score(state, penalty);
}

static int late_game_pressure_black_score(const GameState *state, const GungiHybridSearchConfig *config)
{
    unsigned int over;
    int penalty;

    if (config->late_game_penalty <= 0 || state->ply_count <= config->late_game_start_ply) {
        return 0;
    }

    over = state->ply_count - config->late_game_start_ply;
    penalty = config->late_game_penalty * (int)(over / 100U + 1U);
    return side_to_move_penalty_black_score(state, penalty);
}

static int model_black_score(const GungiQModel *model, const GameState *state, float model_scale)
{
    float value = gungi_v_evaluate(model, state);
    if (state->current_player == GUNGI_PLAYER_WHITE) {
        value = -value;
    }
    return (int)(value * model_scale);
}

int gungi_hybrid_leaf_score(const GungiQModel *model,
                            const GameState *state,
                            const GungiHybridSearchConfig *config)
{
    const GungiHybridSearchConfig *active = effective_config(config);
    int score;

    if (model == NULL || state == NULL) {
        return 0;
    }

    if (state->status != GUNGI_STATUS_ONGOING) {
        return terminal_black_score(state, active);
    }

    score = model_black_score(model, state, active->model_scale);
    if (active->mode == GUNGI_HYBRID_EVAL_HYBRID) {
        score += gungi_evaluate_board(state);
    }

    score += repetition_pressure_black_score(state, active);
    score += late_game_pressure_black_score(state, active);

    return score;
}

static void order_moves(const GungiQModel *model,
                        const GameState *state,
                        Move *moves,
                        int count,
                        int maximizing,
                        const GungiHybridSearchConfig *config)
{
    HybridMoveOrder ordered[GUNGI_MAX_LEGAL_MOVES];
    int i;

    for (i = 0; i < count; ++i) {
        GameState next_state = *state;
        int score;

        gungi_apply_move(&next_state, moves[i]);
        score = gungi_hybrid_leaf_score(model, &next_state, config);
        ordered[i].move = moves[i];
        ordered[i].score = maximizing ? score : -score;
    }

    qsort(ordered, (size_t)count, sizeof(ordered[0]), compare_move_order_desc);

    for (i = 0; i < count; ++i) {
        moves[i] = ordered[i].move;
    }
}

static int hybrid_minimax(GameState *state,
                          const GungiQModel *model,
                          int depth,
                          int alpha,
                          int beta,
                          const GungiHybridSearchConfig *config)
{
    Move moves[GUNGI_MAX_LEGAL_MOVES];
    int count;
    int maximizing;
    int i;

    if (depth == 0 || state->status != GUNGI_STATUS_ONGOING) {
        return gungi_hybrid_leaf_score(model, state, config);
    }

    count = gungi_generate_legal_moves(state, moves, GUNGI_MAX_LEGAL_MOVES);
    if (count == 0) {
        return state->current_player == GUNGI_PLAYER_BLACK ? -HYBRID_WIN_SCORE : HYBRID_WIN_SCORE;
    }

    maximizing = state->current_player == GUNGI_PLAYER_BLACK;
    order_moves(model, state, moves, count, maximizing, config);

    if (maximizing) {
        int best = -INT_MAX;
        for (i = 0; i < count; ++i) {
            GameState next_state = *state;
            int eval;

            gungi_apply_move(&next_state, moves[i]);
            eval = hybrid_minimax(&next_state, model, depth - 1, alpha, beta, config);
            if (eval > best) {
                best = eval;
            }
            if (eval > alpha) {
                alpha = eval;
            }
            if (beta <= alpha) {
                break;
            }
        }
        return best;
    } else {
        int best = INT_MAX;
        for (i = 0; i < count; ++i) {
            GameState next_state = *state;
            int eval;

            gungi_apply_move(&next_state, moves[i]);
            eval = hybrid_minimax(&next_state, model, depth - 1, alpha, beta, config);
            if (eval < best) {
                best = eval;
            }
            if (eval < beta) {
                beta = eval;
            }
            if (beta <= alpha) {
                break;
            }
        }
        return best;
    }
}

int gungi_hybrid_search_score(const GameState *state,
                              const GungiQModel *model,
                              int depth,
                              const GungiHybridSearchConfig *config)
{
    const GungiHybridSearchConfig *active = effective_config(config);
    GameState copy;

    if (state == NULL || model == NULL) {
        return 0;
    }

    if (depth <= 0) {
        depth = 1;
    }

    copy = *state;
    return hybrid_minimax(&copy, model, depth, -INT_MAX, INT_MAX, active);
}

Move gungi_get_hybrid_ai_move_scored(const GameState *state,
                                     const GungiQModel *model,
                                     int depth,
                                     const GungiHybridSearchConfig *config,
                                     int *out_black_score)
{
    const GungiHybridSearchConfig *active = effective_config(config);
    Move moves[GUNGI_MAX_LEGAL_MOVES];
    int count;
    int is_black_turn;
    int best_eval;
    Move best_move;
    int i;

    if (out_black_score != NULL) {
        *out_black_score = 0;
    }

    if (state == NULL || model == NULL) {
        return gungi_make_resign(state != NULL ? state->current_player : GUNGI_PLAYER_BLACK);
    }

    if (depth <= 0) {
        depth = 1;
    }

    count = gungi_generate_legal_moves(state, moves, GUNGI_MAX_LEGAL_MOVES);
    if (count == 0) {
        return gungi_make_resign(state->current_player);
    }

    is_black_turn = state->current_player == GUNGI_PLAYER_BLACK;
    best_eval = is_black_turn ? -INT_MAX : INT_MAX;
    best_move = moves[0];
    order_moves(model, state, moves, count, is_black_turn, active);

#pragma omp parallel for
    for (i = 0; i < count; ++i) {
        GameState next_state = *state;
        int eval;

        gungi_apply_move(&next_state, moves[i]);
        eval = hybrid_minimax(&next_state, model, depth - 1, -INT_MAX, INT_MAX, active);

#pragma omp critical
        {
            if (is_black_turn) {
                if (eval > best_eval) {
                    best_eval = eval;
                    best_move = moves[i];
                }
            } else {
                if (eval < best_eval) {
                    best_eval = eval;
                    best_move = moves[i];
                }
            }
        }
    }

    if (out_black_score != NULL) {
        *out_black_score = best_eval;
    }

    return best_move;
}

Move gungi_get_hybrid_ai_move(const GameState *state,
                              const GungiQModel *model,
                              int depth,
                              const GungiHybridSearchConfig *config)
{
    return gungi_get_hybrid_ai_move_scored(state, model, depth, config, NULL);
}
