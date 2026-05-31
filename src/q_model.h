#ifndef Q_MODEL_H
#define Q_MODEL_H

#include "ai_core.h"
#include "gungi_rules.h"

#define GUNGI_V_FEATURE_COUNT 8
#define GUNGI_V_DEFAULT_MODEL_PATH "models/v_weights.bin"

typedef enum GungiQProfile {
    GUNGI_Q_PROFILE_BALANCED = 0,
    GUNGI_Q_PROFILE_ATTACK = 1,
    GUNGI_Q_PROFILE_DEFENSE = 2
} GungiQProfile;

typedef struct GungiQProfileConfig {
    GungiQProfile profile;
    const char *name;
    float learning_rate;
    float gamma;
} GungiQProfileConfig;

typedef struct GungiQModel {
    float weights[GUNGI_V_FEATURE_COUNT];
    GungiQProfile profile;
} GungiQModel;

const GungiQProfileConfig *gungi_q_profile_config(GungiQProfile profile);
void gungi_q_init(GungiQModel *model);
int gungi_q_load(GungiQModel *model, const char *path);
int gungi_q_save(const GungiQModel *model, const char *path);

float gungi_v_evaluate(const GungiQModel *model, const GameState *state);
void gungi_v_update(GungiQModel *model, const GameState *state, float target, float learning_rate);
float gungi_v_score_move(const GungiQModel *model, const GameState *before, Move move, float gamma);
float gungi_v_immediate_reward(const GameState *before, Move move, const GameState *after, const RulesResult *result, int timeout);

Move gungi_get_q_move(const GameState *state, const GungiQModel *model, float epsilon);
int gungi_q_capture_value(const GameState *state, Move move);

#endif
