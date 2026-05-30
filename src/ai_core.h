#ifndef AI_CORE_H
#define AI_CORE_H

#include "gungi_rules.h"

Move gungi_get_ai_move(const GameState *state, int depth);

// --- 新增：取得隨機合法步數 ---
Move gungi_get_random_move(const GameState *state);

int gungi_evaluate_board(const GameState *state);

#endif