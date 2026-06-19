#include "../src/q_search.h"
#include "../src/vp_model.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>

static int failures = 0;

#define CHECK(expr)                                                           \
    do {                                                                      \
        if (!(expr)) {                                                        \
            fprintf(stderr, "CHECK failed at %s:%d: %s\n", __FILE__, __LINE__, #expr); \
            failures++;                                                       \
        }                                                                     \
    } while (0)

static int all_zero_policy(const GungiVPModel *model)
{
    int i;

    for (i = 0; i < GUNGI_P_FEATURE_COUNT; ++i) {
        if (model->policy_weights[i] != 0.0f) {
            return 0;
        }
    }
    return 1;
}

static int any_nonzero_value(const GungiVPModel *model)
{
    int i;

    for (i = 0; i < GUNGI_V_FEATURE_COUNT; ++i) {
        if (model->value_weights[i] != 0.0f) {
            return 1;
        }
    }
    return 0;
}

static int finite_features(const float *features, int count)
{
    int i;

    for (i = 0; i < count; ++i) {
        if (!isfinite(features[i])) {
            return 0;
        }
    }
    return 1;
}

int main(void)
{
    const char *v3_path = "models/v_weights_hybrid_student_50000.bin";
    const char *roundtrip_path = "tests/vp_roundtrip_test.bin";
    GungiVPModel model;
    GungiVPModel roundtrip;
    GameState state;
    Move moves[GUNGI_MAX_LEGAL_MOVES];
    int count;
    float policy_features[GUNGI_P_FEATURE_COUNT];
    float value_features[GUNGI_V_FEATURE_COUNT];
    Move policy_move;
    Move hybrid_move;
    Move side_hybrid_move;
    RulesResult result;
    int black_score = 0;
    int i;

    gungi_vp_init(&model);
    CHECK(gungi_vp_load(&model, v3_path));
    CHECK(any_nonzero_value(&model));
    CHECK(all_zero_policy(&model));
    CHECK(gungi_vp_weights_are_finite(&model));

    CHECK(gungi_vp_save(&model, roundtrip_path));
    gungi_vp_init(&roundtrip);
    CHECK(gungi_vp_load(&roundtrip, roundtrip_path));
    CHECK(gungi_vp_weights_are_finite(&roundtrip));
    for (i = 0; i < GUNGI_V_FEATURE_COUNT; ++i) {
        CHECK(fabsf(model.value_weights[i] - roundtrip.value_weights[i]) < 0.000001f);
    }
    for (i = 0; i < GUNGI_P_FEATURE_COUNT; ++i) {
        CHECK(fabsf(model.policy_weights[i] - roundtrip.policy_weights[i]) < 0.000001f);
    }

    gungi_init(&state);
    gungi_v_extract_features(value_features, &state);
    CHECK(finite_features(value_features, GUNGI_V_FEATURE_COUNT));
    count = gungi_generate_legal_moves(&state, moves, GUNGI_MAX_LEGAL_MOVES);
    CHECK(count > 0);
    if (count > 0) {
        gungi_vp_extract_policy_features(policy_features, &state, moves[0]);
        CHECK(finite_features(policy_features, GUNGI_P_FEATURE_COUNT));
        CHECK(isfinite(gungi_p_score_move(&model, &state, moves[0])));
    }

    policy_move = gungi_get_vp_move(&state, &model, 0.0f);
    result = gungi_validate_move(&state, policy_move);
    CHECK(result.ok);

    hybrid_move = gungi_get_vp_hybrid_ai_move_scored(&state, &model, 1, NULL, &black_score);
    result = gungi_validate_move(&state, hybrid_move);
    CHECK(result.ok);

    side_hybrid_move = gungi_get_vp_side_policy_hybrid_ai_move_scored(&state,
                                                                      &model,
                                                                      &model,
                                                                      &model,
                                                                      1,
                                                                      NULL,
                                                                      &black_score);
    result = gungi_validate_move(&state, side_hybrid_move);
    CHECK(result.ok);

    remove(roundtrip_path);

    if (failures != 0) {
        fprintf(stderr, "test_vp_model: %d failures\n", failures);
        return 1;
    }

    printf("test_vp_model: checks passed\n");
    return 0;
}
