#include "ai_core.h"

#include <limits.h>
#include <stdlib.h>

int gungi_piece_value(GungiPieceType type)
{
    switch (type) {
    case GUNGI_PIECE_MARSHAL: return 10000;
    case GUNGI_PIECE_GENERAL: return 900;
    case GUNGI_PIECE_LIEUTENANT: return 600;
    case GUNGI_PIECE_MAJOR: return 500;
    case GUNGI_PIECE_CAPTAIN: return 400;
    case GUNGI_PIECE_SAMURAI: return 350;
    case GUNGI_PIECE_ARCHER: return 350;
    case GUNGI_PIECE_CANNON: return 350;
    case GUNGI_PIECE_MUSKETEER: return 350;
    case GUNGI_PIECE_KNIGHT: return 300;
    case GUNGI_PIECE_SPY: return 300;
    case GUNGI_PIECE_SPEAR: return 250;
    case GUNGI_PIECE_FORT: return 200;
    case GUNGI_PIECE_PAWN: return 100;
    default: return 0;
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
    if (move.betray_mask < 0 || move.betray_mask > 3) {
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
                int val = gungi_piece_value(top.type);

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
        int val = gungi_piece_value((GungiPieceType)type) * 11 / 10;
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
                                if (append_move(state, m_stack, moves, max_moves, &count) && top.type == GUNGI_PIECE_CAPTAIN) {
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
                    if (append_move(state, m, moves, max_moves, &count) && type == GUNGI_PIECE_CAPTAIN) {
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

static int minimax(GameState *state, int depth, int alpha, int beta, int maximizingPlayer)
{
    Move moves[GUNGI_MAX_LEGAL_MOVES];
    int count;
    int i;

    if (depth == 0 || state->status != GUNGI_STATUS_ONGOING) {
        return gungi_evaluate_board(state) + (rand() % 6);
    }

    count = gungi_generate_legal_moves(state, moves, GUNGI_MAX_LEGAL_MOVES);
    if (count == 0) {
        return gungi_evaluate_board(state);
    }

    if (maximizingPlayer) {
        int maxEval = -INT_MAX;
        for (i = 0; i < count; i++) {
            GameState next_state = *state;
            int eval;
            gungi_apply_move(&next_state, moves[i]);
            eval = minimax(&next_state, depth - 1, alpha, beta, 0);
            if (eval > maxEval) {
                maxEval = eval;
            }
            if (eval > alpha) {
                alpha = eval;
            }
            if (beta <= alpha) {
                break;
            }
        }
        return maxEval;
    } else {
        int minEval = INT_MAX;
        for (i = 0; i < count; i++) {
            GameState next_state = *state;
            int eval;
            gungi_apply_move(&next_state, moves[i]);
            eval = minimax(&next_state, depth - 1, alpha, beta, 1);
            if (eval < minEval) {
                minEval = eval;
            }
            if (eval < beta) {
                beta = eval;
            }
            if (beta <= alpha) {
                break;
            }
        }
        return minEval;
    }
}

Move gungi_get_ai_move(const GameState *state, int depth)
{
    Move moves[GUNGI_MAX_LEGAL_MOVES];
    int count = gungi_generate_legal_moves(state, moves, GUNGI_MAX_LEGAL_MOVES);
    int is_black_turn;
    int bestEval;
    Move bestMove;

    if (count == 0) {
        return gungi_make_resign(state->current_player);
    }

    is_black_turn = state->current_player == GUNGI_PLAYER_BLACK;
    bestEval = is_black_turn ? -INT_MAX : INT_MAX;
    bestMove = moves[0];

#pragma omp parallel for
    for (int i = 0; i < count; i++) {
        GameState next_state = *state;
        int eval;
        gungi_apply_move(&next_state, moves[i]);

        eval = minimax(&next_state, depth - 1, -INT_MAX, INT_MAX, state->current_player == GUNGI_PLAYER_WHITE);

#pragma omp critical
        {
            if (is_black_turn) {
                if (eval > bestEval) {
                    bestEval = eval;
                    bestMove = moves[i];
                }
            } else {
                if (eval < bestEval) {
                    bestEval = eval;
                    bestMove = moves[i];
                }
            }
        }
    }

    return bestMove;
}

Move gungi_get_random_move(const GameState *state)
{
    Move moves[GUNGI_MAX_LEGAL_MOVES];
    int count = gungi_generate_legal_moves(state, moves, GUNGI_MAX_LEGAL_MOVES);
    int random_index;

    if (count == 0) {
        return gungi_make_resign(state->current_player);
    }

    random_index = rand() % count;
    return moves[random_index];
}
