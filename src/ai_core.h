#ifndef AI_CORE_H
#define AI_CORE_H

#include "gungi_rules.h"

#define GUNGI_MAX_LEGAL_MOVES 2048

int gungi_piece_value(GungiPieceType type);

int gungi_generate_legal_moves(const GameState *state, Move *moves, int max_moves);

Move gungi_get_ai_move(const GameState *state, int depth);

Move gungi_get_random_move(const GameState *state);

int gungi_evaluate_board(const GameState *state);

#endif
