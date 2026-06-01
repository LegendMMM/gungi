#include "../src/ai_core.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int tests_run = 0;

#define CHECK(expr) \
    do { \
        tests_run++; \
        if (!(expr)) { \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #expr); \
            exit(1); \
        } \
    } while (0)

static void test_zero_model_is_stable(void)
{
    GameState state;
    GungiValueModel model;

    gungi_init(&state);
    gungi_value_init(&model);
    model.loaded = 1;

    CHECK(gungi_value_evaluate_raw(&model, &state) == 0.0f);
    CHECK(gungi_value_evaluate_board(&model, &state) == 0);
}

static void test_features_do_not_mutate_state(void)
{
    GameState state;
    GameState before;
    float features[GUNGI_VALUE_FEATURE_COUNT];

    gungi_init(&state);
    before = state;
    gungi_value_extract_features(&state, features);

    CHECK(memcmp(&state, &before, sizeof(state)) == 0);
}

static void test_model_save_load_round_trip(void)
{
    const char *path = "value_model_test.bin";
    GungiValueModel model;
    GungiValueModel loaded;

    remove(path);
    gungi_value_init(&model);
    model.loaded = 1;
    model.weights[0] = 0.25f;
    model.weights[17] = -0.50f;

    CHECK(gungi_value_save(&model, path));
    gungi_value_init(&loaded);
    CHECK(gungi_value_load(&loaded, path));
    CHECK(loaded.loaded);
    CHECK(loaded.weights[0] == model.weights[0]);
    CHECK(loaded.weights[17] == model.weights[17]);
    remove(path);
}

static void test_public_move_generator_returns_valid_moves(void)
{
    GameState state;
    Move moves[GUNGI_MAX_LEGAL_MOVES];
    int count;
    int i;

    gungi_init(&state);
    count = gungi_generate_legal_moves(&state, moves, GUNGI_MAX_LEGAL_MOVES);

    CHECK(count > 0);
    CHECK(count <= GUNGI_MAX_LEGAL_MOVES);
    for (i = 0; i < count; ++i) {
        CHECK(gungi_validate_move(&state, moves[i]).ok);
    }
}

static void test_ai_moves_are_legal(void)
{
    GameState state;
    Move move;

    gungi_init(&state);
    move = gungi_get_ai_move(&state, 1);

    CHECK(gungi_validate_move(&state, move).ok);
}

int main(void)
{
    test_zero_model_is_stable();
    test_features_do_not_mutate_state();
    test_model_save_load_round_trip();
    test_public_move_generator_returns_valid_moves();
    test_ai_moves_are_legal();

    printf("test_value_model: %d checks passed\n", tests_run);
    return 0;
}
