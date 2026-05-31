#include "q_model.h"

#include <float.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MOBILITY_SAMPLE_LIMIT 200
#define REWARD_MOBILITY_SAMPLE_LIMIT 80

static const char MODEL_MAGIC[8] = { 'G', 'U', 'N', 'G', 'I', 'V', '1', '\0' };

static const GungiQProfileConfig Q_PROFILES[] = {
    { GUNGI_Q_PROFILE_BALANCED, "balanced-value", 0.01f, 0.95f },
    { GUNGI_Q_PROFILE_ATTACK, "attack-value", 0.01f, 0.95f },
    { GUNGI_Q_PROFILE_DEFENSE, "defense-value", 0.01f, 0.95f }
};

const GungiQProfileConfig *gungi_q_profile_config(GungiQProfile profile)
{
    size_t i;

    for (i = 0; i < sizeof(Q_PROFILES) / sizeof(Q_PROFILES[0]); ++i) {
        if (Q_PROFILES[i].profile == profile) {
            return &Q_PROFILES[i];
        }
    }

    return &Q_PROFILES[0];
}

void gungi_q_init(GungiQModel *model)
{
    if (model == NULL) {
        return;
    }

    memset(model->weights, 0, sizeof(model->weights));
    model->profile = GUNGI_Q_PROFILE_BALANCED;
}

int gungi_q_load(GungiQModel *model, const char *path)
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
        memcmp(magic, MODEL_MAGIC, sizeof(magic)) != 0 ||
        fread(&feature_count, sizeof(feature_count), 1, file) != 1 ||
        feature_count != GUNGI_V_FEATURE_COUNT ||
        fread(model->weights, sizeof(float), GUNGI_V_FEATURE_COUNT, file) != GUNGI_V_FEATURE_COUNT) {
        fclose(file);
        return 0;
    }

    model->profile = GUNGI_Q_PROFILE_BALANCED;
    fclose(file);
    return 1;
}

int gungi_q_save(const GungiQModel *model, const char *path)
{
    FILE *file;
    int feature_count = GUNGI_V_FEATURE_COUNT;

    if (model == NULL || path == NULL) {
        return 0;
    }

    file = fopen(path, "wb");
    if (file == NULL) {
        return 0;
    }

    if (fwrite(MODEL_MAGIC, sizeof(MODEL_MAGIC), 1, file) != 1 ||
        fwrite(&feature_count, sizeof(feature_count), 1, file) != 1 ||
        fwrite(model->weights, sizeof(float), GUNGI_V_FEATURE_COUNT, file) != GUNGI_V_FEATURE_COUNT) {
        fclose(file);
        return 0;
    }

    fclose(file);
    return 1;
}

static float clamp_float(float value, float min_value, float max_value)
{
    if (value < min_value) {
        return min_value;
    }
    if (value > max_value) {
        return max_value;
    }
    return value;
}

static int abs_int(int value)
{
    return value < 0 ? -value : value;
}

static int manhattan_distance(int ax, int ay, int bx, int by)
{
    return abs_int(ax - bx) + abs_int(ay - by);
}

static int in_bounds(int x, int y)
{
    return x >= 0 && x < GUNGI_BOARD_SIZE && y >= 0 && y < GUNGI_BOARD_SIZE;
}

static int find_top_marshal_local(const GameState *state, GungiPlayer player, int *out_x, int *out_y)
{
    int y;
    int x;

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

static int material_for_player(const GameState *state, GungiPlayer player)
{
    int total = 0;
    int y;
    int x;
    int level;
    int type;

    for (y = 0; y < GUNGI_BOARD_SIZE; ++y) {
        for (x = 0; x < GUNGI_BOARD_SIZE; ++x) {
            int height = gungi_cell_height(state, x, y);
            for (level = 0; level < height; ++level) {
                Piece piece = gungi_stack_piece(state, x, y, level);
                if (piece.owner == player) {
                    total += gungi_piece_value(piece.type);
                }
            }
        }
    }

    for (type = GUNGI_PIECE_NONE + 1; type < GUNGI_PIECE_TYPE_COUNT; ++type) {
        total += gungi_hand_count(state, player, (GungiPieceType)type) *
                 (gungi_piece_value((GungiPieceType)type) * 11 / 10);
    }

    return total;
}

static int mobility_for_player_limited(const GameState *state, GungiPlayer player, int limit)
{
    GameState copy;
    Move moves[MOBILITY_SAMPLE_LIMIT];
    int capped_limit;

    if (state == NULL || state->status != GUNGI_STATUS_ONGOING) {
        return 0;
    }

    capped_limit = limit;
    if (capped_limit < 1) {
        capped_limit = 1;
    } else if (capped_limit > MOBILITY_SAMPLE_LIMIT) {
        capped_limit = MOBILITY_SAMPLE_LIMIT;
    }

    copy = *state;
    copy.current_player = player;
    return gungi_generate_legal_moves(&copy, moves, capped_limit);
}

static int mobility_for_player(const GameState *state, GungiPlayer player)
{
    return mobility_for_player_limited(state, player, MOBILITY_SAMPLE_LIMIT);
}

int gungi_q_capture_value(const GameState *state, Move move)
{
    int total = 0;
    int height;
    int level;

    if (state == NULL || move.kind != GUNGI_MOVE_NORMAL || move.intent != GUNGI_INTENT_CAPTURE) {
        return 0;
    }
    if (!in_bounds(move.to_x, move.to_y)) {
        return 0;
    }

    height = gungi_cell_height(state, move.to_x, move.to_y);
    for (level = 0; level < height; ++level) {
        Piece piece = gungi_stack_piece(state, move.to_x, move.to_y, level);
        if (piece.owner == gungi_opponent(move.player)) {
            total += gungi_piece_value(piece.type);
        }
    }

    return total;
}

static float enemy_marshal_distance_score(const GameState *state, GungiPlayer player, int marshal_x, int marshal_y)
{
    int best_distance = 16;
    int found = 0;
    int y;
    int x;

    for (y = 0; y < GUNGI_BOARD_SIZE; ++y) {
        for (x = 0; x < GUNGI_BOARD_SIZE; ++x) {
            if (gungi_cell_height(state, x, y) > 0) {
                Piece top = gungi_top_piece(state, x, y);
                if (top.owner == player) {
                    int distance = manhattan_distance(x, y, marshal_x, marshal_y);
                    if (!found || distance < best_distance) {
                        best_distance = distance;
                        found = 1;
                    }
                }
            }
        }
    }

    if (!found) {
        return 0.0f;
    }

    return clamp_float((16.0f - (float)best_distance) / 16.0f, 0.0f, 1.0f);
}

static void add_top_three(float scores[3], float value)
{
    int i;

    for (i = 0; i < 3; ++i) {
        if (value > scores[i]) {
            int j;
            for (j = 2; j > i; --j) {
                scores[j] = scores[j - 1];
            }
            scores[i] = value;
            return;
        }
    }
}

static float enemy_marshal_pressure(const GameState *state, GungiPlayer player, int marshal_x, int marshal_y)
{
    float pressure = 0.0f;
    int adjacent_control = 0;
    float near_scores[3] = { 0.0f, 0.0f, 0.0f };
    int dy;
    int dx;
    int y;
    int x;

    if (gungi_is_square_attacked(state, marshal_x, marshal_y, player)) {
        pressure += 1.0f;
    }

    for (dy = -1; dy <= 1; ++dy) {
        for (dx = -1; dx <= 1; ++dx) {
            int tx = marshal_x + dx;
            int ty = marshal_y + dy;
            if ((dx != 0 || dy != 0) && in_bounds(tx, ty) &&
                gungi_is_square_attacked(state, tx, ty, player)) {
                adjacent_control++;
            }
        }
    }
    pressure += (float)adjacent_control * 0.15f;

    for (y = 0; y < GUNGI_BOARD_SIZE; ++y) {
        for (x = 0; x < GUNGI_BOARD_SIZE; ++x) {
            if (gungi_cell_height(state, x, y) > 0) {
                Piece top = gungi_top_piece(state, x, y);
                if (top.owner == player) {
                    int distance = manhattan_distance(x, y, marshal_x, marshal_y);
                    float score = (float)(4 - distance) / 4.0f;
                    if (score > 0.0f) {
                        add_top_three(near_scores, score);
                    }
                }
            }
        }
    }
    pressure += (near_scores[0] + near_scores[1] + near_scores[2]) * 0.10f;

    return pressure;
}

static void attack_progress_for_player(const GameState *state, GungiPlayer player, float *pressure, float *distance_score)
{
    GungiPlayer opponent;
    int marshal_x = -1;
    int marshal_y = -1;

    if (pressure != NULL) {
        *pressure = 0.0f;
    }
    if (distance_score != NULL) {
        *distance_score = 0.0f;
    }
    if (state == NULL) {
        return;
    }

    opponent = gungi_opponent(player);
    if (!find_top_marshal_local(state, opponent, &marshal_x, &marshal_y)) {
        return;
    }

    if (pressure != NULL) {
        *pressure = enemy_marshal_pressure(state, player, marshal_x, marshal_y);
    }
    if (distance_score != NULL) {
        *distance_score = enemy_marshal_distance_score(state, player, marshal_x, marshal_y);
    }
}

static float own_marshal_pressure_for_player(const GameState *state, GungiPlayer player)
{
    GungiPlayer opponent = gungi_opponent(player);
    int marshal_x = -1;
    int marshal_y = -1;

    if (state == NULL || !find_top_marshal_local(state, player, &marshal_x, &marshal_y)) {
        return 0.0f;
    }

    return enemy_marshal_pressure(state, opponent, marshal_x, marshal_y);
}

static int own_marshal_directly_attacked(const GameState *state, GungiPlayer player)
{
    GungiPlayer opponent = gungi_opponent(player);
    int marshal_x = -1;
    int marshal_y = -1;

    if (state == NULL || !find_top_marshal_local(state, player, &marshal_x, &marshal_y)) {
        return 0;
    }

    return gungi_is_square_attacked(state, marshal_x, marshal_y, opponent);
}

static int player_has_immediate_win(const GameState *state, GungiPlayer player)
{
    GameState turn_state;
    Move replies[GUNGI_MAX_LEGAL_MOVES];
    int count;
    int i;

    if (state == NULL || state->status != GUNGI_STATUS_ONGOING) {
        return 0;
    }

    turn_state = *state;
    turn_state.current_player = player;
    count = gungi_generate_legal_moves(&turn_state, replies, GUNGI_MAX_LEGAL_MOVES);
    for (i = 0; i < count; ++i) {
        GameState trial = turn_state;
        RulesResult result = gungi_apply_move(&trial, replies[i]);
        if (result.ok &&
            (trial.status == GUNGI_STATUS_BLACK_WIN ||
             trial.status == GUNGI_STATUS_WHITE_WIN ||
             trial.status == GUNGI_STATUS_RESIGNED) &&
            trial.winner == player) {
            return 1;
        }
    }

    return 0;
}

static void extract_value_features(float features[GUNGI_V_FEATURE_COUNT], const GameState *state)
{
    GungiPlayer player = state->current_player;
    GungiPlayer opponent = gungi_opponent(player);
    int own_material = material_for_player(state, player);
    int opp_material = material_for_player(state, opponent);
    int marshal_x = -1;
    int marshal_y = -1;
    int reps;

    memset(features, 0, sizeof(float) * GUNGI_V_FEATURE_COUNT);

    features[0] = 1.0f;
    features[1] = (float)(own_material - opp_material) / 10000.0f;

    if (find_top_marshal_local(state, opponent, &marshal_x, &marshal_y)) {
        features[2] = enemy_marshal_pressure(state, player, marshal_x, marshal_y);
        features[3] = enemy_marshal_distance_score(state, player, marshal_x, marshal_y);
    }

    features[4] = (float)mobility_for_player(state, player) / 200.0f;
    features[5] = (float)mobility_for_player(state, opponent) / 200.0f;

    reps = gungi_count_repetition(state, gungi_position_hash(state));
    features[6] = reps > 1 ? (float)-(reps - 1) : 0.0f;
    features[7] = -clamp_float((float)state->ply_count / 600.0f, 0.0f, 1.0f);
}

float gungi_v_evaluate(const GungiQModel *model, const GameState *state)
{
    float features[GUNGI_V_FEATURE_COUNT];
    float score = 0.0f;
    int i;

    if (model == NULL || state == NULL || state->status != GUNGI_STATUS_ONGOING) {
        return 0.0f;
    }

    extract_value_features(features, state);
    for (i = 0; i < GUNGI_V_FEATURE_COUNT; ++i) {
        score += model->weights[i] * features[i];
    }

    return score;
}

void gungi_v_update(GungiQModel *model, const GameState *state, float target, float learning_rate)
{
    float features[GUNGI_V_FEATURE_COUNT];
    float prediction = 0.0f;
    float error;
    int i;

    if (model == NULL || state == NULL || state->status != GUNGI_STATUS_ONGOING) {
        return;
    }

    extract_value_features(features, state);
    for (i = 0; i < GUNGI_V_FEATURE_COUNT; ++i) {
        prediction += model->weights[i] * features[i];
    }

    error = target - prediction;
    for (i = 0; i < GUNGI_V_FEATURE_COUNT; ++i) {
        model->weights[i] += learning_rate * error * features[i];
        if (!isfinite(model->weights[i])) {
            model->weights[i] = 0.0f;
        }
    }
}

float gungi_v_immediate_reward(const GameState *before, Move move, const GameState *after, const RulesResult *result, int timeout)
{
    float reward = -0.003f;
    int capture_value = gungi_q_capture_value(before, move);
    float pressure_before = 0.0f;
    float pressure_after = 0.0f;
    float distance_before = 0.0f;
    float distance_after = 0.0f;
    float pressure_delta = 0.0f;
    float distance_delta = 0.0f;
    int enemy_mobility_before = 0;
    int enemy_mobility_after = 0;
    int own_mobility_before = 0;
    int own_mobility_after = 0;
    float enemy_mobility_delta = 0.0f;
    float own_mobility_delta = 0.0f;
    float own_pressure_before = 0.0f;
    float own_pressure_after = 0.0f;
    float own_pressure_delta = 0.0f;
    int own_direct_before = 0;
    int own_direct_after = 0;
    int was_under_threat = 0;
    int made_progress = 0;
    int repetition_count = 0;
    RulesResult local_result;

    if (capture_value > 0) {
        reward += (float)capture_value / 8000.0f;
    }

    if (result == NULL) {
        local_result = gungi_validate_move(before, move);
        result = &local_result;
    }

    if (result->gives_check) {
        reward += 0.06f;
    }

    if (before != NULL && after != NULL) {
        GungiPlayer mover = move.player;
        GungiPlayer opponent = gungi_opponent(mover);
        float ply_ratio = clamp_float((float)before->ply_count / 600.0f, 0.0f, 1.0f);

        reward -= 0.004f * ply_ratio;
        reward -= 0.012f * ply_ratio * ply_ratio;

        attack_progress_for_player(before, mover, &pressure_before, &distance_before);
        attack_progress_for_player(after, mover, &pressure_after, &distance_after);
        pressure_delta = pressure_after - pressure_before;
        distance_delta = distance_after - distance_before;

        enemy_mobility_before = mobility_for_player_limited(before, opponent, REWARD_MOBILITY_SAMPLE_LIMIT);
        enemy_mobility_after = mobility_for_player_limited(after, opponent, REWARD_MOBILITY_SAMPLE_LIMIT);
        own_mobility_before = mobility_for_player_limited(before, mover, REWARD_MOBILITY_SAMPLE_LIMIT);
        own_mobility_after = mobility_for_player_limited(after, mover, REWARD_MOBILITY_SAMPLE_LIMIT);
        enemy_mobility_delta = (float)(enemy_mobility_before - enemy_mobility_after) /
                               (float)REWARD_MOBILITY_SAMPLE_LIMIT;
        own_mobility_delta = (float)(own_mobility_after - own_mobility_before) /
                             (float)REWARD_MOBILITY_SAMPLE_LIMIT;

        reward += 0.12f * pressure_delta;
        reward += 0.08f * distance_delta;
        reward += 0.08f * enemy_mobility_delta;
        reward += 0.01f * own_mobility_delta;

        own_pressure_before = own_marshal_pressure_for_player(before, mover);
        own_pressure_after = own_marshal_pressure_for_player(after, mover);
        own_pressure_delta = own_pressure_after - own_pressure_before;
        own_direct_before = own_marshal_directly_attacked(before, mover);
        own_direct_after = own_marshal_directly_attacked(after, mover);
        was_under_threat = own_direct_before || own_pressure_before >= 0.75f;

        if (own_pressure_delta > 0.02f) {
            reward -= 0.24f * own_pressure_delta;
        } else if (was_under_threat && own_pressure_delta < -0.05f) {
            reward += 0.08f * -own_pressure_delta;
        }

        if (own_direct_after) {
            reward -= 0.45f;
        }

        if (own_direct_before && !own_direct_after) {
            reward += 0.18f;
        }

        if (after->status == GUNGI_STATUS_ONGOING &&
            player_has_immediate_win(after, opponent)) {
            reward -= 2.0f;
        }

        repetition_count = gungi_count_repetition(after, gungi_position_hash(after));
        if (repetition_count > 1) {
            reward -= 0.15f * (float)(repetition_count - 1);
            made_progress = 0;
        }

        if (capture_value > 0 || result->gives_check ||
            pressure_delta > 0.01f || distance_delta > 0.01f || enemy_mobility_delta > 0.01f) {
            made_progress = 1;
        }

        if (!made_progress) {
            reward -= 0.02f;
            if (reward > -0.006f) {
                reward = -0.006f;
            }
        }
    }

    if (after != NULL) {
        if (after->status == GUNGI_STATUS_BLACK_WIN ||
            after->status == GUNGI_STATUS_WHITE_WIN ||
            after->status == GUNGI_STATUS_RESIGNED) {
            reward += after->winner == move.player ? 1.5f : -1.5f;
        } else if (after->status == GUNGI_STATUS_DRAW) {
            reward -= 0.8f;
        }
    }

    if (timeout) {
        reward -= 1.0f;
    }

    return reward;
}

float gungi_v_score_move(const GungiQModel *model, const GameState *before, Move move, float gamma)
{
    GameState after;
    RulesResult result;
    float reward;

    if (model == NULL || before == NULL) {
        return -FLT_MAX;
    }

    after = *before;
    result = gungi_apply_move(&after, move);
    if (!result.ok) {
        return -FLT_MAX;
    }

    reward = gungi_v_immediate_reward(before, move, &after, &result, 0);
    if (after.status != GUNGI_STATUS_ONGOING) {
        return reward;
    }

    return reward - gamma * gungi_v_evaluate(model, &after);
}

Move gungi_get_q_move(const GameState *state, const GungiQModel *model, float epsilon)
{
    Move moves[GUNGI_MAX_LEGAL_MOVES];
    int count = gungi_generate_legal_moves(state, moves, GUNGI_MAX_LEGAL_MOVES);
    int best_index = 0;
    float best_score = -FLT_MAX;
    const GungiQProfileConfig *config;
    int i;

    if (state == NULL || count == 0) {
        return gungi_make_resign(state != NULL ? state->current_player : GUNGI_PLAYER_BLACK);
    }

    if (model == NULL || (epsilon > 0.0f && ((float)rand() / (float)RAND_MAX) < epsilon)) {
        return moves[rand() % count];
    }

    config = gungi_q_profile_config(model->profile);
    for (i = 0; i < count; ++i) {
        float score = gungi_v_score_move(model, state, moves[i], config->gamma);
        if (score > best_score) {
            best_score = score;
            best_index = i;
        }
    }

    return moves[best_index];
}
