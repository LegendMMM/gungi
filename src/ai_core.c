#include "ai_core.h"
#include <stdlib.h>
#include <limits.h>

#define MAX_LEGAL_MOVES 2048

// 1. 棋子基礎分數設定 (參考 komugi 引擎)
static int get_piece_value(GungiPieceType type) {
    switch (type) {
        case GUNGI_PIECE_MARSHAL:    return 10000;
        case GUNGI_PIECE_GENERAL:      return 900;
        case GUNGI_PIECE_LIEUTENANT: return 600;
        case GUNGI_PIECE_MAJOR:    return 500;
        case GUNGI_PIECE_CAPTAIN:    return 400;
        case GUNGI_PIECE_SAMURAI:    return 350;
        case GUNGI_PIECE_ARCHER:     return 350;
        case GUNGI_PIECE_CANNON:     return 350;
        case GUNGI_PIECE_MUSKETEER:  return 350;
        case GUNGI_PIECE_KNIGHT:     return 300;
        case GUNGI_PIECE_SPY:        return 300;
        case GUNGI_PIECE_SPEAR:      return 250;
        case GUNGI_PIECE_FORT:       return 200;
        case GUNGI_PIECE_PAWN:       return 100;
        default: return 0;
    }
}

// 2. 評估當前盤面對誰有利 (正分黑方優勢，負分白方優勢)
int gungi_evaluate_board(const GameState *state) {
    int score = 0;

    // A. 盤面上的棋子
    for (int y = 0; y < GUNGI_BOARD_SIZE; y++) {
        for (int x = 0; x < GUNGI_BOARD_SIZE; x++) {
            int height = gungi_cell_height(state, x, y);
            if (height > 0) {
                Piece top = gungi_top_piece(state, x, y);
                int val = get_piece_value(top.type);
                
                // 疊得越高，戰鬥力越強
                if (height == 2) val = val * 15 / 10;
                if (height == 3) val = val * 20 / 10;

                if (top.owner == GUNGI_PLAYER_BLACK) score += val;
                else score -= val;
            }
        }
    }

    // B. 手上的備用棋子 (手駒很靈活，給予 1.1 倍加成)
    for (int type = GUNGI_PIECE_NONE + 1; type < GUNGI_PIECE_TYPE_COUNT; ++type) {
        int val = get_piece_value((GungiPieceType)type) * 11 / 10;
        score += gungi_hand_count(state, GUNGI_PLAYER_BLACK, (GungiPieceType)type) * val;
        score -= gungi_hand_count(state, GUNGI_PLAYER_WHITE, (GungiPieceType)type) * val;
    }

    // --- 新增 C：千日手（重複局面）的嚴厲懲罰 ---
    // 利用規則引擎內建的函數，檢查現在這個盤面在歷史中出現過幾次
    int reps = gungi_count_repetition(state, gungi_position_hash(state));
    
    // 如果這個盤面出現超過 1 次，代表有人在繞圈圈
    if (reps > 1) {
        // 判斷是誰造成了這個重複局面？ (是上一步走棋的人)
        // state->current_player 是「輪到誰走」，所以上一步是對手走的
        if (state->current_player == GUNGI_PLAYER_WHITE) {
            // 上一步是黑方走的，嚴懲黑方 (扣 1000 分，相當於損失一個大將)
            score -= 1000 * (reps - 1);
        } else {
            // 上一步是白方走的，嚴懲白方 (因為白方追求負分，所以加分就是懲罰)
            score += 1000 * (reps - 1);
        }
    }

    return score;
}

// 3. 找出當前玩家所有合法的走法
static int generate_legal_moves(const GameState *state, Move *moves) {
    int count = 0;
    GungiPlayer player = state->current_player;

    // 盤面移動
    for (int y = 0; y < GUNGI_BOARD_SIZE; y++) {
        for (int x = 0; x < GUNGI_BOARD_SIZE; x++) {
            if (gungi_cell_height(state, x, y) > 0) {
                Piece top = gungi_top_piece(state, x, y);
                if (top.owner == player) {
                    for (int ty = 0; ty < GUNGI_BOARD_SIZE; ty++) {
                        for (int tx = 0; tx < GUNGI_BOARD_SIZE; tx++) {
                            Move m_move = gungi_make_move(player, x, y, tx, ty);
                            if (gungi_validate_move(state, m_move).ok) {
                                moves[count++] = m_move;
                            }
                            Move m_stack = gungi_make_stack_move(player, x, y, tx, ty);
                            if (gungi_validate_move(state, m_stack).ok) {
                                moves[count++] = m_stack;
                            }
                            Move m_cap = gungi_make_capture_move(player, x, y, tx, ty);
                            if (gungi_validate_move(state, m_cap).ok) {
                                moves[count++] = m_cap;
                            }
                        }
                    }
                }
            }
        }
    }

    // 手駒打入
    for (int type = GUNGI_PIECE_NONE + 1; type < GUNGI_PIECE_TYPE_COUNT; ++type) {
        if (gungi_hand_count(state, player, (GungiPieceType)type) > 0) {
            for (int ty = 0; ty < GUNGI_BOARD_SIZE; ty++) {
                for (int tx = 0; tx < GUNGI_BOARD_SIZE; tx++) {
                    Move m = gungi_make_drop(player, (GungiPieceType)type, tx, ty);
                    if (gungi_validate_move(state, m).ok) {
                        moves[count++] = m;
                    }
                }
            }
        }
    }
    return count;
}

// 4. Minimax 演算法核心 (Alpha-Beta 剪枝)
static int minimax(GameState *state, int depth, int alpha, int beta, int maximizingPlayer) {
    if (depth == 0 || state->status != GUNGI_STATUS_ONGOING) {
        return gungi_evaluate_board(state) + (rand() % 6);
    }

    Move moves[MAX_LEGAL_MOVES];
    int count = generate_legal_moves(state, moves);
    
    if (count == 0) return gungi_evaluate_board(state); // 無路可走

    if (maximizingPlayer) {
        int maxEval = -INT_MAX;
        for (int i = 0; i < count; i++) {
            GameState next_state = *state;
            gungi_apply_move(&next_state, moves[i]);
            int eval = minimax(&next_state, depth - 1, alpha, beta, 0);
            if (eval > maxEval) maxEval = eval;
            if (eval > alpha) alpha = eval;
            if (beta <= alpha) break; // Beta 剪枝
        }
        return maxEval;
    } else {
        int minEval = INT_MAX;
        for (int i = 0; i < count; i++) {
            GameState next_state = *state;
            gungi_apply_move(&next_state, moves[i]);
            int eval = minimax(&next_state, depth - 1, alpha, beta, 1);
            if (eval < minEval) minEval = eval;
            if (eval < beta) beta = eval;
            if (beta <= alpha) break; // Alpha 剪枝
        }
        return minEval;
    }
}

// 5. 對外開放的 AI 函數
Move gungi_get_ai_move(const GameState *state, int depth) {
    Move moves[MAX_LEGAL_MOVES];
    int count = generate_legal_moves(state, moves);

    if (count == 0) {
        return gungi_make_resign(state->current_player); // 認輸
    }

    int bestEval = (state->current_player == GUNGI_PLAYER_BLACK) ? -INT_MAX : INT_MAX;
    Move bestMove = moves[0];

    for (int i = 0; i < count; i++) {
        GameState next_state = *state;
        gungi_apply_move(&next_state, moves[i]);

        int eval = minimax(&next_state, depth - 1, -INT_MAX, INT_MAX, state->current_player == GUNGI_PLAYER_WHITE);
        
        if (state->current_player == GUNGI_PLAYER_BLACK) {
            if (eval > bestEval) { bestEval = eval; bestMove = moves[i]; }
        } else {
            if (eval < bestEval) { bestEval = eval; bestMove = moves[i]; }
        }
    }

    return bestMove;
}

// --- 新增：隨機 AI 實作 ---
Move gungi_get_random_move(const GameState *state) {
    Move moves[MAX_LEGAL_MOVES];
    int count = generate_legal_moves(state, moves);

    if (count == 0) {
        return gungi_make_resign(state->current_player); // 無路可走則認輸
    }

    // 從所有合法的步數中，隨機挑選一步
    int random_index = rand() % count;
    return moves[random_index];
}

