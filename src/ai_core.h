#ifndef AI_CORE_H
#define AI_CORE_H

#include "gungi_rules.h"
#include "value_model.h"

#define GUNGI_MAX_LEGAL_MOVES 2048
#define GUNGI_VALUE_AI_DEFAULT_DEPTH 2
#define GUNGI_VALUE_AI_DEFAULT_TOP_K 12

typedef struct GungiAiSearchStats {
    long long nodes;
    long long leaves;
    long long cutoffs;
    int root_moves;
    int searched_root_moves;
    int pruned_root_moves;
    int tactical_root_moves;
    int searched_tactical_root_moves;
} GungiAiSearchStats;

void gungi_ai_stats_clear(GungiAiSearchStats *stats);

int gungi_generate_legal_moves(const GameState *state, Move *moves, int max_moves);
int gungi_score_ai_position(const GameState *state, int depth);
Move gungi_get_ai_move(const GameState *state, int depth);
Move gungi_get_random_move(const GameState *state);
Move gungi_get_value_ai_move(const GameState *state,
                             const GungiValueModel *model,
                             int depth,
                             int top_k,
                             GungiAiSearchStats *stats);

int gungi_evaluate_board(const GameState *state);

#endif
