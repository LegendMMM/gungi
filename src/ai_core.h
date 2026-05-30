#ifndef AI_CORE_H
#define AI_CORE_H

#include "gungi_rules.h"

Move gungi_get_ai_move(const GameState *state, int depth);

Move gungi_get_random_move(const GameState *state);

int gungi_evaluate_board(const GameState *state);

#endif