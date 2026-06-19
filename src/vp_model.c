#include "vp_model.h"

#include <float.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define POLICY_GENERATED_MOVE_LIMIT 512
#define POLICY_CANDIDATE_LIMIT 64

static const char VP_MODEL_MAGIC[8] = { 'G', 'U', 'N', 'G', 'I', 'P', '4', '\0' };
static const char V3_MODEL_MAGIC[8] = { 'G', 'U', 'N', 'G', 'I', 'V', '3', '\0' };

static int in_bounds(int x, int y)
{
    return x >= 0 && x < GUNGI_BOARD_SIZE && y >= 0 && y < GUNGI_BOARD_SIZE;
}

static int find_top_marshal(const GameState *state, GungiPlayer player, int *out_x, int *out_y)
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
    GungiPlayer opponent = gungi_opponent(player);
    int marshal_x = -1;
    int marshal_y = -1;

    if (!find_top_marshal(state, opponent, &marshal_x, &marshal_y)) {
        return 0;
    }

    return gungi_is_square_attacked(state, marshal_x, marshal_y, player);
}

static float coord_norm(int value)
{
    if (value < 0 || value >= GUNGI_BOARD_SIZE) {
        return -1.0f;
    }
    return (float)value / (float)(GUNGI_BOARD_SIZE - 1);
}

static float row_norm_for_player(int y, GungiPlayer player)
{
    if (y < 0 || y >= GUNGI_BOARD_SIZE) {
        return -1.0f;
    }
    if (player == GUNGI_PLAYER_WHITE) {
        y = GUNGI_BOARD_SIZE - 1 - y;
    }
    return (float)y / (float)(GUNGI_BOARD_SIZE - 1);
}

static int capture_bucket(int capture_value)
{
    if (capture_value <= 0) {
        return 0;
    }
    if (capture_value <= 200) {
        return 1;
    }
    if (capture_value <= 500) {
        return 2;
    }
    if (capture_value <= 900) {
        return 3;
    }
    return 4;
}

static int material_delta_bucket(int delta)
{
    if (delta <= -1000) {
        return 0;
    }
    if (delta <= -301) {
        return 1;
    }
    if (delta <= -1) {
        return 2;
    }
    if (delta == 0) {
        return 3;
    }
    if (delta <= 300) {
        return 4;
    }
    if (delta <= 999) {
        return 5;
    }
    return 6;
}

static GungiPieceType moving_piece_type(const GameState *state, Move move)
{
    if (move.kind == GUNGI_MOVE_DROP) {
        return move.drop_type;
    }
    if (move.kind == GUNGI_MOVE_NORMAL && state != NULL && in_bounds(move.from_x, move.from_y) &&
        gungi_cell_height(state, move.from_x, move.from_y) > 0) {
        return gungi_top_piece(state, move.from_x, move.from_y).type;
    }
    return GUNGI_PIECE_NONE;
}

static float vp_value_from_weights(const float weights[GUNGI_V_FEATURE_COUNT], const GameState *state)
{
    float features[GUNGI_V_FEATURE_COUNT];
    float score = 0.0f;
    int i;

    if (weights == NULL || state == NULL || state->status != GUNGI_STATUS_ONGOING) {
        return 0.0f;
    }

    gungi_v_extract_features(features, state);
    for (i = 0; i < GUNGI_V_FEATURE_COUNT; ++i) {
        score += weights[i] * features[i];
    }
    return score;
}

void gungi_vp_init(GungiVPModel *model)
{
    if (model == NULL) {
        return;
    }

    memset(model->value_weights, 0, sizeof(model->value_weights));
    memset(model->policy_weights, 0, sizeof(model->policy_weights));
    model->profile = GUNGI_Q_PROFILE_BALANCED;
}

int gungi_vp_load(GungiVPModel *model, const char *path)
{
    FILE *file;
    char magic[8];
    int value_feature_count;
    int policy_feature_count;

    if (model == NULL || path == NULL) {
        return 0;
    }

    file = fopen(path, "rb");
    if (file == NULL) {
        return 0;
    }

    gungi_vp_init(model);
    if (fread(magic, sizeof(magic), 1, file) != 1) {
        fclose(file);
        return 0;
    }

    if (memcmp(magic, VP_MODEL_MAGIC, sizeof(magic)) == 0) {
        if (fread(&value_feature_count, sizeof(value_feature_count), 1, file) != 1 ||
            fread(&policy_feature_count, sizeof(policy_feature_count), 1, file) != 1 ||
            value_feature_count != GUNGI_V_FEATURE_COUNT ||
            policy_feature_count != GUNGI_P_FEATURE_COUNT ||
            fread(model->value_weights, sizeof(float), GUNGI_V_FEATURE_COUNT, file) != GUNGI_V_FEATURE_COUNT ||
            fread(model->policy_weights, sizeof(float), GUNGI_P_FEATURE_COUNT, file) != GUNGI_P_FEATURE_COUNT) {
            fclose(file);
            return 0;
        }
        fclose(file);
        return 1;
    }

    if (memcmp(magic, V3_MODEL_MAGIC, sizeof(magic)) == 0) {
        if (fread(&value_feature_count, sizeof(value_feature_count), 1, file) != 1 ||
            value_feature_count != GUNGI_V_FEATURE_COUNT ||
            fread(model->value_weights, sizeof(float), GUNGI_V_FEATURE_COUNT, file) != GUNGI_V_FEATURE_COUNT) {
            fclose(file);
            return 0;
        }
        memset(model->policy_weights, 0, sizeof(model->policy_weights));
        fclose(file);
        return 1;
    }

    fclose(file);
    return 0;
}

int gungi_vp_save(const GungiVPModel *model, const char *path)
{
    FILE *file;
    int value_feature_count = GUNGI_V_FEATURE_COUNT;
    int policy_feature_count = GUNGI_P_FEATURE_COUNT;

    if (model == NULL || path == NULL) {
        return 0;
    }

    file = fopen(path, "wb");
    if (file == NULL) {
        return 0;
    }

    if (fwrite(VP_MODEL_MAGIC, sizeof(VP_MODEL_MAGIC), 1, file) != 1 ||
        fwrite(&value_feature_count, sizeof(value_feature_count), 1, file) != 1 ||
        fwrite(&policy_feature_count, sizeof(policy_feature_count), 1, file) != 1 ||
        fwrite(model->value_weights, sizeof(float), GUNGI_V_FEATURE_COUNT, file) != GUNGI_V_FEATURE_COUNT ||
        fwrite(model->policy_weights, sizeof(float), GUNGI_P_FEATURE_COUNT, file) != GUNGI_P_FEATURE_COUNT) {
        fclose(file);
        return 0;
    }

    fclose(file);
    return 1;
}

int gungi_vp_weights_are_finite(const GungiVPModel *model)
{
    int i;

    if (model == NULL) {
        return 0;
    }

    for (i = 0; i < GUNGI_V_FEATURE_COUNT; ++i) {
        if (!isfinite(model->value_weights[i])) {
            return 0;
        }
    }
    for (i = 0; i < GUNGI_P_FEATURE_COUNT; ++i) {
        if (!isfinite(model->policy_weights[i])) {
            return 0;
        }
    }
    return 1;
}

float gungi_vp_value_evaluate(const GungiVPModel *model, const GameState *state)
{
    if (model == NULL) {
        return 0.0f;
    }
    return vp_value_from_weights(model->value_weights, state);
}

void gungi_vp_value_update(GungiVPModel *model, const GameState *state, float target, float learning_rate)
{
    float features[GUNGI_V_FEATURE_COUNT];
    float prediction = 0.0f;
    float error;
    int i;

    if (model == NULL || state == NULL || state->status != GUNGI_STATUS_ONGOING) {
        return;
    }

    gungi_v_extract_features(features, state);
    for (i = 0; i < GUNGI_V_FEATURE_COUNT; ++i) {
        prediction += model->value_weights[i] * features[i];
    }

    error = target - prediction;
    for (i = 0; i < GUNGI_V_FEATURE_COUNT; ++i) {
        model->value_weights[i] += learning_rate * error * features[i];
        if (!isfinite(model->value_weights[i])) {
            model->value_weights[i] = 0.0f;
        }
    }
}

void gungi_vp_extract_policy_features(float features[GUNGI_P_FEATURE_COUNT],
                                      const GameState *state,
                                      Move move)
{
    GameState after;
    RulesResult result;
    GungiPieceType piece_type;
    int before_material = 0;
    int after_material = 0;
    int material_delta = 0;
    int reps_after = 0;
    int capture_value = 0;
    int dest_height = 0;
    int bucket;
    int index = 0;
    int type;

    if (features == NULL) {
        return;
    }

    memset(features, 0, sizeof(float) * GUNGI_P_FEATURE_COUNT);
    if (state == NULL) {
        return;
    }

    features[index++] = 1.0f;

    if (move.kind >= GUNGI_MOVE_NORMAL && move.kind <= GUNGI_MOVE_RESIGN) {
        features[index + move.kind] = 1.0f;
    }
    index += 3;

    if (move.intent == GUNGI_INTENT_STACK) {
        features[index + 1] = 1.0f;
    } else if (move.intent == GUNGI_INTENT_CAPTURE) {
        features[index + 2] = 1.0f;
    } else {
        features[index] = 1.0f;
    }
    index += 3;

    piece_type = moving_piece_type(state, move);
    for (type = GUNGI_PIECE_NONE + 1; type < GUNGI_PIECE_TYPE_COUNT; ++type) {
        if (piece_type == (GungiPieceType)type) {
            features[index] = 1.0f;
        }
        index++;
    }

    features[index++] = coord_norm(move.from_x);
    features[index++] = row_norm_for_player(move.from_y, move.player);
    features[index++] = coord_norm(move.to_x);
    features[index++] = row_norm_for_player(move.to_y, move.player);

    if (in_bounds(move.from_x, move.from_y) && in_bounds(move.to_x, move.to_y)) {
        features[index++] = (float)(move.to_x - move.from_x) / (float)(GUNGI_BOARD_SIZE - 1);
        features[index++] = move.player == GUNGI_PLAYER_WHITE
                                 ? (float)(move.from_y - move.to_y) / (float)(GUNGI_BOARD_SIZE - 1)
                                 : (float)(move.to_y - move.from_y) / (float)(GUNGI_BOARD_SIZE - 1);
    } else {
        features[index++] = 0.0f;
        features[index++] = 0.0f;
    }

    if (in_bounds(move.to_x, move.to_y)) {
        dest_height = gungi_cell_height(state, move.to_x, move.to_y);
    }
    features[index++] = (float)dest_height / (float)GUNGI_MAX_STACK;

    capture_value = gungi_q_capture_value(state, move);
    bucket = capture_bucket(capture_value);
    features[index + bucket] = 1.0f;
    index += 5;

    after = *state;
    result = gungi_apply_move(&after, move);
    if (result.ok) {
        features[index] = result.gives_check ? 1.0f : 0.0f;
        features[index + 1] =
            (result.captured_marshal || player_attacks_enemy_marshal(&after, move.player)) ? 1.0f : 0.0f;
        reps_after = gungi_count_repetition(&after, gungi_position_hash(&after));
        if (reps_after > GUNGI_REPETITION_DRAW_COUNT) {
            reps_after = GUNGI_REPETITION_DRAW_COUNT;
        }
        features[index + 2] = (float)reps_after / (float)GUNGI_REPETITION_DRAW_COUNT;

        before_material = gungi_evaluate_board(state);
        after_material = gungi_evaluate_board(&after);
        material_delta = after_material - before_material;
        if (move.player == GUNGI_PLAYER_WHITE) {
            material_delta = -material_delta;
        }
    }
    index += 3;

    bucket = material_delta_bucket(material_delta);
    features[index + bucket] = 1.0f;
    index += 7;

    if (move.betray_mask >= 0 && move.betray_mask <= 3) {
        features[index + move.betray_mask] = 1.0f;
    }
}

float gungi_p_score_move(const GungiVPModel *model, const GameState *state, Move move)
{
    float features[GUNGI_P_FEATURE_COUNT];
    float score = 0.0f;
    int i;

    if (model == NULL || state == NULL) {
        return -FLT_MAX;
    }

    gungi_vp_extract_policy_features(features, state, move);
    for (i = 0; i < GUNGI_P_FEATURE_COUNT; ++i) {
        score += model->policy_weights[i] * features[i];
    }
    return score;
}

Move gungi_get_vp_move(const GameState *state, const GungiVPModel *model, float epsilon)
{
    Move moves[GUNGI_MAX_LEGAL_MOVES];
    int move_limit = epsilon > 0.0f ? POLICY_GENERATED_MOVE_LIMIT : GUNGI_MAX_LEGAL_MOVES;
    int count = gungi_generate_legal_moves(state, moves, move_limit);
    int score_count = count;
    int best_index = 0;
    float best_score = -FLT_MAX;
    int i;

    if (state == NULL || count == 0) {
        return gungi_make_resign(state != NULL ? state->current_player : GUNGI_PLAYER_BLACK);
    }

    if (model == NULL || (epsilon > 0.0f && ((float)rand() / (float)RAND_MAX) < epsilon)) {
        return moves[rand() % count];
    }

    if (epsilon > 0.0f && score_count > POLICY_CANDIDATE_LIMIT) {
        score_count = POLICY_CANDIDATE_LIMIT;
        for (i = 0; i < score_count; ++i) {
            int j = i + rand() % (count - i);
            Move temp = moves[i];
            moves[i] = moves[j];
            moves[j] = temp;
        }
    }

    for (i = 0; i < score_count; ++i) {
        float score = gungi_p_score_move(model, state, moves[i]);
        if (score > best_score) {
            best_score = score;
            best_index = i;
        }
    }

    return moves[best_index];
}
