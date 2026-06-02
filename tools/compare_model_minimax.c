#include "../src/q_search.h"
#include "../src/ai_core.h"
#include "../src/q_model.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define DEFAULT_MATCHES_PER_SIDE 5
#define DEFAULT_DEPTH 2
#define DEFAULT_MAX_PLY 600
#define DEFAULT_SEED 24680

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

static GungiPlayer opponent(GungiPlayer player)
{
    return player == GUNGI_PLAYER_BLACK ? GUNGI_PLAYER_WHITE : GUNGI_PLAYER_BLACK;
}

static int is_win_status(GungiGameStatus status)
{
    return status == GUNGI_STATUS_BLACK_WIN ||
           status == GUNGI_STATUS_WHITE_WIN ||
           status == GUNGI_STATUS_RESIGNED;
}

static GungiHybridEvalMode parse_mode(const char *value)
{
    if (value != NULL && strcmp(value, "model-only") == 0) {
        return GUNGI_HYBRID_EVAL_MODEL_ONLY;
    }
    return GUNGI_HYBRID_EVAL_HYBRID;
}

static const char *mode_name(GungiHybridEvalMode mode)
{
    return mode == GUNGI_HYBRID_EVAL_MODEL_ONLY ? "model-only" : "hybrid";
}

static int play_match(const GungiQModel *model,
                      GungiPlayer model_player,
                      int depth,
                      int max_ply,
                      int seed,
                      const GungiHybridSearchConfig *config,
                      FILE *log)
{
    GameState state;
    int ply = 0;
    int timed_out = 0;

    srand((unsigned int)seed);
    gungi_init(&state);

    while (state.status == GUNGI_STATUS_ONGOING && ply < max_ply) {
        Move move;
        RulesResult result;

        if (state.current_player == model_player) {
            move = gungi_get_hybrid_ai_move(&state, model, depth, config);
        } else {
            move = gungi_get_ai_move(&state, depth);
        }

        result = gungi_apply_move(&state, move);
        if (!result.ok) {
            fprintf(stderr,
                    "Illegal move at ply %d | model=%s | player=%s | code=%d\n",
                    ply + 1,
                    player_name(model_player),
                    player_name(move.player),
                    result.code);
            return -2;
        }

        ply++;
        timed_out = state.status == GUNGI_STATUS_ONGOING && ply >= max_ply;
        if (timed_out) {
            break;
        }
    }

    fprintf(log,
            "%s,%s,%s,%s,%d,%d\n",
            player_name(model_player),
            player_name(opponent(model_player)),
            status_name(state.status),
            player_name(state.winner),
            ply,
            timed_out);
    fflush(log);

    if (timed_out || state.status == GUNGI_STATUS_DRAW || !is_win_status(state.status)) {
        return 0;
    }

    return state.winner == model_player ? 1 : -1;
}

int main(int argc, char **argv)
{
    const char *model_path = argc > 1 ? argv[1] : "models/v_weights_100000.bin";
    int matches_per_side = argc > 2 ? atoi(argv[2]) : DEFAULT_MATCHES_PER_SIDE;
    int depth = argc > 3 ? atoi(argv[3]) : DEFAULT_DEPTH;
    int max_ply = argc > 4 ? atoi(argv[4]) : DEFAULT_MAX_PLY;
    const char *log_path = argc > 5 ? argv[5] : "model_minimax_vs_original.csv";
    int seed = argc > 6 ? atoi(argv[6]) : DEFAULT_SEED;
    GungiHybridSearchConfig config = GUNGI_HYBRID_SEARCH_DEFAULT_CONFIG;
    GungiQModel model;
    FILE *log;
    int model_wins = 0;
    int original_wins = 0;
    int draws = 0;
    int errors = 0;
    int i;

    if (argc > 7) {
        config.mode = parse_mode(argv[7]);
    }
    if (argc > 8) {
        config.model_scale = (float)atof(argv[8]);
    }
    if (argc > 9) {
        config.draw_penalty = atoi(argv[9]);
    }
    if (argc > 10) {
        config.repetition_penalty = atoi(argv[10]);
    }
    if (argc > 11) {
        config.late_game_penalty = atoi(argv[11]);
    }
    if (argc > 12) {
        config.late_game_start_ply = (unsigned int)atoi(argv[12]);
    }

    if (matches_per_side <= 0) {
        matches_per_side = DEFAULT_MATCHES_PER_SIDE;
    }
    if (depth <= 0) {
        depth = DEFAULT_DEPTH;
    }
    if (max_ply <= 0) {
        max_ply = DEFAULT_MAX_PLY;
    }
    if (config.model_scale <= 0.0f) {
        config.model_scale = GUNGI_HYBRID_SEARCH_DEFAULT_CONFIG.model_scale;
    }

    gungi_q_init(&model);
    if (!gungi_q_load(&model, model_path)) {
        fprintf(stderr, "Failed to load model %s\n", model_path);
        return 1;
    }

    log = fopen(log_path, "w");
    if (log == NULL) {
        fprintf(stderr, "Failed to open log %s\n", log_path);
        return 1;
    }

    fprintf(log, "model_player,original_player,status,winner,ply,timed_out\n");
    fflush(log);

    printf("Compare hybrid minimax vs original minimax | model %s | matches %d per side | depth %d | max ply %d | mode %s | scale %.1f | draw %d | repetition %d | late %d/%u\n",
           model_path,
           matches_per_side,
           depth,
           max_ply,
           mode_name(config.mode),
           config.model_scale,
           config.draw_penalty,
           config.repetition_penalty,
           config.late_game_penalty,
           config.late_game_start_ply);
    fflush(stdout);

    for (i = 0; i < matches_per_side; ++i) {
        int result = play_match(&model, GUNGI_PLAYER_BLACK, depth, max_ply, seed + i, &config, log);
        printf("Match %d/%d | hybrid B vs original W | result %d\n", i + 1, matches_per_side, result);
        fflush(stdout);
        if (result > 0) {
            model_wins++;
        } else if (result < 0) {
            original_wins++;
        } else if (result == 0) {
            draws++;
        } else {
            errors++;
        }
    }

    for (i = 0; i < matches_per_side; ++i) {
        int result = play_match(&model, GUNGI_PLAYER_WHITE, depth, max_ply, seed + matches_per_side + i, &config, log);
        printf("Match %d/%d | original B vs hybrid W | result %d\n", i + 1, matches_per_side, result);
        fflush(stdout);
        if (result > 0) {
            model_wins++;
        } else if (result < 0) {
            original_wins++;
        } else if (result == 0) {
            draws++;
        } else {
            errors++;
        }
    }

    fclose(log);

    printf("Hybrid minimax vs original minimax | mode %s | depth %d | hybrid wins %d | original wins %d | draws %d | errors %d | log %s\n",
           mode_name(config.mode),
           depth,
           model_wins,
           original_wins,
           draws,
           errors,
           log_path);

    return errors == 0 ? 0 : 1;
}
