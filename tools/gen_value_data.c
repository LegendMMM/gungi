#include "../src/ai_core.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#define DEFAULT_POSITIONS 60000
#define MAX_PLY_PER_GAME 300

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

static Move choose_mixed_move(const GameState *state)
{
    int roll = rand() % 100;

    if (roll < 40) {
        return gungi_get_random_move(state);
    }
    if (roll < 70) {
        return choose_greedy_move(state);
    }
    if (roll < 90) {
        return gungi_get_ai_move(state, 1);
    }
    return gungi_get_ai_move(state, 2);
}

static int choose_teacher_score(const GameState *state)
{
    int roll = rand() % 10;

    if (state == NULL || state->status != GUNGI_STATUS_ONGOING) {
        return gungi_evaluate_board(state);
    }
    if (roll < 7) {
        return gungi_score_ai_position(state, 2);
    }
    if (roll < 9) {
        return gungi_score_ai_position(state, 1);
    }
    return gungi_evaluate_board(state);
}

static void write_header(FILE *out)
{
    int i;

    fprintf(out, "target");
    for (i = 0; i < GUNGI_VALUE_FEATURE_COUNT; ++i) {
        fprintf(out, ",f%d", i);
    }
    fprintf(out, "\n");
}

static void write_row(FILE *out, const GameState *state)
{
    float features[GUNGI_VALUE_FEATURE_COUNT];
    float target = target_from_score(choose_teacher_score(state));
    int i;

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
    const char *output_path = "value_data.csv";
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
    write_header(out);

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
    return 0;
}
