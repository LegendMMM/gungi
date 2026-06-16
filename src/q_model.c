#include "q_model.h"

#include <float.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PIECE_FEATURE_COUNT (GUNGI_PIECE_TYPE_COUNT - 1)
#define BOARD_COUNT_SCALE 10.0f
#define STACK_LEVEL_SCALE 20.0f
#define HAND_COUNT_SCALE 10.0f
#define TRAINING_PLY_LIMIT 600.0f
#define TRAINING_GENERATED_MOVE_LIMIT 512
#define TRAINING_CANDIDATE_LIMIT 64

static const char MODEL_MAGIC[8] = { 'G', 'U', 'N', 'G', 'I', 'V', '3', '\0' };

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

static int player_attacks_enemy_marshal(const GameState *state, GungiPlayer player)
{
    GungiPlayer opponent = gungi_opponent(player);
    int marshal_x = -1;
    int marshal_y = -1;

    if (state == NULL || !find_top_marshal_local(state, opponent, &marshal_x, &marshal_y)) {
        return 0;
    }

    return gungi_is_square_attacked(state, marshal_x, marshal_y, player);
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

void gungi_v_extract_features(float features[GUNGI_V_FEATURE_COUNT], const GameState *state)
{
    GungiPlayer player = state->current_player;
    GungiPlayer opponent = gungi_opponent(player);
    int board_counts[2][GUNGI_PIECE_TYPE_COUNT];
    int stack_levels[2][GUNGI_PIECE_TYPE_COUNT];
    int index = 0;
    int y;
    int x;
    int level;
    int type;

    memset(features, 0, sizeof(float) * GUNGI_V_FEATURE_COUNT);
    memset(board_counts, 0, sizeof(board_counts));
    memset(stack_levels, 0, sizeof(stack_levels));

    features[index++] = 1.0f;

    for (y = 0; y < GUNGI_BOARD_SIZE; ++y) {
        for (x = 0; x < GUNGI_BOARD_SIZE; ++x) {
            int height = gungi_cell_height(state, x, y);
            for (level = 0; level < height; ++level) {
                Piece piece = gungi_stack_piece(state, x, y, level);
                if (piece.owner == GUNGI_PLAYER_BLACK || piece.owner == GUNGI_PLAYER_WHITE) {
                    board_counts[piece.owner][piece.type]++;
                    stack_levels[piece.owner][piece.type] += level + 1;
                }
            }
        }
    }

    for (type = GUNGI_PIECE_NONE + 1; type < GUNGI_PIECE_TYPE_COUNT; ++type) {
        features[index++] =
            (float)(board_counts[player][type] - board_counts[opponent][type]) / BOARD_COUNT_SCALE;
    }

    for (type = GUNGI_PIECE_NONE + 1; type < GUNGI_PIECE_TYPE_COUNT; ++type) {
        features[index++] =
            (float)(stack_levels[player][type] - stack_levels[opponent][type]) / STACK_LEVEL_SCALE;
    }

    for (type = GUNGI_PIECE_NONE + 1; type < GUNGI_PIECE_TYPE_COUNT; ++type) {
        features[index++] =
            (float)(gungi_hand_count(state, player, (GungiPieceType)type) -
                    gungi_hand_count(state, opponent, (GungiPieceType)type)) / HAND_COUNT_SCALE;
    }

    features[index++] = player_attacks_enemy_marshal(state, player) ? 1.0f : 0.0f;
}

float gungi_v_evaluate(const GungiQModel *model, const GameState *state)
{
    float features[GUNGI_V_FEATURE_COUNT];
    float score = 0.0f;
    int i;

    if (model == NULL || state == NULL || state->status != GUNGI_STATUS_ONGOING) {
        return 0.0f;
    }

    gungi_v_extract_features(features, state);
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

    gungi_v_extract_features(features, state);
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
    (void)before;
    (void)result;

    if (timeout) {
        return -1.0f;
    }
    if (after != NULL) {
        if (after->status == GUNGI_STATUS_BLACK_WIN ||
            after->status == GUNGI_STATUS_WHITE_WIN ||
            after->status == GUNGI_STATUS_RESIGNED) {
            return after->winner == move.player ? 1.0f : -1.0f;
        }
        if (after->status == GUNGI_STATUS_DRAW) {
            return -0.5f;
        }
    }

    return -0.01f;
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
    int move_limit = epsilon > 0.0f ? TRAINING_GENERATED_MOVE_LIMIT : GUNGI_MAX_LEGAL_MOVES;
    int count = gungi_generate_legal_moves(state, moves, move_limit);
    int score_count = count;
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

    if (epsilon > 0.0f && score_count > TRAINING_CANDIDATE_LIMIT) {
        score_count = TRAINING_CANDIDATE_LIMIT;
        for (i = 0; i < score_count; ++i) {
            int j = i + rand() % (count - i);
            Move temp = moves[i];
            moves[i] = moves[j];
            moves[j] = temp;
        }
    }

    config = gungi_q_profile_config(model->profile);
    for (i = 0; i < score_count; ++i) {
        float score = gungi_v_score_move(model, state, moves[i], config->gamma);
        if (score > best_score) {
            best_score = score;
            best_index = i;
        }
    }

    return moves[best_index];
}
