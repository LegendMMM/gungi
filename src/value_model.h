#ifndef VALUE_MODEL_H
#define VALUE_MODEL_H

#include "gungi_rules.h"

#define GUNGI_VALUE_FEATURE_COUNT 192
#define GUNGI_VALUE_DEFAULT_MODEL_PATH "models/value_weights.bin"
#define GUNGI_VALUE_MODEL_ENV "GUNGI_VALUE_MODEL"
#define GUNGI_VALUE_SCORE_SCALE 2000.0f

typedef struct GungiValueModel {
    float weights[GUNGI_VALUE_FEATURE_COUNT];
    int loaded;
} GungiValueModel;

void gungi_value_init(GungiValueModel *model);
int gungi_value_load(GungiValueModel *model, const char *path);
int gungi_value_save(const GungiValueModel *model, const char *path);
const char *gungi_value_resolve_model_path(void);
int gungi_value_model_loaded(const GungiValueModel *model);

void gungi_value_extract_features(const GameState *state, float features[GUNGI_VALUE_FEATURE_COUNT]);
float gungi_value_evaluate_raw(const GungiValueModel *model, const GameState *state);
int gungi_value_evaluate_board(const GungiValueModel *model, const GameState *state);

int gungi_value_piece_value(GungiPieceType type);

#endif
