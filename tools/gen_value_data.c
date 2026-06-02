#include "../src/ai_core.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#define DEFAULT_POSITIONS 100000
#define MAX_PLY_PER_GAME 360
#define DATASET_SCHEMA_PREFIX "#gungi_value_schema"

static int g_teacher_static = 0;
static int g_teacher_depth1 = 0;
static int g_teacher_depth2 = 0;
static int g_teacher_depth3 = 0;
static int g_key_positions = 0;

static float target_from_score(int score)
{
    return (float)tanh((double)score / (double)GUNGI_VALUE_SCORE_SCALE);
}

static Move choose_greedy_move(const GameState *state)
{
    Move moves[GUNGI_MAX_LEGAL_MOVES];
    int count = gungi_generate_legal_moves(state, moves, GUNGI_MAX_LEGAL_MOVES);
    int is_black = state != NULL && state->current_player == GUNGI_PLAYER_BLACK;
    int best_score = is_black ? -2147483647 : 2147483647;
    Move best;
    int i;

    if (state == NULL || count == 0) {
        return gungi_make_resign(state != NULL ? state->current_player : GUNGI_PLAYER_BLACK);
    }

    best = moves[0];
    for (i = 0; i < count; ++i) {
        GameState next = *state;
        int score;
        if (!gungi_apply_move(&next, moves[i]).ok) {
            continue;
        }
        score = gungi_evaluate_board(&next);
        if ((is_black && score > best_score) || (!is_black && score < best_score)) {
            best_score = score;
            best = moves[i];
        }
    }

    return best;
}

static int board_piece_total(const GameState *state)
{
    int total = 0;
    int y;
    int x;

    if (state == NULL) {
        return 0;
    }

    for (y = 0; y < GUNGI_BOARD_SIZE; ++y) {
        for (x = 0; x < GUNGI_BOARD_SIZE; ++x) {
            total += gungi_cell_height(state, x, y);
        }
    }

    return total;
}

static int result_tactical_priority(int was_in_check, RulesResult result, const GameState *after)
{
    int priority = 0;

    if (after != NULL && after->status != GUNGI_STATUS_ONGOING) {
        priority = 5;
    }
    if (result.captured_marshal) {
        priority = 5;
    } else if (result.captured_count > 0 && result.gives_check && priority < 4) {
        priority = 4;
    } else if (result.gives_check && priority < 3) {
        priority = 3;
    } else if (result.captured_count > 0 && priority < 2) {
        priority = 2;
    }
    if (was_in_check && priority < 1) {
        priority = 1;
    }

    return priority;
}

static Move choose_tactical_move(const GameState *state)
{
    Move moves[GUNGI_MAX_LEGAL_MOVES];
    int count = gungi_generate_legal_moves(state, moves, GUNGI_MAX_LEGAL_MOVES);
    int is_black = state != NULL && state->current_player == GUNGI_PLAYER_BLACK;
    int was_in_check = state != NULL && gungi_is_in_check(state, state->current_player);
    int best_rank = -1;
    int best_score = -2147483647;
    Move best;
    int i;

    if (state == NULL || count == 0) {
        return gungi_make_resign(state != NULL ? state->current_player : GUNGI_PLAYER_BLACK);
    }

    best = moves[0];
    for (i = 0; i < count; ++i) {
        GameState next = *state;
        RulesResult result = gungi_apply_move(&next, moves[i]);
        int priority;
        int score;

        if (!result.ok) {
            continue;
        }

        priority = result_tactical_priority(was_in_check, result, &next);
        if (priority <= 0) {
            continue;
        }
        score = gungi_evaluate_board(&next);
        if (!is_black) {
            score = -score;
        }
        if (priority > best_rank || (priority == best_rank && score > best_score)) {
            best_rank = priority;
            best_score = score;
            best = moves[i];
        }
    }

    if (best_rank <= 0) {
        return choose_greedy_move(state);
    }

    return best;
}

static Move choose_mixed_move(const GameState *state)
{
    int roll = rand() % 100;

    if (roll < 35) {
        return gungi_get_random_move(state);
    }
    if (roll < 60) {
        return choose_greedy_move(state);
    }
    if (roll < 75) {
        return choose_tactical_move(state);
    }
    if (roll < 92) {
        return gungi_get_ai_move(state, 1);
    }
    return gungi_get_ai_move(state, 2);
}

static int is_key_position(const GameState *state)
{
    Move moves[GUNGI_MAX_LEGAL_MOVES];
    int count;
    int was_in_check;
    int i;

    if (state == NULL || state->status != GUNGI_STATUS_ONGOING) {
        return 0;
    }

    if (gungi_is_in_check(state, GUNGI_PLAYER_BLACK) || gungi_is_in_check(state, GUNGI_PLAYER_WHITE)) {
        return 1;
    }
    if (board_piece_total(state) <= 30) {
        return 1;
    }

    count = gungi_generate_legal_moves(state, moves, GUNGI_MAX_LEGAL_MOVES);
    if (count > 0 && count <= 24) {
        return 1;
    }

    was_in_check = gungi_is_in_check(state, state->current_player);
    for (i = 0; i < count; ++i) {
        GameState next = *state;
        RulesResult result = gungi_apply_move(&next, moves[i]);
        if (result.ok && result_tactical_priority(was_in_check, result, &next) >= 3) {
            return 1;
        }
    }

    return 0;
}

static int choose_teacher_score(const GameState *state, int is_key)
{
    int roll = rand() % 100;

    if (state == NULL || state->status != GUNGI_STATUS_ONGOING) {
        g_teacher_static++;
        return gungi_evaluate_board(state);
    }
    if ((is_key && roll < 8) || (!is_key && roll < 1)) {
        g_teacher_depth3++;
        return gungi_score_ai_position(state, 3);
    }
    if ((is_key && roll < 78) || (!is_key && roll < 76)) {
        g_teacher_depth2++;
        return gungi_score_ai_position(state, 2);
    }
    if ((is_key && roll < 92) || (!is_key && roll < 90)) {
        g_teacher_depth1++;
        return gungi_score_ai_position(state, 1);
    }
    g_teacher_static++;
    return gungi_evaluate_board(state);
}

static void write_header(FILE *out, int target_positions, unsigned int seed)
{
    int i;

    fprintf(out, "%s,%d,features,%d\n", DATASET_SCHEMA_PREFIX, GUNGI_VALUE_MODEL_VERSION, GUNGI_VALUE_FEATURE_COUNT);
    fprintf(out, "#generator,value_data_v2,positions,%d,seed,%u,max_ply,%d\n",
            target_positions,
            seed,
            MAX_PLY_PER_GAME);
    fprintf(out, "target");
    for (i = 0; i < GUNGI_VALUE_FEATURE_COUNT; ++i) {
        fprintf(out, ",f%d", i);
    }
    fprintf(out, "\n");
}

static void write_row(FILE *out, const GameState *state)
{
    float features[GUNGI_VALUE_FEATURE_COUNT];
    int key = is_key_position(state);
    float target = target_from_score(choose_teacher_score(state, key));
    int i;

    if (key) {
        g_key_positions++;
    }
    gungi_value_extract_features(state, features);
    fprintf(out, "%.8f", target);
    for (i = 0; i < GUNGI_VALUE_FEATURE_COUNT; ++i) {
        fprintf(out, ",%.8f", features[i]);
    }
    fprintf(out, "\n");
}

int main(int argc, char **argv)
{
    int target_positions = DEFAULT_POSITIONS;
    const char *output_path = "value_data_v2_100k.csv";
    unsigned int seed = (unsigned int)time(NULL);
    FILE *out;
    GameState state;
    int positions = 0;
    int ply = 0;

    if (argc > 1) {
        target_positions = atoi(argv[1]);
        if (target_positions <= 0) {
            target_positions = DEFAULT_POSITIONS;
        }
    }
    if (argc > 2) {
        output_path = argv[2];
    }
    if (argc > 3) {
        seed = (unsigned int)strtoul(argv[3], NULL, 10);
    }

    srand(seed);
    out = fopen(output_path, "w");
    if (out == NULL) {
        fprintf(stderr, "Failed to open %s\n", output_path);
        return 1;
    }

    gungi_init(&state);
    write_header(out, target_positions, seed);

    while (positions < target_positions) {
        Move move;
        RulesResult result;

        if (state.status != GUNGI_STATUS_ONGOING || ply >= MAX_PLY_PER_GAME) {
            gungi_init(&state);
            ply = 0;
        }

        write_row(out, &state);
        positions++;

        move = choose_mixed_move(&state);
        result = gungi_apply_move(&state, move);
        if (!result.ok) {
            gungi_init(&state);
            ply = 0;
            continue;
        }
        ply++;

        if (positions % 10000 == 0 || positions == target_positions) {
            printf("Generated %d/%d positions\n", positions, target_positions);
        }
    }

    fclose(out);
    printf("Saved %d positions to %s with seed %u\n", positions, output_path, seed);
    printf("Key positions: %d | teacher static=%d depth1=%d depth2=%d depth3=%d\n",
           g_key_positions,
           g_teacher_static,
           g_teacher_depth1,
           g_teacher_depth2,
           g_teacher_depth3);
    return 0;
}
