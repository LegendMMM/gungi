#include "../src/q_model.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#ifdef _WIN32
#include <direct.h>
#else
#include <sys/stat.h>
#endif

#define DEFAULT_EPISODES 10000
#define MAX_PLY_PER_GAME 600
#define START_EPSILON 0.30f
#define END_EPSILON 0.05f

static void ensure_models_dir(void)
{
#ifdef _WIN32
    _mkdir("models");
#else
    mkdir("models", 0755);
#endif
}

static float epsilon_for_episode(int episode, int total_episodes)
{
    float t;

    if (total_episodes <= 1) {
        return END_EPSILON;
    }

    t = (float)(episode - 1) / (float)(total_episodes - 1);
    return START_EPSILON + (END_EPSILON - START_EPSILON) * t;
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

static void print_weights(const GungiQModel *model)
{
    int i;

    if (model == NULL) {
        return;
    }

    printf("Weights:");
    for (i = 0; i < GUNGI_V_FEATURE_COUNT; ++i) {
        printf(" w%d=%.6f", i, model->weights[i]);
    }
    printf("\n");
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

static void write_trace_header(FILE *trace)
{
    if (trace == NULL) {
        return;
    }

    fprintf(trace,
            "episode,ply,player,kind,intent,from_x,from_y,to_x,to_y,drop_type,capture_value,gives_check,status,winner,timed_out,reward,target,value_before,value_after\n");
}

static void write_trace_row(FILE *trace,
                            int episode,
                            int ply,
                            Move move,
                            int capture_value,
                            const RulesResult *result,
                            const GameState *state,
                            int timed_out,
                            float reward,
                            float target,
                            float value_before,
                            float value_after)
{
    if (trace == NULL || result == NULL || state == NULL) {
        return;
    }

    fprintf(trace,
            "%d,%d,%s,%d,%d,%d,%d,%d,%d,%d,%d,%d,%s,%s,%d,%.6f,%.6f,%.6f,%.6f\n",
            episode,
            ply,
            player_name(move.player),
            move.kind,
            move.intent,
            move.from_x,
            move.from_y,
            move.to_x,
            move.to_y,
            move.drop_type,
            capture_value,
            result->gives_check,
            status_name(state->status),
            player_name(state->winner),
            timed_out,
            reward,
            target,
            value_before,
            value_after);
}

int main(int argc, char **argv)
{
    const GungiQProfileConfig *config = gungi_q_profile_config(GUNGI_Q_PROFILE_BALANCED);
    GungiQModel model;
    GungiQModel verify_model;
    int episodes = DEFAULT_EPISODES;
    int black_wins = 0;
    int white_wins = 0;
    int draws = 0;
    int timeouts = 0;
    int total_ply = 0;
    int episode;
    const char *trace_path = getenv("GUNGI_Q_TRACE");
    FILE *trace = NULL;

    if (argc > 1) {
        episodes = atoi(argv[1]);
        if (episodes <= 0) {
            episodes = DEFAULT_EPISODES;
        }
    }

    srand((unsigned int)time(NULL));
    gungi_q_init(&model);
    model.profile = config->profile;

    if (trace_path != NULL && trace_path[0] != '\0') {
        trace = fopen(trace_path, "w");
        if (trace == NULL) {
            fprintf(stderr, "Failed to open trace log %s\n", trace_path);
            return 1;
        }
        write_trace_header(trace);
    }

    printf("Training %d self-play games with %s after-state value profile...\n", episodes, config->name);

    for (episode = 1; episode <= episodes; ++episode) {
        GameState state;
        int ply = 0;
        int timed_out = 0;
        float epsilon = epsilon_for_episode(episode, episodes);

        gungi_init(&state);

        while (state.status == GUNGI_STATUS_ONGOING && ply < MAX_PLY_PER_GAME) {
            GameState before = state;
            Move move = gungi_get_q_move(&before, &model, epsilon);
            RulesResult result;
            float reward;
            float target;
            float value_before = gungi_v_evaluate(&model, &before);
            float value_after;
            int capture_value = gungi_q_capture_value(&before, move);

            result = gungi_apply_move(&state, move);
            if (!result.ok) {
                fprintf(stderr, "Illegal generated move at episode %d ply %d: code %d\n", episode, ply + 1, result.code);
                if (trace != NULL) {
                    fclose(trace);
                }
                return 1;
            }

            ply++;
            timed_out = state.status == GUNGI_STATUS_ONGOING && ply >= MAX_PLY_PER_GAME;
            reward = gungi_v_immediate_reward(&before, move, &state, &result, timed_out);

            if (state.status == GUNGI_STATUS_ONGOING && !timed_out) {
                value_after = gungi_v_evaluate(&model, &state);
                target = reward - config->gamma * value_after;
            } else {
                value_after = 0.0f;
                target = reward;
            }

            write_trace_row(trace,
                            episode,
                            ply,
                            move,
                            capture_value,
                            &result,
                            &state,
                            timed_out,
                            reward,
                            target,
                            value_before,
                            value_after);

            gungi_v_update(&model, &before, target, config->learning_rate);

            if (timed_out) {
                break;
            }
        }

        total_ply += ply;
        if (state.status == GUNGI_STATUS_BLACK_WIN || (state.status == GUNGI_STATUS_RESIGNED && state.winner == GUNGI_PLAYER_BLACK)) {
            black_wins++;
        } else if (state.status == GUNGI_STATUS_WHITE_WIN || (state.status == GUNGI_STATUS_RESIGNED && state.winner == GUNGI_PLAYER_WHITE)) {
            white_wins++;
        } else {
            draws++;
            if (timed_out) {
                timeouts++;
            }
        }

        if (episode % 500 == 0 || episode == episodes) {
            float avg_ply = (float)total_ply / (float)episode;
            printf("Episode %d/%d | B %d W %d D %d T %d | avg ply %.1f | epsilon %.3f\n",
                   episode, episodes, black_wins, white_wins, draws, timeouts, avg_ply, epsilon);
        }
    }

    if (trace != NULL) {
        fclose(trace);
    }

    ensure_models_dir();
    if (!gungi_q_save(&model, GUNGI_V_DEFAULT_MODEL_PATH)) {
        fprintf(stderr, "Failed to save %s\n", GUNGI_V_DEFAULT_MODEL_PATH);
        return 1;
    }

    gungi_q_init(&verify_model);
    if (!gungi_q_load(&verify_model, GUNGI_V_DEFAULT_MODEL_PATH)) {
        fprintf(stderr, "Failed to reload %s\n", GUNGI_V_DEFAULT_MODEL_PATH);
        return 1;
    }

    if (weight_checksum(&verify_model) <= 0.0f) {
        fprintf(stderr, "Reloaded model weights are all zero\n");
        return 1;
    }

    printf("Saved %s | weight checksum %.6f\n", GUNGI_V_DEFAULT_MODEL_PATH, weight_checksum(&verify_model));
    print_weights(&verify_model);
    return 0;
}
