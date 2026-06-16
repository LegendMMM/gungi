#ifndef VP_MODEL_H
#define VP_MODEL_H

#include "q_model.h"

#define GUNGI_P_FEATURE_COUNT 47
#define GUNGI_VP_DEFAULT_MODEL_PATH "models/vp_hybrid_student_100000.bin"

typedef struct GungiVPModel {
    float value_weights[GUNGI_V_FEATURE_COUNT];
    float policy_weights[GUNGI_P_FEATURE_COUNT];
    GungiQProfile profile;
} GungiVPModel;

void gungi_vp_init(GungiVPModel *model);
int gungi_vp_load(GungiVPModel *model, const char *path);
int gungi_vp_save(const GungiVPModel *model, const char *path);
int gungi_vp_weights_are_finite(const GungiVPModel *model);

float gungi_vp_value_evaluate(const GungiVPModel *model, const GameState *state);
void gungi_vp_value_update(GungiVPModel *model, const GameState *state, float target, float learning_rate);

void gungi_vp_extract_policy_features(float features[GUNGI_P_FEATURE_COUNT],
                                      const GameState *state,
                                      Move move);
float gungi_p_score_move(const GungiVPModel *model, const GameState *state, Move move);
Move gungi_get_vp_move(const GameState *state, const GungiVPModel *model, float epsilon);

#endif
