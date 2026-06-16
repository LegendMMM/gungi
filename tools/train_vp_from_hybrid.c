#include "../src/q_model.h"
#include "../src/q_search.h"
#include "../src/vp_model.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _OPENMP
#include <omp.h>
#endif

#define DEFAULT_SAMPLES 100000
#define DEFAULT_MAX_PLY 1000
#define DEFAULT_SEED 515151U
#define DEFAULT_DEPTH 2
#define DEFAULT_MAX_CANDIDATES 64
#define TARGET_SCALE 60000.0f

static const char DATASET_MAGIC[8] = { 'G', 'V', 'P', 'D', 'S', '1', '\0', '\0' };

static const char *player_name(GungiPlayer player)
{
    if (player == GUNGI_PLAYER_BLACK) {
        return "B";
    }
    if (player == GUNGI_PLAYER_WHITE) {
        return "W";
    }
    return "-";
}

static const char *status_name(GungiGameStatus status)
{
    switch (status) {
    case GUNGI_STATUS_ONGOING:
        return "ongoing";
    case GUNGI_STATUS_BLACK_WIN:
        return "black_win";
    case GUNGI_STATUS_WHITE_WIN:
        return "white_win";
    case GUNGI_STATUS_DRAW:
        return "draw";
    case GUNGI_STATUS_RESIGNED:
        return "resigned";
    default:
        return "unknown";
    }
}

static float clampf(float value, float low, float high)
{
    if (value < low) {
        return low;
    }
    if (value > high) {
        return high;
    }
    return value;
}

static int write_int(FILE *file, int value)
{
    return fwrite(&value, sizeof(value), 1, file) == 1;
}

static int write_float(FILE *file, float value)
{
    return fwrite(&value, sizeof(value), 1, file) == 1;
}

static int find_move_index(const Move *moves, int count, Move target)
{
    int i;

    for (i = 0; i < count; ++i) {
        if (gungi_moves_equal(moves[i], target)) {
            return i;
        }
    }
    return -1;
}

static int move_already_selected(const Move *moves, int count, Move target)
{
    return find_move_index(moves, count, target) >= 0;
}

static int select_candidates(const Move *legal_moves,
                             int legal_count,
                             Move teacher_move,
                             Move *candidate_moves,
                             int max_candidates)
{
    int candidate_count = 0;
    int i;
    int guard = 0;

    if (max_candidates <= 0) {
        return 0;
    }

    candidate_moves[candidate_count++] = teacher_move;

    if (legal_count <= max_candidates) {
        for (i = 0; i < legal_count && candidate_count < max_candidates; ++i) {
            if (!gungi_moves_equal(legal_moves[i], teacher_move)) {
                candidate_moves[candidate_count++] = legal_moves[i];
            }
        }
        return candidate_count;
    }

    while (candidate_count < max_candidates && guard < legal_count * 8) {
        int index = rand() % legal_count;
        Move candidate = legal_moves[index];
        if (!move_already_selected(candidate_moves, candidate_count, candidate)) {
            candidate_moves[candidate_count++] = candidate;
        }
        guard++;
    }

    for (i = 0; i < legal_count && candidate_count < max_candidates; ++i) {
        if (!move_already_selected(candidate_moves, candidate_count, legal_moves[i])) {
            candidate_moves[candidate_count++] = legal_moves[i];
        }
    }

    return candidate_count;
}

static void write_summary_header(FILE *csv)
{
    fprintf(csv,
            "sample,game,ply,player,position_hash,legal_count,candidate_count,teacher_black_score,value_target,"
            "teacher_kind,teacher_intent,from_x,from_y,to_x,to_y,drop_type,betray_mask,status_after,winner_after,timed_out\n");
}

static void write_candidate_header(FILE *csv)
{
    fprintf(csv,
            "sample,game,ply,candidate_index,is_teacher,player,kind,intent,from_x,from_y,to_x,to_y,drop_type,betray_mask,"
            "capture_value,gives_check,status_after,winner_after,teacher_black_score,value_target\n");
}

static void write_candidate_row(FILE *csv,
                                int sample,
                                int game,
                                int ply,
                                int candidate_index,
                                int is_teacher,
                                const GameState *before,
                                Move move,
                                int teacher_black_score,
                                float value_target)
{
    GameState after = *before;
    RulesResult result = gungi_apply_move(&after, move);
    int capture_value = gungi_q_capture_value(before, move);

    fprintf(csv,
            "%d,%d,%d,%d,%d,%s,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%s,%s,%d,%.6f\n",
            sample,
            game,
            ply,
            candidate_index,
            is_teacher,
            player_name(move.player),
            move.kind,
            move.intent,
            move.from_x,
            move.from_y,
            move.to_x,
            move.to_y,
            move.drop_type,
            move.betray_mask,
            capture_value,
            result.ok ? result.gives_check : 0,
            result.ok ? status_name(after.status) : "illegal",
            result.ok ? player_name(after.winner) : "-",
            teacher_black_score,
            value_target);
}

static int write_dataset_header(FILE *dataset, int samples, int max_candidates, int record_size)
{
    int version = 1;

    return fwrite(DATASET_MAGIC, sizeof(DATASET_MAGIC), 1, dataset) == 1 &&
           write_int(dataset, version) &&
           write_int(dataset, samples) &&
           write_int(dataset, GUNGI_V_FEATURE_COUNT) &&
           write_int(dataset, GUNGI_P_FEATURE_COUNT) &&
           write_int(dataset, max_candidates) &&
           write_int(dataset, record_size);
}

static int write_dataset_record(FILE *dataset,
                                int ply,
                                GungiPlayer player,
                                int legal_count,
                                int candidate_count,
                                int teacher_black_score,
                                float value_target,
                                const float value_features[GUNGI_V_FEATURE_COUNT],
                                const float *policy_features,
                                int max_candidates)
{
    size_t policy_count = (size_t)max_candidates * (size_t)GUNGI_P_FEATURE_COUNT;

    return write_int(dataset, ply) &&
           write_int(dataset, (int)player) &&
           write_int(dataset, legal_count) &&
           write_int(dataset, candidate_count) &&
           write_int(dataset, 0) &&
           write_int(dataset, teacher_black_score) &&
           write_float(dataset, value_target) &&
           fwrite(value_features, sizeof(float), GUNGI_V_FEATURE_COUNT, dataset) == GUNGI_V_FEATURE_COUNT &&
           fwrite(policy_features, sizeof(float), policy_count, dataset) == policy_count;
}

int main(int argc, char **argv)
{
    const char *dataset_path = argc > 2 ? argv[2] : "train_vp_hybrid_student_100000.samples.bin";
    const char *summary_csv_path = argc > 3 ? argv[3] : "train_vp_hybrid_student_100000.csv";
    const char *log_path = argc > 4 ? argv[4] : "train_vp_hybrid_student_100000.log";
    const char *teacher_model_path = argc > 5 ? argv[5] : "models/v_weights_hybrid_student_50000.bin";
    const char *candidate_csv_path = argc > 9 ? argv[9] : "train_vp_hybrid_student_100000_candidates.csv";
    int samples = argc > 1 ? atoi(argv[1]) : DEFAULT_SAMPLES;
    unsigned int seed = argc > 6 ? (unsigned int)strtoul(argv[6], NULL, 10) : DEFAULT_SEED;
    int max_ply = argc > 7 ? atoi(argv[7]) : DEFAULT_MAX_PLY;
    int depth = argc > 8 ? atoi(argv[8]) : DEFAULT_DEPTH;
    int max_candidates = argc > 10 ? atoi(argv[10]) : DEFAULT_MAX_CANDIDATES;
    GungiQModel teacher;
    GungiHybridSearchConfig config = GUNGI_HYBRID_SEARCH_DEFAULT_CONFIG;
    FILE *dataset;
    FILE *summary_csv;
    FILE *candidate_csv;
    FILE *log;
    Move legal_moves[GUNGI_MAX_LEGAL_MOVES];
    Move *candidate_moves;
    float value_features[GUNGI_V_FEATURE_COUNT];
    float *policy_features;
    GameState state;
    int sample;
    int game = 1;
    int ply = 0;
    int black_wins = 0;
    int white_wins = 0;
    int draws = 0;
    int timeouts = 0;
    int record_size;

    if (samples <= 0) {
        samples = DEFAULT_SAMPLES;
    }
    if (max_ply <= 0) {
        max_ply = DEFAULT_MAX_PLY;
    }
    if (depth <= 0) {
        depth = DEFAULT_DEPTH;
    }
    if (max_candidates <= 1) {
        max_candidates = DEFAULT_MAX_CANDIDATES;
    }
    if (max_candidates > GUNGI_MAX_LEGAL_MOVES) {
        max_candidates = GUNGI_MAX_LEGAL_MOVES;
    }

#ifdef _OPENMP
    omp_set_num_threads(1);
#endif

    srand(seed);
    gungi_q_init(&teacher);
    if (!gungi_q_load(&teacher, teacher_model_path)) {
        fprintf(stderr, "Failed to load teacher model %s\n", teacher_model_path);
        return 1;
    }

    dataset = fopen(dataset_path, "wb");
    summary_csv = fopen(summary_csv_path, "w");
    candidate_csv = fopen(candidate_csv_path, "w");
    log = fopen(log_path, "w");
    if (dataset == NULL || summary_csv == NULL || candidate_csv == NULL || log == NULL) {
        fprintf(stderr, "Failed to open one or more output files\n");
        if (dataset != NULL) {
            fclose(dataset);
        }
        if (summary_csv != NULL) {
            fclose(summary_csv);
        }
        if (candidate_csv != NULL) {
            fclose(candidate_csv);
        }
        if (log != NULL) {
            fclose(log);
        }
        return 1;
    }

    candidate_moves = (Move *)calloc((size_t)max_candidates, sizeof(Move));
    policy_features = (float *)calloc((size_t)max_candidates * (size_t)GUNGI_P_FEATURE_COUNT, sizeof(float));
    if (candidate_moves == NULL || policy_features == NULL) {
        fprintf(stderr, "Failed to allocate candidate buffers\n");
        fclose(dataset);
        fclose(summary_csv);
        fclose(candidate_csv);
        fclose(log);
        free(candidate_moves);
        free(policy_features);
        return 1;
    }

    record_size = (int)(sizeof(int) * 6 +
                        sizeof(float) +
                        sizeof(float) * GUNGI_V_FEATURE_COUNT +
                        sizeof(float) * max_candidates * GUNGI_P_FEATURE_COUNT);
    if (!write_dataset_header(dataset, samples, max_candidates, record_size)) {
        fprintf(stderr, "Failed to write dataset header\n");
        fclose(dataset);
        fclose(summary_csv);
        fclose(candidate_csv);
        fclose(log);
        free(candidate_moves);
        free(policy_features);
        return 1;
    }

    write_summary_header(summary_csv);
    write_candidate_header(candidate_csv);
    fprintf(log,
            "samples=%d depth=%d max_ply=%d seed=%u teacher=%s value_features=%d policy_features=%d max_candidates=%d target_scale=%.1f\n",
            samples,
            depth,
            max_ply,
            seed,
            teacher_model_path,
            GUNGI_V_FEATURE_COUNT,
            GUNGI_P_FEATURE_COUNT,
            max_candidates,
            TARGET_SCALE);

    gungi_init(&state);
    for (sample = 1; sample <= samples; ++sample) {
        GameState before;
        GameState after;
        RulesResult result;
        Move teacher_move;
        int teacher_black_score = 0;
        int legal_count;
        int teacher_legal_index;
        int candidate_count;
        int candidate_index;
        int timed_out;
        float value_target;
        uint64_t position_hash;

        if (state.status != GUNGI_STATUS_ONGOING || ply >= max_ply) {
            if (state.status == GUNGI_STATUS_BLACK_WIN || (state.status == GUNGI_STATUS_RESIGNED && state.winner == GUNGI_PLAYER_BLACK)) {
                black_wins++;
            } else if (state.status == GUNGI_STATUS_WHITE_WIN || (state.status == GUNGI_STATUS_RESIGNED && state.winner == GUNGI_PLAYER_WHITE)) {
                white_wins++;
            } else {
                draws++;
                if (state.status == GUNGI_STATUS_ONGOING && ply >= max_ply) {
                    timeouts++;
                }
            }
            game++;
            ply = 0;
            gungi_init(&state);
        }

        before = state;
        legal_count = gungi_generate_legal_moves(&before, legal_moves, GUNGI_MAX_LEGAL_MOVES);
        if (legal_count <= 0) {
            game++;
            ply = 0;
            gungi_init(&state);
            sample--;
            continue;
        }

        teacher_move = gungi_get_hybrid_ai_move_scored(&before, &teacher, depth, &config, &teacher_black_score);
        teacher_legal_index = find_move_index(legal_moves, legal_count, teacher_move);
        if (teacher_legal_index < 0) {
            fprintf(stderr, "Teacher generated a move outside legal set at sample %d game %d ply %d\n", sample, game, ply);
            fclose(dataset);
            fclose(summary_csv);
            fclose(candidate_csv);
            fclose(log);
            free(candidate_moves);
            free(policy_features);
            return 1;
        }

        value_target = (float)teacher_black_score / TARGET_SCALE;
        if (before.current_player == GUNGI_PLAYER_WHITE) {
            value_target = -value_target;
        }
        value_target = clampf(value_target, -1.0f, 1.0f);

        gungi_v_extract_features(value_features, &before);
        memset(policy_features, 0, sizeof(float) * (size_t)max_candidates * (size_t)GUNGI_P_FEATURE_COUNT);
        candidate_count = select_candidates(legal_moves, legal_count, teacher_move, candidate_moves, max_candidates);
        for (candidate_index = 0; candidate_index < candidate_count; ++candidate_index) {
            gungi_vp_extract_policy_features(&policy_features[(size_t)candidate_index * GUNGI_P_FEATURE_COUNT],
                                             &before,
                                             candidate_moves[candidate_index]);
            write_candidate_row(candidate_csv,
                                sample,
                                game,
                                ply,
                                candidate_index,
                                candidate_index == 0,
                                &before,
                                candidate_moves[candidate_index],
                                teacher_black_score,
                                value_target);
        }

        if (!write_dataset_record(dataset,
                                  ply,
                                  before.current_player,
                                  legal_count,
                                  candidate_count,
                                  teacher_black_score,
                                  value_target,
                                  value_features,
                                  policy_features,
                                  max_candidates)) {
            fprintf(stderr, "Failed to write dataset record at sample %d\n", sample);
            fclose(dataset);
            fclose(summary_csv);
            fclose(candidate_csv);
            fclose(log);
            free(candidate_moves);
            free(policy_features);
            return 1;
        }

        after = before;
        result = gungi_apply_move(&after, teacher_move);
        if (!result.ok) {
            fprintf(stderr, "Illegal teacher move at sample %d: code %d\n", sample, result.code);
            fclose(dataset);
            fclose(summary_csv);
            fclose(candidate_csv);
            fclose(log);
            free(candidate_moves);
            free(policy_features);
            return 1;
        }

        timed_out = after.status == GUNGI_STATUS_ONGOING && (ply + 1) >= max_ply;
        position_hash = gungi_position_hash(&before);
        fprintf(summary_csv,
                "%d,%d,%d,%s,%llu,%d,%d,%d,%.6f,%d,%d,%d,%d,%d,%d,%d,%d,%s,%s,%d\n",
                sample,
                game,
                ply,
                player_name(before.current_player),
                (unsigned long long)position_hash,
                legal_count,
                candidate_count,
                teacher_black_score,
                value_target,
                teacher_move.kind,
                teacher_move.intent,
                teacher_move.from_x,
                teacher_move.from_y,
                teacher_move.to_x,
                teacher_move.to_y,
                teacher_move.drop_type,
                teacher_move.betray_mask,
                status_name(after.status),
                player_name(after.winner),
                timed_out);

        state = after;
        ply++;

        if (sample % 1000 == 0 || sample == samples) {
            fprintf(log,
                    "sample=%d/%d game=%d ply=%d B=%d W=%d D=%d T=%d\n",
                    sample,
                    samples,
                    game,
                    ply,
                    black_wins,
                    white_wins,
                    draws,
                    timeouts);
            fflush(log);
        }
    }

    if (state.status == GUNGI_STATUS_BLACK_WIN || (state.status == GUNGI_STATUS_RESIGNED && state.winner == GUNGI_PLAYER_BLACK)) {
        black_wins++;
    } else if (state.status == GUNGI_STATUS_WHITE_WIN || (state.status == GUNGI_STATUS_RESIGNED && state.winner == GUNGI_PLAYER_WHITE)) {
        white_wins++;
    } else if (ply > 0) {
        draws++;
        if (state.status == GUNGI_STATUS_ONGOING && ply >= max_ply) {
            timeouts++;
        }
    }

    fprintf(log,
            "done samples=%d games=%d black_wins=%d white_wins=%d draws=%d timeouts=%d dataset=%s summary_csv=%s candidate_csv=%s\n",
            samples,
            game,
            black_wins,
            white_wins,
            draws,
            timeouts,
            dataset_path,
            summary_csv_path,
            candidate_csv_path);

    fclose(dataset);
    fclose(summary_csv);
    fclose(candidate_csv);
    fclose(log);
    free(candidate_moves);
    free(policy_features);

    printf("VP samples written | samples %d | dataset %s | csv %s | candidates %s | log %s\n",
           samples,
           dataset_path,
           summary_csv_path,
           candidate_csv_path,
           log_path);
    return 0;
}
