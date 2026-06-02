#include "value_model.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char VALUE_MODEL_MAGIC[8] = { 'G', 'U', 'N', 'G', 'I', 'V', 'A', '2' };

static float clampf_local(float value, float min_value, float max_value)
{
    if (value < min_value) {
        return min_value;
    }
    if (value > max_value) {
        return max_value;
    }
    return value;
}

static void push_feature(float features[GUNGI_VALUE_FEATURE_COUNT], int *index, float value)
{
    if (*index < GUNGI_VALUE_FEATURE_COUNT) {
        features[*index] = value;
    }
    (*index)++;
}

int gungi_value_piece_value(GungiPieceType type)
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

static int find_top_marshal_local(const GameState *state, GungiPlayer player, int *out_x, int *out_y)
{
    int y;
    int x;

    if (state == NULL) {
        return 0;
    }

    for (y = 0; y < GUNGI_BOARD_SIZE; ++y) {
        for (x = 0; x < GUNGI_BOARD_SIZE; ++x) {
            Piece top = gungi_top_piece(state, x, y);
            if (top.owner == player && top.type == GUNGI_PIECE_MARSHAL) {
                if (out_x != NULL) {
                    *out_x = x;
                }
                if (out_y != NULL) {
                    *out_y = y;
                }
                return 1;
            }
        }
    }

    return 0;
}

static int player_attacks_enemy_marshal(const GameState *state, GungiPlayer player)
{
    int x = -1;
    int y = -1;
    GungiPlayer opponent = gungi_opponent(player);

    if (!find_top_marshal_local(state, opponent, &x, &y)) {
        return 0;
    }

    return gungi_is_square_attacked(state, x, y, player);
}

static int most_advanced_row_local(const GameState *state, GungiPlayer player)
{
    int y;
    int x;
    int level;
    int found = 0;
    int best = player == GUNGI_PLAYER_BLACK ? 0 : GUNGI_BOARD_SIZE - 1;

    if (state == NULL) {
        return best;
    }

    for (y = 0; y < GUNGI_BOARD_SIZE; ++y) {
        for (x = 0; x < GUNGI_BOARD_SIZE; ++x) {
            int height = gungi_cell_height(state, x, y);
            for (level = 0; level < height; ++level) {
                Piece piece = gungi_stack_piece(state, x, y, level);
                if (piece.owner == player) {
                    if (!found) {
                        best = y;
                        found = 1;
                    } else if (player == GUNGI_PLAYER_BLACK && y > best) {
                        best = y;
                    } else if (player == GUNGI_PLAYER_WHITE && y < best) {
                        best = y;
                    }
                }
            }
        }
    }

    return best;
}

void gungi_value_init(GungiValueModel *model)
{
    if (model == NULL) {
        return;
    }

    memset(model->weights, 0, sizeof(model->weights));
    model->loaded = 0;
}

int gungi_value_load(GungiValueModel *model, const char *path)
{
    FILE *file;
    char magic[8];
    int feature_count;

    if (model == NULL || path == NULL) {
        return 0;
    }

    file = fopen(path, "rb");
    if (file == NULL) {
        return 0;
    }

    if (fread(magic, sizeof(magic), 1, file) != 1 ||
        memcmp(magic, VALUE_MODEL_MAGIC, sizeof(magic)) != 0 ||
        fread(&feature_count, sizeof(feature_count), 1, file) != 1 ||
        feature_count != GUNGI_VALUE_FEATURE_COUNT ||
        fread(model->weights, sizeof(float), GUNGI_VALUE_FEATURE_COUNT, file) != GUNGI_VALUE_FEATURE_COUNT) {
        fclose(file);
        gungi_value_init(model);
        return 0;
    }

    model->loaded = 1;
    fclose(file);
    return 1;
}

int gungi_value_save(const GungiValueModel *model, const char *path)
{
    FILE *file;
    int feature_count = GUNGI_VALUE_FEATURE_COUNT;

    if (model == NULL || path == NULL) {
        return 0;
    }

    file = fopen(path, "wb");
    if (file == NULL) {
        return 0;
    }

    if (fwrite(VALUE_MODEL_MAGIC, sizeof(VALUE_MODEL_MAGIC), 1, file) != 1 ||
        fwrite(&feature_count, sizeof(feature_count), 1, file) != 1 ||
        fwrite(model->weights, sizeof(float), GUNGI_VALUE_FEATURE_COUNT, file) != GUNGI_VALUE_FEATURE_COUNT) {
        fclose(file);
        return 0;
    }

    fclose(file);
    return 1;
}

const char *gungi_value_resolve_model_path(void)
{
    const char *path = getenv(GUNGI_VALUE_MODEL_ENV);
    return (path != NULL && path[0] != '\0') ? path : GUNGI_VALUE_DEFAULT_MODEL_PATH;
}

int gungi_value_model_loaded(const GungiValueModel *model)
{
    return model != NULL && model->loaded;
}

void gungi_value_extract_features(const GameState *state, float features[GUNGI_VALUE_FEATURE_COUNT])
{
    int board_counts[2][GUNGI_PIECE_TYPE_COUNT];
    int hand_counts[2][GUNGI_PIECE_TYPE_COUNT];
    int top_counts[2][GUNGI_PIECE_TYPE_COUNT];
    int buried_counts[2][GUNGI_PIECE_TYPE_COUNT];
    int stack_heights[2][GUNGI_MAX_STACK];
    int material = 0;
    int top_material = 0;
    int board_totals[2] = { 0, 0 };
    int hand_totals[2] = { 0, 0 };
    int top_totals[2] = { 0, 0 };
    int index = 0;
    int y;
    int x;
    int level;
    int type;
    int marshal_x;
    int marshal_y;
    int repetition = 0;

    memset(features, 0, sizeof(float) * GUNGI_VALUE_FEATURE_COUNT);
    memset(board_counts, 0, sizeof(board_counts));
    memset(hand_counts, 0, sizeof(hand_counts));
    memset(top_counts, 0, sizeof(top_counts));
    memset(buried_counts, 0, sizeof(buried_counts));
    memset(stack_heights, 0, sizeof(stack_heights));

    if (state == NULL) {
        return;
    }

    for (y = 0; y < GUNGI_BOARD_SIZE; ++y) {
        for (x = 0; x < GUNGI_BOARD_SIZE; ++x) {
            int height = gungi_cell_height(state, x, y);
            for (level = 0; level < height; ++level) {
                Piece piece = gungi_stack_piece(state, x, y, level);
                int value = gungi_value_piece_value(piece.type);
                if (piece.owner == GUNGI_PLAYER_BLACK || piece.owner == GUNGI_PLAYER_WHITE) {
                    board_counts[piece.owner][piece.type]++;
                    board_totals[piece.owner]++;
                    material += piece.owner == GUNGI_PLAYER_BLACK ? value : -value;
                    if (level == height - 1) {
                        top_counts[piece.owner][piece.type]++;
                        top_totals[piece.owner]++;
                        top_material += piece.owner == GUNGI_PLAYER_BLACK ? value : -value;
                        if (height >= 1 && height <= GUNGI_MAX_STACK) {
                            stack_heights[piece.owner][height - 1]++;
                        }
                    } else {
                        buried_counts[piece.owner][piece.type]++;
                    }
                }
            }
        }
    }

    for (type = GUNGI_PIECE_NONE + 1; type < GUNGI_PIECE_TYPE_COUNT; ++type) {
        hand_counts[GUNGI_PLAYER_BLACK][type] = gungi_hand_count(state, GUNGI_PLAYER_BLACK, (GungiPieceType)type);
        hand_counts[GUNGI_PLAYER_WHITE][type] = gungi_hand_count(state, GUNGI_PLAYER_WHITE, (GungiPieceType)type);
        hand_totals[GUNGI_PLAYER_BLACK] += hand_counts[GUNGI_PLAYER_BLACK][type];
        hand_totals[GUNGI_PLAYER_WHITE] += hand_counts[GUNGI_PLAYER_WHITE][type];
        material += hand_counts[GUNGI_PLAYER_BLACK][type] * gungi_value_piece_value((GungiPieceType)type);
        material -= hand_counts[GUNGI_PLAYER_WHITE][type] * gungi_value_piece_value((GungiPieceType)type);
    }

    repetition = gungi_count_repetition(state, gungi_position_hash(state));

    push_feature(features, &index, 1.0f);
    push_feature(features, &index, state->current_player == GUNGI_PLAYER_BLACK ? 1.0f : -1.0f);
    push_feature(features, &index, clampf_local((float)(board_totals[GUNGI_PLAYER_BLACK] + board_totals[GUNGI_PLAYER_WHITE]) / 60.0f, 0.0f, 2.0f));
    push_feature(features, &index, clampf_local((float)(board_totals[GUNGI_PLAYER_BLACK] - board_totals[GUNGI_PLAYER_WHITE]) / 40.0f, -2.0f, 2.0f));
    push_feature(features, &index, clampf_local((float)(hand_totals[GUNGI_PLAYER_BLACK] + hand_totals[GUNGI_PLAYER_WHITE]) / 60.0f, 0.0f, 2.0f));
    push_feature(features, &index, clampf_local((float)(hand_totals[GUNGI_PLAYER_BLACK] - hand_totals[GUNGI_PLAYER_WHITE]) / 40.0f, -2.0f, 2.0f));
    push_feature(features, &index, clampf_local((float)(top_totals[GUNGI_PLAYER_BLACK] + top_totals[GUNGI_PLAYER_WHITE]) / 40.0f, 0.0f, 2.0f));
    push_feature(features, &index, clampf_local((float)(top_totals[GUNGI_PLAYER_BLACK] - top_totals[GUNGI_PLAYER_WHITE]) / 40.0f, -2.0f, 2.0f));
    push_feature(features, &index, clampf_local((float)repetition / (float)GUNGI_REPETITION_DRAW_COUNT, 0.0f, 2.0f));
    push_feature(features, &index, clampf_local((float)material / 20000.0f, -2.0f, 2.0f));
    push_feature(features, &index, clampf_local((float)top_material / 20000.0f, -2.0f, 2.0f));
    push_feature(features, &index, gungi_is_in_check(state, GUNGI_PLAYER_BLACK) ? 1.0f : 0.0f);
    push_feature(features, &index, gungi_is_in_check(state, GUNGI_PLAYER_WHITE) ? 1.0f : 0.0f);
    push_feature(features, &index, player_attacks_enemy_marshal(state, GUNGI_PLAYER_BLACK) ? 1.0f : 0.0f);
    push_feature(features, &index, player_attacks_enemy_marshal(state, GUNGI_PLAYER_WHITE) ? 1.0f : 0.0f);

    marshal_x = marshal_y = -1;
    if (find_top_marshal_local(state, GUNGI_PLAYER_BLACK, &marshal_x, &marshal_y)) {
        push_feature(features, &index, (float)marshal_x / (float)(GUNGI_BOARD_SIZE - 1));
        push_feature(features, &index, (float)marshal_y / (float)(GUNGI_BOARD_SIZE - 1));
    } else {
        push_feature(features, &index, 0.0f);
        push_feature(features, &index, 0.0f);
    }

    marshal_x = marshal_y = -1;
    if (find_top_marshal_local(state, GUNGI_PLAYER_WHITE, &marshal_x, &marshal_y)) {
        push_feature(features, &index, (float)marshal_x / (float)(GUNGI_BOARD_SIZE - 1));
        push_feature(features, &index, (float)marshal_y / (float)(GUNGI_BOARD_SIZE - 1));
    } else {
        push_feature(features, &index, 0.0f);
        push_feature(features, &index, 0.0f);
    }

    for (type = GUNGI_PIECE_NONE + 1; type < GUNGI_PIECE_TYPE_COUNT; ++type) {
        push_feature(features, &index, (float)board_counts[GUNGI_PLAYER_BLACK][type] / 10.0f);
        push_feature(features, &index, (float)board_counts[GUNGI_PLAYER_WHITE][type] / 10.0f);
        push_feature(features, &index, (float)(board_counts[GUNGI_PLAYER_BLACK][type] - board_counts[GUNGI_PLAYER_WHITE][type]) / 10.0f);
    }

    for (type = GUNGI_PIECE_NONE + 1; type < GUNGI_PIECE_TYPE_COUNT; ++type) {
        push_feature(features, &index, (float)hand_counts[GUNGI_PLAYER_BLACK][type] / 10.0f);
        push_feature(features, &index, (float)hand_counts[GUNGI_PLAYER_WHITE][type] / 10.0f);
        push_feature(features, &index, (float)(hand_counts[GUNGI_PLAYER_BLACK][type] - hand_counts[GUNGI_PLAYER_WHITE][type]) / 10.0f);
    }

    for (type = GUNGI_PIECE_NONE + 1; type < GUNGI_PIECE_TYPE_COUNT; ++type) {
        push_feature(features, &index, (float)top_counts[GUNGI_PLAYER_BLACK][type] / 10.0f);
        push_feature(features, &index, (float)top_counts[GUNGI_PLAYER_WHITE][type] / 10.0f);
    }

    for (type = GUNGI_PIECE_NONE + 1; type < GUNGI_PIECE_TYPE_COUNT; ++type) {
        push_feature(features, &index, (float)buried_counts[GUNGI_PLAYER_BLACK][type] / 10.0f);
        push_feature(features, &index, (float)buried_counts[GUNGI_PLAYER_WHITE][type] / 10.0f);
    }

    for (level = 0; level < GUNGI_MAX_STACK; ++level) {
        push_feature(features, &index, (float)stack_heights[GUNGI_PLAYER_BLACK][level] / 20.0f);
        push_feature(features, &index, (float)stack_heights[GUNGI_PLAYER_WHITE][level] / 20.0f);
    }

    push_feature(features, &index, (float)most_advanced_row_local(state, GUNGI_PLAYER_BLACK) / (float)(GUNGI_BOARD_SIZE - 1));
    push_feature(features, &index, (float)(GUNGI_BOARD_SIZE - 1 - most_advanced_row_local(state, GUNGI_PLAYER_WHITE)) / (float)(GUNGI_BOARD_SIZE - 1));
}

float gungi_value_evaluate_raw(const GungiValueModel *model, const GameState *state)
{
    float features[GUNGI_VALUE_FEATURE_COUNT];
    float score = 0.0f;
    int i;

    if (model == NULL || state == NULL) {
        return 0.0f;
    }

    gungi_value_extract_features(state, features);
    for (i = 0; i < GUNGI_VALUE_FEATURE_COUNT; ++i) {
        score += model->weights[i] * features[i];
    }

    return (float)tanh((double)score);
}

int gungi_value_evaluate_board(const GungiValueModel *model, const GameState *state)
{
    float raw = gungi_value_evaluate_raw(model, state);
    return (int)(raw * GUNGI_VALUE_SCORE_SCALE);
}
