#include "ai_core.h"

#include <limits.h>
#include <stdlib.h>
#include <string.h>

typedef struct ScoredMove {
    Move move;
    int score;
} ScoredMove;

#define VALUE_AI_INTERNAL_TOP_K 8

void gungi_ai_stats_clear(GungiAiSearchStats *stats)
{
    if (stats != NULL) {
        memset(stats, 0, sizeof(*stats));
    }
}

static int same_move(Move a, Move b)
{
    return a.kind == b.kind &&
           a.player == b.player &&
           a.from_x == b.from_x &&
           a.from_y == b.from_y &&
           a.to_x == b.to_x &&
           a.to_y == b.to_y &&
           a.drop_type == b.drop_type &&
           a.intent == b.intent &&
           a.betray_mask == b.betray_mask;
}

static int append_move(const GameState *state, Move move, Move *moves, int max_moves, int *count)
{
    int i;

    if (*count >= max_moves || !gungi_validate_move(state, move).ok) {
        return 0;
    }

    for (i = 0; i < *count; ++i) {
        if (same_move(moves[i], move)) {
            return 0;
        }
    }

    moves[*count] = move;
    (*count)++;
    return 1;
}

static int move_changes_betrayal_target(const GameState *state, Move move)
{
    GameState after;
    int before_height;
    int level;

    if (move.betray_mask == 0) {
        return 1;
    }
    if (state == NULL || move.betray_mask < 0 || move.betray_mask > 3) {
        return 0;
    }

    after = *state;
    if (!gungi_apply_move(&after, move).ok) {
        return 0;
    }

    before_height = gungi_cell_height(state, move.to_x, move.to_y);
    for (level = 0; level < 2 && level < before_height; ++level) {
        Piece before_piece = gungi_stack_piece(state, move.to_x, move.to_y, level);
        Piece after_piece = gungi_stack_piece(&after, move.to_x, move.to_y, level);
        if ((move.betray_mask & (1 << level)) != 0 &&
            before_piece.owner != move.player &&
            after_piece.owner == move.player) {
            return 1;
        }
    }

    return 0;
}

static void append_betrayal_variants(const GameState *state, Move base, Move *moves, int max_moves, int *count)
{
    int mask;

    for (mask = 1; mask <= 3; ++mask) {
        Move betrayal = base;
        betrayal.betray_mask = mask;
        if (move_changes_betrayal_target(state, betrayal)) {
            append_move(state, betrayal, moves, max_moves, count);
        }
    }
}

int gungi_evaluate_board(const GameState *state)
{
    int score = 0;
    int y;
    int x;
    int type;
    int reps;

    if (state == NULL) {
        return 0;
    }

    for (y = 0; y < GUNGI_BOARD_SIZE; y++) {
        for (x = 0; x < GUNGI_BOARD_SIZE; x++) {
            int height = gungi_cell_height(state, x, y);
            if (height > 0) {
                Piece top = gungi_top_piece(state, x, y);
                int val = gungi_value_piece_value(top.type);

                if (height == 2) {
                    val = val * 15 / 10;
                } else if (height == 3) {
                    val = val * 20 / 10;
                }

                if (top.owner == GUNGI_PLAYER_BLACK) {
                    score += val;
                } else if (top.owner == GUNGI_PLAYER_WHITE) {
                    score -= val;
                }
            }
        }
    }

    for (type = GUNGI_PIECE_NONE + 1; type < GUNGI_PIECE_TYPE_COUNT; ++type) {
        int val = gungi_value_piece_value((GungiPieceType)type) * 11 / 10;
        score += gungi_hand_count(state, GUNGI_PLAYER_BLACK, (GungiPieceType)type) * val;
        score -= gungi_hand_count(state, GUNGI_PLAYER_WHITE, (GungiPieceType)type) * val;
    }

    reps = gungi_count_repetition(state, gungi_position_hash(state));
    if (reps > 1) {
        if (state->current_player == GUNGI_PLAYER_WHITE) {
            score -= 1000 * (reps - 1);
        } else {
            score += 1000 * (reps - 1);
        }
    }

    return score;
}

int gungi_generate_legal_moves(const GameState *state, Move *moves, int max_moves)
{
    int count = 0;
    GungiPlayer player;
    int y;
    int x;
    int ty;
    int tx;
    int type;

    if (state == NULL || moves == NULL || max_moves <= 0 || state->status != GUNGI_STATUS_ONGOING) {
        return 0;
    }

    player = state->current_player;

    for (y = 0; y < GUNGI_BOARD_SIZE; y++) {
        for (x = 0; x < GUNGI_BOARD_SIZE; x++) {
            if (gungi_cell_height(state, x, y) > 0) {
                Piece top = gungi_top_piece(state, x, y);
                if (top.owner == player) {
                    for (ty = 0; ty < GUNGI_BOARD_SIZE; ty++) {
                        for (tx = 0; tx < GUNGI_BOARD_SIZE; tx++) {
                            Move m_move = gungi_make_move(player, x, y, tx, ty);
                            Move m_stack = gungi_make_stack_move(player, x, y, tx, ty);
                            Move m_cap = gungi_make_capture_move(player, x, y, tx, ty);
                            int target_height = gungi_cell_height(state, tx, ty);

                            if (target_height <= 0) {
                                append_move(state, m_move, moves, max_moves, &count);
                            } else {
                                if (append_move(state, m_stack, moves, max_moves, &count) &&
                                    top.type == GUNGI_PIECE_CAPTAIN) {
                                    append_betrayal_variants(state, m_stack, moves, max_moves, &count);
                                }
                                append_move(state, m_cap, moves, max_moves, &count);
                            }
                            if (count >= max_moves) {
                                return count;
                            }
                        }
                    }
                }
            }
        }
    }

    for (type = GUNGI_PIECE_NONE + 1; type < GUNGI_PIECE_TYPE_COUNT; ++type) {
        if (gungi_hand_count(state, player, (GungiPieceType)type) > 0) {
            for (ty = 0; ty < GUNGI_BOARD_SIZE; ty++) {
                for (tx = 0; tx < GUNGI_BOARD_SIZE; tx++) {
                    Move m = gungi_make_drop(player, (GungiPieceType)type, tx, ty);
                    if (append_move(state, m, moves, max_moves, &count) &&
                        type == GUNGI_PIECE_CAPTAIN) {
                        append_betrayal_variants(state, m, moves, max_moves, &count);
                    }
                    if (count >= max_moves) {
                        return count;
                    }
                }
            }
        }
    }

    return count;
}

static int terminal_score(const GameState *state)
{
    if (state == NULL) {
        return 0;
    }

    if (state->status == GUNGI_STATUS_BLACK_WIN) {
        return 30000;
    }
    if (state->status == GUNGI_STATUS_WHITE_WIN) {
        return -30000;
    }
    if (state->status == GUNGI_STATUS_RESIGNED) {
        if (state->winner == GUNGI_PLAYER_BLACK) {
            return 30000;
        }
        if (state->winner == GUNGI_PLAYER_WHITE) {
            return -30000;
        }
    }
    if (state->status == GUNGI_STATUS_DRAW) {
        return 0;
    }

    return gungi_evaluate_board(state);
}

static int heuristic_leaf_score(const GameState *state)
{
    if (state == NULL || state->status != GUNGI_STATUS_ONGOING) {
        return terminal_score(state);
    }
    return gungi_evaluate_board(state);
}

static int value_leaf_score(const GungiValueModel *model, const GameState *state)
{
    if (state == NULL || state->status != GUNGI_STATUS_ONGOING) {
        return terminal_score(state);
    }
    if (!gungi_value_model_loaded(model)) {
        return gungi_evaluate_board(state);
    }
    return gungi_value_evaluate_board(model, state);
}

static int minimax_heuristic(GameState *state, int depth, int alpha, int beta)
{
    Move moves[GUNGI_MAX_LEGAL_MOVES];
    int count;
    int i;

    if (depth <= 0 || state->status != GUNGI_STATUS_ONGOING) {
        return heuristic_leaf_score(state);
    }

    count = gungi_generate_legal_moves(state, moves, GUNGI_MAX_LEGAL_MOVES);
    if (count == 0) {
        return heuristic_leaf_score(state);
    }

    if (state->current_player == GUNGI_PLAYER_BLACK) {
        int max_eval = -INT_MAX;
        for (i = 0; i < count; i++) {
            GameState next_state = *state;
            if (!gungi_apply_move(&next_state, moves[i]).ok) {
                continue;
            }
            {
                int eval = minimax_heuristic(&next_state, depth - 1, alpha, beta);
                if (eval > max_eval) {
                    max_eval = eval;
                }
                if (eval > alpha) {
                    alpha = eval;
                }
                if (beta <= alpha) {
                    break;
                }
            }
        }
        return max_eval;
    } else {
        int min_eval = INT_MAX;
        for (i = 0; i < count; i++) {
            GameState next_state = *state;
            if (!gungi_apply_move(&next_state, moves[i]).ok) {
                continue;
            }
            {
                int eval = minimax_heuristic(&next_state, depth - 1, alpha, beta);
                if (eval < min_eval) {
                    min_eval = eval;
                }
                if (eval < beta) {
                    beta = eval;
                }
                if (beta <= alpha) {
                    break;
                }
            }
        }
        return min_eval;
    }
}

int gungi_score_ai_position(const GameState *state, int depth)
{
    GameState copy;

    if (state == NULL) {
        return 0;
    }
    if (depth <= 0 || state->status != GUNGI_STATUS_ONGOING) {
        return heuristic_leaf_score(state);
    }

    copy = *state;
    return minimax_heuristic(&copy, depth, -INT_MAX, INT_MAX);
}

static int compare_scored_desc(const void *left, const void *right)
{
    const ScoredMove *a = (const ScoredMove *)left;
    const ScoredMove *b = (const ScoredMove *)right;

    if (a->score < b->score) {
        return 1;
    }
    if (a->score > b->score) {
        return -1;
    }
    return 0;
}

static int compare_scored_asc(const void *left, const void *right)
{
    const ScoredMove *a = (const ScoredMove *)left;
    const ScoredMove *b = (const ScoredMove *)right;

    if (a->score > b->score) {
        return 1;
    }
    if (a->score < b->score) {
        return -1;
    }
    return 0;
}

static int order_moves_by_value(const GameState *state,
                                const GungiValueModel *model,
                                ScoredMove *ordered,
                                int max_moves)
{
    Move moves[GUNGI_MAX_LEGAL_MOVES];
    int count = gungi_generate_legal_moves(state, moves, max_moves);
    int i;

    for (i = 0; i < count; ++i) {
        GameState next_state = *state;
        ordered[i].move = moves[i];
        if (gungi_apply_move(&next_state, moves[i]).ok) {
            ordered[i].score = value_leaf_score(model, &next_state);
        } else {
            ordered[i].score = state->current_player == GUNGI_PLAYER_BLACK ? -INT_MAX : INT_MAX;
        }
    }

    if (state->current_player == GUNGI_PLAYER_BLACK) {
        qsort(ordered, (size_t)count, sizeof(ordered[0]), compare_scored_desc);
    } else {
        qsort(ordered, (size_t)count, sizeof(ordered[0]), compare_scored_asc);
    }

    return count;
}

static int value_minimax(GameState *state,
                         const GungiValueModel *model,
                         int depth,
                         int alpha,
                         int beta,
                         int top_k,
                         GungiAiSearchStats *stats)
{
    ScoredMove ordered[GUNGI_MAX_LEGAL_MOVES];
    int count;
    int search_count;
    int i;

    if (stats != NULL) {
        stats->nodes++;
    }

    if (depth <= 0 || state->status != GUNGI_STATUS_ONGOING) {
        if (stats != NULL) {
            stats->leaves++;
        }
        return value_leaf_score(model, state);
    }

    count = order_moves_by_value(state, model, ordered, GUNGI_MAX_LEGAL_MOVES);
    if (count == 0) {
        if (stats != NULL) {
            stats->leaves++;
        }
        return value_leaf_score(model, state);
    }

    search_count = count;
    if (top_k > VALUE_AI_INTERNAL_TOP_K) {
        top_k = VALUE_AI_INTERNAL_TOP_K;
    }
    if (top_k > 0 && search_count > top_k) {
        search_count = top_k;
    }

    if (state->current_player == GUNGI_PLAYER_BLACK) {
        int max_eval = -INT_MAX;
        for (i = 0; i < search_count; ++i) {
            GameState next_state = *state;
            if (!gungi_apply_move(&next_state, ordered[i].move).ok) {
                continue;
            }
            {
                int eval = value_minimax(&next_state, model, depth - 1, alpha, beta, top_k, stats);
                if (eval > max_eval) {
                    max_eval = eval;
                }
                if (eval > alpha) {
                    alpha = eval;
                }
                if (beta <= alpha) {
                    if (stats != NULL) {
                        stats->cutoffs++;
                    }
                    break;
                }
            }
        }
        return max_eval;
    } else {
        int min_eval = INT_MAX;
        for (i = 0; i < search_count; ++i) {
            GameState next_state = *state;
            if (!gungi_apply_move(&next_state, ordered[i].move).ok) {
                continue;
            }
            {
                int eval = value_minimax(&next_state, model, depth - 1, alpha, beta, top_k, stats);
                if (eval < min_eval) {
                    min_eval = eval;
                }
                if (eval < beta) {
                    beta = eval;
                }
                if (beta <= alpha) {
                    if (stats != NULL) {
                        stats->cutoffs++;
                    }
                    break;
                }
            }
        }
        return min_eval;
    }
}

Move gungi_get_ai_move(const GameState *state, int depth)
{
    Move moves[GUNGI_MAX_LEGAL_MOVES];
    int count = gungi_generate_legal_moves(state, moves, GUNGI_MAX_LEGAL_MOVES);
    int is_black_turn;
    int best_eval;
    Move best_move;
    int i;

    if (state == NULL || count == 0) {
        return gungi_make_resign(state != NULL ? state->current_player : GUNGI_PLAYER_BLACK);
    }

    if (depth < 1) {
        depth = 1;
    }

    is_black_turn = state->current_player == GUNGI_PLAYER_BLACK;
    best_eval = is_black_turn ? -INT_MAX : INT_MAX;
    best_move = moves[0];

#pragma omp parallel for
    for (i = 0; i < count; i++) {
        GameState next_state = *state;
        int eval;
        if (!gungi_apply_move(&next_state, moves[i]).ok) {
            continue;
        }

        eval = minimax_heuristic(&next_state, depth - 1, -INT_MAX, INT_MAX);

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

    return best_move;
}

Move gungi_get_random_move(const GameState *state)
{
    Move moves[GUNGI_MAX_LEGAL_MOVES];
    int count = gungi_generate_legal_moves(state, moves, GUNGI_MAX_LEGAL_MOVES);

    if (state == NULL || count == 0) {
        return gungi_make_resign(state != NULL ? state->current_player : GUNGI_PLAYER_BLACK);
    }

    return moves[rand() % count];
}

Move gungi_get_value_ai_move(const GameState *state,
                             const GungiValueModel *model,
                             int depth,
                             int top_k,
                             GungiAiSearchStats *stats)
{
    ScoredMove ordered[GUNGI_MAX_LEGAL_MOVES];
    int count;
    int search_count;
    int is_black_turn;
    int best_eval;
    Move best_move;
    int i;

    if (stats != NULL) {
        gungi_ai_stats_clear(stats);
    }

    if (state == NULL) {
        return gungi_make_resign(GUNGI_PLAYER_BLACK);
    }
    if (!gungi_value_model_loaded(model)) {
        return gungi_get_ai_move(state, 2);
    }
    if (depth < 1) {
        depth = GUNGI_VALUE_AI_DEFAULT_DEPTH;
    }
    if (top_k <= 0 || top_k > GUNGI_MAX_LEGAL_MOVES) {
        top_k = GUNGI_VALUE_AI_DEFAULT_TOP_K;
    }

    count = order_moves_by_value(state, model, ordered, GUNGI_MAX_LEGAL_MOVES);
    if (count == 0) {
        return gungi_make_resign(state->current_player);
    }

    search_count = count;
    if (search_count > top_k) {
        search_count = top_k;
    }

    if (stats != NULL) {
        stats->root_moves = count;
        stats->searched_root_moves = search_count;
        stats->pruned_root_moves = count - search_count;
    }

    is_black_turn = state->current_player == GUNGI_PLAYER_BLACK;
    best_eval = is_black_turn ? -INT_MAX : INT_MAX;
    best_move = ordered[0].move;

    for (i = 0; i < search_count; ++i) {
        GameState next_state = *state;
        if (!gungi_apply_move(&next_state, ordered[i].move).ok) {
            continue;
        }
        {
            int eval = value_minimax(&next_state, model, depth - 1, -INT_MAX, INT_MAX, top_k, stats);
            if (is_black_turn) {
                if (eval > best_eval) {
                    best_eval = eval;
                    best_move = ordered[i].move;
                }
            } else {
                if (eval < best_eval) {
                    best_eval = eval;
                    best_move = ordered[i].move;
                }
            }
        }
    }

    return best_move;
}
