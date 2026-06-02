#include "../src/ai_core.h"
#include "../src/q_model.h"
#include "../src/q_search.h"

#include <float.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#ifdef _WIN32
#include <direct.h>
#else
#include <sys/stat.h>
#endif

#define DEFAULT_SAMPLES 50000
#define DEFAULT_DEPTH 2
#define DEFAULT_MAX_PLY 1000
#define DEFAULT_SEED 424242
#define DEFAULT_LEARNING_RATE 0.002f
#define CHECKPOINT_INTERVAL 5000
#define TARGET_SCALE 30000.0f

typedef enum DriverMode {
    DRIVER_HYBRID_SELF = 0,
    DRIVER_HYBRID_BLACK = 1,
    DRIVER_HYBRID_WHITE = 2,
    DRIVER_MODE_COUNT = 3
} DriverMode;

typedef struct DriverState {
    GameState state;
    int ply;
} DriverState;

typedef struct TrainStats {
    int samples;
    int agreements;
    int black_samples;
    int white_samples;
    int resets;
    double target_total;
    double abs_error_total;
    float target_min;
    float target_max;
} TrainStats;

static void ensure_models_dir(void)
{
#ifdef _WIN32
    _mkdir("models");
#else
    mkdir("models", 0755);
#endif
}

static const char *driver_name(DriverMode mode)
{
    switch (mode) {
    case DRIVER_HYBRID_SELF:
        return "hybrid_self";
    case DRIVER_HYBRID_BLACK:
        return "hybrid_black";
    case DRIVER_HYBRID_WHITE:
        return "hybrid_white";
    default:
        return "unknown";
    }
}

static int same_move(Move a, Move b)
{
    return a.kind == b.kind &&
           a.player == b.player &&
           a.from_x == b.from_x &&
           a.from_y == b.from_y &&
           a.to_x == b.to_x &&
           a.to_y == b.to_y &&
           a.drop_type == b.drop_type &&
           a.intent == b.intent &&
           a.betray_mask == b.betray_mask;
}

static float clampf(float value, float min_value, float max_value)
{
    if (value < min_value) {
        return min_value;
    }
    if (value > max_value) {
        return max_value;
    }
    return value;
}

static float weight_checksum(const GungiQModel *model)
{
    float total = 0.0f;
    int i;

    for (i = 0; i < GUNGI_V_FEATURE_COUNT; ++i) {
        total += fabsf(model->weights[i]);
    }

    return total;
}

static int has_bad_weight(const GungiQModel *model)
{
    int i;

    for (i = 0; i < GUNGI_V_FEATURE_COUNT; ++i) {
        if (!isfinite(model->weights[i])) {
            return 1;
        }
    }

    return 0;
}

static void print_weights(const char *label, int sample, const GungiQModel *model)
{
    int i;

    printf("%s sample %d | checksum %.6f:", label, sample, weight_checksum(model));
    for (i = 0; i < GUNGI_V_FEATURE_COUNT; ++i) {
        printf(" w%d=%.6f", i, model->weights[i]);
    }
    printf("\n");
}

static void reset_window_stats(TrainStats *stats)
{
    stats->samples = 0;
    stats->agreements = 0;
    stats->black_samples = 0;
    stats->white_samples = 0;
    stats->resets = 0;
    stats->target_total = 0.0;
    stats->abs_error_total = 0.0;
    stats->target_min = 0.0f;
    stats->target_max = 0.0f;
}

static void add_sample_stats(TrainStats *stats,
                             GungiPlayer player,
                             float target,
                             float prediction,
                             int agreement)
{
    float abs_error = fabsf(target - prediction);

    if (stats->samples == 0 || target < stats->target_min) {
        stats->target_min = target;
    }
    if (stats->samples == 0 || target > stats->target_max) {
        stats->target_max = target;
    }

    stats->samples++;
    stats->agreements += agreement ? 1 : 0;
    if (player == GUNGI_PLAYER_BLACK) {
        stats->black_samples++;
    } else if (player == GUNGI_PLAYER_WHITE) {
        stats->white_samples++;
    }
    stats->target_total += target;
    stats->abs_error_total += abs_error;
}

static Move select_driver_move(DriverMode mode,
                               const GameState *state,
                               const GungiQModel *teacher,
                               int depth,
                               const GungiHybridSearchConfig *search_config,
                               Move teacher_move)
{
    if (mode == DRIVER_HYBRID_SELF) {
        return teacher_move;
    }
    if (mode == DRIVER_HYBRID_BLACK) {
        if (state->current_player == GUNGI_PLAYER_BLACK) {
            return teacher_move;
        }
        return gungi_get_ai_move(state, depth);
    }
    if (mode == DRIVER_HYBRID_WHITE) {
        if (state->current_player == GUNGI_PLAYER_WHITE) {
            return teacher_move;
        }
        return gungi_get_ai_move(state, depth);
    }

    return gungi_get_hybrid_ai_move(state, teacher, depth, search_config);
}

static int train_sample(DriverMode mode,
                        DriverState *driver,
                        const GungiQModel *teacher,
                        GungiQModel *student,
                        int depth,
                        int max_ply,
                        float learning_rate,
                        const GungiHybridSearchConfig *search_config,
                        TrainStats *window,
                        TrainStats *total)
{
    GameState before = driver->state;
    Move teacher_move;
    Move student_move;
    Move driver_move;
    RulesResult result;
    int black_score = 0;
    float target;
    float prediction;
    int agreement;

    if (before.status != GUNGI_STATUS_ONGOING) {
        gungi_init(&driver->state);
        driver->ply = 0;
        before = driver->state;
        window->resets++;
        total->resets++;
    }

    teacher_move = gungi_get_hybrid_ai_move_scored(&before, teacher, depth, search_config, &black_score);
    student_move = gungi_get_q_move(&before, student, 0.0f);
    agreement = same_move(teacher_move, student_move);

    target = (float)black_score / TARGET_SCALE;
    if (before.current_player == GUNGI_PLAYER_WHITE) {
        target = -target;
    }
    target = clampf(target, -1.0f, 1.0f);

    prediction = gungi_v_evaluate(student, &before);
    add_sample_stats(window, before.current_player, target, prediction, agreement);
    add_sample_stats(total, before.current_player, target, prediction, agreement);
    gungi_v_update(student, &before, target, learning_rate);

    if (has_bad_weight(student)) {
        fprintf(stderr, "Bad student weight after sample %d\n", total->samples);
        return 0;
    }

    driver_move = select_driver_move(mode, &before, teacher, depth, search_config, teacher_move);
    result = gungi_apply_move(&driver->state, driver_move);
    if (!result.ok) {
        fprintf(stderr,
                "Illegal driver move at sample %d | mode %s | player %d | code %d\n",
                total->samples,
                driver_name(mode),
                driver_move.player,
                result.code);
        return 0;
    }

    driver->ply++;
    if (driver->state.status != GUNGI_STATUS_ONGOING || driver->ply >= max_ply) {
        gungi_init(&driver->state);
        driver->ply = 0;
        window->resets++;
        total->resets++;
    }

    return 1;
}

static void write_checkpoint(FILE *csv,
                             int sample,
                             const TrainStats *window,
                             const GungiQModel *student)
{
    float target_avg = window->samples > 0 ? (float)(window->target_total / (double)window->samples) : 0.0f;
    float abs_error_avg = window->samples > 0 ? (float)(window->abs_error_total / (double)window->samples) : 0.0f;
    float agreement_rate = window->samples > 0 ? (float)window->agreements / (float)window->samples : 0.0f;

    fprintf(csv,
            "%d,rotating,mixed,%.6f,%.6f,%.6f,%.6f,%.6f,%d,%d,%d,%.6f\n",
            sample,
            target_avg,
            window->target_min,
            window->target_max,
            abs_error_avg,
            agreement_rate,
            window->black_samples,
            window->white_samples,
            window->resets,
            weight_checksum(student));
    fflush(csv);

    printf("Sample %d | target avg %.6f min %.6f max %.6f | abs error %.6f | agree %.3f | B %d W %d resets %d | checksum %.6f\n",
           sample,
           target_avg,
           window->target_min,
           window->target_max,
           abs_error_avg,
           agreement_rate,
           window->black_samples,
           window->white_samples,
           window->resets,
           weight_checksum(student));
    print_weights("Weights", sample, student);
    fflush(stdout);
}

int main(int argc, char **argv)
{
    const char *teacher_path = argc > 1 ? argv[1] : "models/v_weights_100000.bin";
    const char *student_base_path = argc > 2 ? argv[2] : "models/v_weights_100000.bin";
    const char *out_model_path = argc > 3 ? argv[3] : "models/v_weights_hybrid_student_50000.bin";
    int samples = argc > 4 ? atoi(argv[4]) : DEFAULT_SAMPLES;
    int depth = argc > 5 ? atoi(argv[5]) : DEFAULT_DEPTH;
    int max_ply = argc > 6 ? atoi(argv[6]) : DEFAULT_MAX_PLY;
    int seed = argc > 7 ? atoi(argv[7]) : DEFAULT_SEED;
    float learning_rate = argc > 8 ? (float)atof(argv[8]) : DEFAULT_LEARNING_RATE;
    const char *csv_path = argc > 9 ? argv[9] : "train_hybrid_student_50000.csv";
    GungiHybridSearchConfig search_config = GUNGI_HYBRID_SEARCH_DEFAULT_CONFIG;
    GungiQModel teacher;
    GungiQModel student;
    GungiQModel verify_model;
    DriverState drivers[DRIVER_MODE_COUNT];
    TrainStats window;
    TrainStats total;
    FILE *csv;
    int sample;
    int i;

    if (samples <= 0) {
        samples = DEFAULT_SAMPLES;
    }
    if (depth <= 0) {
        depth = DEFAULT_DEPTH;
    }
    if (max_ply <= 0) {
        max_ply = DEFAULT_MAX_PLY;
    }
    if (learning_rate <= 0.0f) {
        learning_rate = DEFAULT_LEARNING_RATE;
    }

    srand((unsigned int)seed);
    ensure_models_dir();

    gungi_q_init(&teacher);
    if (!gungi_q_load(&teacher, teacher_path)) {
        fprintf(stderr, "Failed to load teacher model %s\n", teacher_path);
        return 1;
    }

    gungi_q_init(&student);
    if (!gungi_q_load(&student, student_base_path)) {
        fprintf(stderr, "Failed to load student base model %s\n", student_base_path);
        return 1;
    }

    csv = fopen(csv_path, "w");
    if (csv == NULL) {
        fprintf(stderr, "Failed to open CSV %s\n", csv_path);
        return 1;
    }

    fprintf(csv, "sample,mode,current_player,target_avg,target_min,target_max,abs_error_avg,agreement_rate,black_samples,white_samples,resets,checksum\n");
    fflush(csv);

    for (i = 0; i < DRIVER_MODE_COUNT; ++i) {
        gungi_init(&drivers[i].state);
        drivers[i].ply = 0;
    }
    reset_window_stats(&window);
    reset_window_stats(&total);

    printf("Training hybrid student | samples %d | teacher %s | base %s | out %s | depth %d | max ply %d | seed %d | lr %.6f | scale %.1f\n",
           samples,
           teacher_path,
           student_base_path,
           out_model_path,
           depth,
           max_ply,
           seed,
           learning_rate,
           search_config.model_scale);
    print_weights("Initial student weights", 0, &student);

    for (sample = 1; sample <= samples; ++sample) {
        DriverMode mode = (DriverMode)((sample - 1) % DRIVER_MODE_COUNT);

        if (!train_sample(mode,
                          &drivers[mode],
                          &teacher,
                          &student,
                          depth,
                          max_ply,
                          learning_rate,
                          &search_config,
                          &window,
                          &total)) {
            fclose(csv);
            return 1;
        }

        if (sample % CHECKPOINT_INTERVAL == 0 || sample == samples) {
            write_checkpoint(csv, sample, &window, &student);
            reset_window_stats(&window);
        }
    }

    fclose(csv);

    if (!gungi_q_save(&student, out_model_path)) {
        fprintf(stderr, "Failed to save student model %s\n", out_model_path);
        return 1;
    }

    gungi_q_init(&verify_model);
    if (!gungi_q_load(&verify_model, out_model_path)) {
        fprintf(stderr, "Failed to reload saved student model %s\n", out_model_path);
        return 1;
    }

    printf("Saved %s | checksum %.6f | total samples %d | total agreement %.6f | total resets %d\n",
           out_model_path,
           weight_checksum(&verify_model),
           total.samples,
           total.samples > 0 ? (float)total.agreements / (float)total.samples : 0.0f,
           total.resets);
    print_weights("Saved weights", samples, &verify_model);
    fflush(stdout);

    return 0;
}
