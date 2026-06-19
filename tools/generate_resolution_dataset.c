#include "../src/q_model.h"
#include "../src/q_search.h"
#include "../src/vp_model.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _OPENMP
#include <omp.h>
#endif

#define DEFAULT_SAMPLES 10000
#define DEFAULT_MAX_PLY 1000
#define DEFAULT_SEED 626262U
#define DEFAULT_DEPTH 2
#define DEFAULT_CANDIDATES 8
#define DEFAULT_HORIZON 40

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
    case GUNGI_STATUS_ONGOING: return "ongoing";
    case GUNGI_STATUS_BLACK_WIN: return "black_win";
    case GUNGI_STATUS_WHITE_WIN: return "white_win";
    case GUNGI_STATUS_DRAW: return "draw";
    case GUNGI_STATUS_RESIGNED: return "resigned";
    default: return "unknown";
    }
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

static int terminal_winner_matches(const GameState *state, GungiPlayer player)
{
    if (state->status == GUNGI_STATUS_BLACK_WIN || state->status == GUNGI_STATUS_WHITE_WIN || state->status == GUNGI_STATUS_RESIGNED) {
        return state->winner == player;
    }
    return 0;
}

static int terminal_winner_is_opponent(const GameState *state, GungiPlayer player)
{
    if (state->status == GUNGI_STATUS_BLACK_WIN || state->status == GUNGI_STATUS_WHITE_WIN || state->status == GUNGI_STATUS_RESIGNED) {
        return state->winner == gungi_opponent(player);
    }
    return 0;
}

static float rollout_resolution_score(const GameState *after,
                                      GungiPlayer mover,
                                      int horizon,
                                      int *out_plies,
                                      int *out_timeout,
                                      GungiPlayer *out_winner,
                                      GungiGameStatus *out_status)
{
    GameState rollout = *after;
    int plies = 0;

    if (out_timeout != NULL) {
        *out_timeout = 0;
    }
    if (out_winner != NULL) {
        *out_winner = GUNGI_PLAYER_NONE;
    }
    if (out_status != NULL) {
        *out_status = rollout.status;
    }

    while (rollout.status == GUNGI_STATUS_ONGOING && plies < horizon) {
        Move moves[GUNGI_MAX_LEGAL_MOVES];
        int count = gungi_generate_legal_moves(&rollout, moves, GUNGI_MAX_LEGAL_MOVES);
        Move move;
        RulesResult result;

        if (count <= 0) {
            break;
        }
        move = moves[rand() % count];
        result = gungi_apply_move(&rollout, move);
        if (!result.ok) {
            break;
        }
        plies++;
    }

    if (out_plies != NULL) {
        *out_plies = plies;
    }
    if (out_winner != NULL) {
        *out_winner = rollout.winner;
    }
    if (out_status != NULL) {
        *out_status = rollout.status;
    }

    if (terminal_winner_matches(&rollout, mover)) {
        return 1.0f - (float)plies / (float)(horizon * 2 + 1);
    }
    if (terminal_winner_is_opponent(&rollout, mover)) {
        return -1.0f + (float)plies / (float)(horizon * 2 + 1);
    }
    if (rollout.status == GUNGI_STATUS_DRAW) {
        return -0.45f;
    }

    if (out_timeout != NULL) {
        *out_timeout = 1;
    }
    return -0.20f;
}

static float direct_resolution_score(const GameState *before,
                                     const GameState *after,
                                     Move move,
                                     const RulesResult *result,
                                     int legal_before,
                                     int legal_after)
{
    int capture_value = gungi_q_capture_value(before, move);
    int before_eval = gungi_evaluate_board(before);
    int after_eval = gungi_evaluate_board(after);
    int material_delta = after_eval - before_eval;
    int reps_after = gungi_count_repetition(after, gungi_position_hash(after));
    float legal_pressure = 0.0f;
    float score = 0.0f;

    if (move.player == GUNGI_PLAYER_WHITE) {
        material_delta = -material_delta;
    }

    if (legal_before > 0) {
        legal_pressure = (float)(legal_before - legal_after) / (float)legal_before;
    }

    if (result->captured_marshal) {
        score += 1.0f;
    }
    if (result->gives_check) {
        score += 0.18f;
    }
    score += clampf((float)capture_value / 2000.0f, 0.0f, 0.35f);
    score += clampf((float)material_delta / 6000.0f, -0.25f, 0.25f);
    score += clampf(legal_pressure, -0.25f, 0.25f);
    if (reps_after > 1) {
        score -= 0.20f * (float)(reps_after - 1);
    }
    if (after->ply_count > 500U) {
        score -= 0.02f * (float)((after->ply_count - 500U) / 100U + 1U);
    }

    return clampf(score, -1.0f, 1.0f);
}

static void write_header(FILE *csv)
{
    int i;

    fprintf(csv,
            "position_id,candidate_index,is_teacher,player,kind,intent,from_x,from_y,to_x,to_y,drop_type,betray_mask,"
            "legal_before,legal_after,capture_value,gives_check,status_after,winner_after,rollout_status,rollout_winner,"
            "rollout_plies,rollout_timeout,direct_score,rollout_score,resolution_target");
    for (i = 0; i < GUNGI_P_FEATURE_COUNT; ++i) {
        fprintf(csv, ",p%d", i);
    }
    fprintf(csv, "\n");
}

int main(int argc, char **argv)
{
    int samples = argc > 1 ? atoi(argv[1]) : DEFAULT_SAMPLES;
    const char *csv_path = argc > 2 ? argv[2] : "resolution_v2a_10000.csv";
    const char *log_path = argc > 3 ? argv[3] : "resolution_v2a_10000.log";
    const char *teacher_model_path = argc > 4 ? argv[4] : "models/v_weights_hybrid_student_50000.bin";
    unsigned int seed = argc > 5 ? (unsigned int)strtoul(argv[5], NULL, 10) : DEFAULT_SEED;
    int max_ply = argc > 6 ? atoi(argv[6]) : DEFAULT_MAX_PLY;
    int teacher_depth = argc > 7 ? atoi(argv[7]) : DEFAULT_DEPTH;
    int max_candidates = argc > 8 ? atoi(argv[8]) : DEFAULT_CANDIDATES;
    int horizon = argc > 9 ? atoi(argv[9]) : DEFAULT_HORIZON;
    GungiQModel teacher;
    GungiHybridSearchConfig config = GUNGI_HYBRID_SEARCH_DEFAULT_CONFIG;
    FILE *csv;
    FILE *log;
    GameState state;
    Move legal_moves[GUNGI_MAX_LEGAL_MOVES];
    Move *candidate_moves;
    int sample;
    int game = 1;
    int ply = 0;
    int wins = 0;
    int losses = 0;
    int draws = 0;
    int timeouts = 0;

    if (samples <= 0) {
        samples = DEFAULT_SAMPLES;
    }
    if (max_ply <= 0) {
        max_ply = DEFAULT_MAX_PLY;
    }
    if (teacher_depth <= 0) {
        teacher_depth = DEFAULT_DEPTH;
    }
    if (max_candidates <= 1) {
        max_candidates = DEFAULT_CANDIDATES;
    }
    if (max_candidates > GUNGI_MAX_LEGAL_MOVES) {
        max_candidates = GUNGI_MAX_LEGAL_MOVES;
    }
    if (horizon <= 0) {
        horizon = DEFAULT_HORIZON;
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

    csv = fopen(csv_path, "w");
    log = fopen(log_path, "w");
    if (csv == NULL || log == NULL) {
        fprintf(stderr, "Failed to open output files\n");
        if (csv != NULL) {
            fclose(csv);
        }
        if (log != NULL) {
            fclose(log);
        }
        return 1;
    }

    candidate_moves = (Move *)calloc((size_t)max_candidates, sizeof(Move));
    if (candidate_moves == NULL) {
        fprintf(stderr, "Failed to allocate candidate buffer\n");
        fclose(csv);
        fclose(log);
        return 1;
    }

    write_header(csv);
    fprintf(log,
            "samples=%d max_ply=%d seed=%u teacher=%s teacher_depth=%d max_candidates=%d horizon=%d policy_features=%d\n",
            samples,
            max_ply,
            seed,
            teacher_model_path,
            teacher_depth,
            max_candidates,
            horizon,
            GUNGI_P_FEATURE_COUNT);

    gungi_init(&state);
    for (sample = 1; sample <= samples; ++sample) {
        GameState before;
        GameState next_state;
        Move teacher_move;
        int teacher_black_score = 0;
        int legal_count;
        int candidate_count;
        int candidate_index;

        if (state.status != GUNGI_STATUS_ONGOING || ply >= max_ply) {
            if (state.status == GUNGI_STATUS_BLACK_WIN || state.status == GUNGI_STATUS_WHITE_WIN || state.status == GUNGI_STATUS_RESIGNED) {
                if (state.winner == GUNGI_PLAYER_BLACK) {
                    wins++;
                } else if (state.winner == GUNGI_PLAYER_WHITE) {
                    losses++;
                }
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
            gungi_init(&state);
            ply = 0;
            sample--;
            continue;
        }

        teacher_move = gungi_get_hybrid_ai_move_scored(&before, &teacher, teacher_depth, &config, &teacher_black_score);
        if (find_move_index(legal_moves, legal_count, teacher_move) < 0) {
            fprintf(stderr, "Teacher move was not legal at sample %d\n", sample);
            fclose(csv);
            fclose(log);
            free(candidate_moves);
            return 1;
        }

        candidate_count = select_candidates(legal_moves, legal_count, teacher_move, candidate_moves, max_candidates);
        for (candidate_index = 0; candidate_index < candidate_count; ++candidate_index) {
            Move candidate = candidate_moves[candidate_index];
            GameState after = before;
            RulesResult result = gungi_apply_move(&after, candidate);
            int legal_after = 0;
            int capture_value = gungi_q_capture_value(&before, candidate);
            int rollout_plies = 0;
            int rollout_timeout = 0;
            GungiPlayer rollout_winner = GUNGI_PLAYER_NONE;
            GungiGameStatus rollout_status = GUNGI_STATUS_ONGOING;
            float direct_score;
            float rollout_score;
            float target;
            float policy_features[GUNGI_P_FEATURE_COUNT];
            int i;

            if (!result.ok) {
                continue;
            }
            if (after.status == GUNGI_STATUS_ONGOING) {
                Move after_moves[GUNGI_MAX_LEGAL_MOVES];
                legal_after = gungi_generate_legal_moves(&after, after_moves, GUNGI_MAX_LEGAL_MOVES);
            }

            direct_score = direct_resolution_score(&before, &after, candidate, &result, legal_count, legal_after);
            rollout_score = rollout_resolution_score(&after, candidate.player, horizon, &rollout_plies, &rollout_timeout, &rollout_winner, &rollout_status);
            target = clampf(0.55f * rollout_score + 0.45f * direct_score, -1.0f, 1.0f);
            gungi_vp_extract_policy_features(policy_features, &before, candidate);

            fprintf(csv,
                    "%d,%d,%d,%s,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%s,%s,%s,%s,%d,%d,%.6f,%.6f,%.6f",
                    sample,
                    candidate_index,
                    candidate_index == 0,
                    player_name(candidate.player),
                    candidate.kind,
                    candidate.intent,
                    candidate.from_x,
                    candidate.from_y,
                    candidate.to_x,
                    candidate.to_y,
                    candidate.drop_type,
                    candidate.betray_mask,
                    legal_count,
                    legal_after,
                    capture_value,
                    result.gives_check,
                    status_name(after.status),
                    player_name(after.winner),
                    status_name(rollout_status),
                    player_name(rollout_winner),
                    rollout_plies,
                    rollout_timeout,
                    direct_score,
                    rollout_score,
                    target);
            for (i = 0; i < GUNGI_P_FEATURE_COUNT; ++i) {
                fprintf(csv, ",%.6f", policy_features[i]);
            }
            fprintf(csv, "\n");
        }

        next_state = before;
        if (!gungi_apply_move(&next_state, teacher_move).ok) {
            fprintf(stderr, "Failed to apply teacher move at sample %d\n", sample);
            fclose(csv);
            fclose(log);
            free(candidate_moves);
            return 1;
        }
        state = next_state;
        ply++;

        if (sample % 1000 == 0 || sample == samples) {
            fprintf(log,
                    "sample=%d/%d game=%d ply=%d draws=%d timeouts=%d\n",
                    sample,
                    samples,
                    game,
                    ply,
                    draws,
                    timeouts);
            fflush(log);
            fflush(csv);
        }
    }

    fprintf(log,
            "done samples=%d games=%d wins=%d losses=%d draws=%d timeouts=%d csv=%s\n",
            samples,
            game,
            wins,
            losses,
            draws,
            timeouts,
            csv_path);

    fclose(csv);
    fclose(log);
    free(candidate_moves);

    printf("Resolution dataset written | samples %d | csv %s | log %s\n", samples, csv_path, log_path);
    return 0;
}
