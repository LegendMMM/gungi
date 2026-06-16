#ifndef Q_SEARCH_H
#define Q_SEARCH_H

#include "gungi_rules.h"

struct GungiQModel;
struct GungiVPModel;

typedef enum GungiHybridEvalMode {
    GUNGI_HYBRID_EVAL_MODEL_ONLY = 0,
    GUNGI_HYBRID_EVAL_HYBRID = 1
} GungiHybridEvalMode;

typedef struct GungiHybridSearchConfig {
    GungiHybridEvalMode mode;
    float model_scale;
    int draw_penalty;
    int repetition_penalty;
    int late_game_penalty;
    unsigned int late_game_start_ply;
} GungiHybridSearchConfig;

extern const GungiHybridSearchConfig GUNGI_HYBRID_SEARCH_DEFAULT_CONFIG;

int gungi_hybrid_leaf_score(const struct GungiQModel *model,
                            const GameState *state,
                            const GungiHybridSearchConfig *config);

int gungi_hybrid_search_score(const GameState *state,
                              const struct GungiQModel *model,
                              int depth,
                              const GungiHybridSearchConfig *config);

Move gungi_get_hybrid_ai_move(const GameState *state,
                              const struct GungiQModel *model,
                              int depth,
                              const GungiHybridSearchConfig *config);

Move gungi_get_hybrid_ai_move_scored(const GameState *state,
                                     const struct GungiQModel *model,
                                     int depth,
                                     const GungiHybridSearchConfig *config,
                                     int *out_black_score);

int gungi_vp_hybrid_leaf_score(const struct GungiVPModel *model,
                               const GameState *state,
                               const GungiHybridSearchConfig *config);

int gungi_vp_hybrid_search_score(const GameState *state,
                                 const struct GungiVPModel *model,
                                 int depth,
                                 const GungiHybridSearchConfig *config);

Move gungi_get_vp_hybrid_ai_move(const GameState *state,
                                 const struct GungiVPModel *model,
                                 int depth,
                                 const GungiHybridSearchConfig *config);

Move gungi_get_vp_hybrid_ai_move_scored(const GameState *state,
                                        const struct GungiVPModel *model,
                                        int depth,
                                        const GungiHybridSearchConfig *config,
                                        int *out_black_score);

Move gungi_get_vp_side_policy_hybrid_ai_move_scored(const GameState *state,
                                                    const struct GungiVPModel *value_model,
                                                    const struct GungiVPModel *black_policy_model,
                                                    const struct GungiVPModel *white_policy_model,
                                                    int depth,
                                                    const GungiHybridSearchConfig *config,
                                                    int *out_black_score);

Move gungi_get_vp_side_policy_hybrid_ai_move(const GameState *state,
                                             const struct GungiVPModel *value_model,
                                             const struct GungiVPModel *black_policy_model,
                                             const struct GungiVPModel *white_policy_model,
                                             int depth,
                                             const GungiHybridSearchConfig *config);

#endif
