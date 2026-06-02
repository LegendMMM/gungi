#include "../src/q_search.h"
#include "../src/q_model.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define DEFAULT_TOTAL_GAMES 100
#define DEFAULT_DEPTH 2
#define DEFAULT_MAX_PLY 1000
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

static const char *mode_name(GungiHybridEvalMode mode)
{
    return mode == GUNGI_HYBRID_EVAL_MODEL_ONLY ? "model-only" : "hybrid";
}

static GungiHybridEvalMode parse_mode(const char *value)
{
    if (value != NULL && strcmp(value, "model-only") == 0) {
        return GUNGI_HYBRID_EVAL_MODEL_ONLY;
    }
    return GUNGI_HYBRID_EVAL_HYBRID;
}

static int is_win_status(GungiGameStatus status)
{
    return status == GUNGI_STATUS_BLACK_WIN ||
           status == GUNGI_STATUS_WHITE_WIN ||
           status == GUNGI_STATUS_RESIGNED;
}

static int play_match(const GungiQModel *new_model,
                      const GungiQModel *old_model,
                      GungiPlayer new_player,
                      int game_index,
                      int depth,
                      int max_ply,
                      int seed,
                      const GungiHybridSearchConfig *config,
                      FILE *csv,
                      int *out_ply,
                      int *out_timed_out)
{
    GameState state;
    int ply = 0;
    int timed_out = 0;

    srand((unsigned int)seed);
    gungi_init(&state);

    while (state.status == GUNGI_STATUS_ONGOING && ply < max_ply) {
        const GungiQModel *active_model =
            state.current_player == new_player ? new_model : old_model;
        Move move = gungi_get_hybrid_ai_move(&state, active_model, depth, config);
        RulesResult result = gungi_apply_move(&state, move);

        if (!result.ok) {
            fprintf(stderr,
                    "Illegal move | game=%d ply=%d player=%s code=%d\n",
                    game_index,
                    ply + 1,
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

    fprintf(csv,
            "%d,%s,%s,%s,%s,%d,%d,%d\n",
            game_index,
            player_name(new_player),
            player_name(gungi_opponent(new_player)),
            status_name(state.status),
            player_name(state.winner),
            ply,
            timed_out,
            is_win_status(state.status) && state.winner == new_player ? 1 :
                (timed_out || state.status == GUNGI_STATUS_DRAW || !is_win_status(state.status) ? 0 : -1));
    fflush(csv);

    if (out_ply != NULL) {
        *out_ply = ply;
    }
    if (out_timed_out != NULL) {
        *out_timed_out = timed_out;
    }

    if (timed_out || state.status == GUNGI_STATUS_DRAW || !is_win_status(state.status)) {
        return 0;
    }

    return state.winner == new_player ? 1 : -1;
}

int main(int argc, char **argv)
{
    const char *new_model_path = argc > 1 ? argv[1] : "models/v_weights_hybrid_student_50000.bin";
    const char *old_model_path = argc > 2 ? argv[2] : "models/v_weights_100000.bin";
    int total_games = argc > 3 ? atoi(argv[3]) : DEFAULT_TOTAL_GAMES;
    int depth = argc > 4 ? atoi(argv[4]) : DEFAULT_DEPTH;
    int max_ply = argc > 5 ? atoi(argv[5]) : DEFAULT_MAX_PLY;
    const char *csv_path = argc > 6 ? argv[6] : "q_models_compare.csv";
    int seed = argc > 7 ? atoi(argv[7]) : DEFAULT_SEED;
    GungiHybridSearchConfig config = GUNGI_HYBRID_SEARCH_DEFAULT_CONFIG;
    GungiQModel new_model;
    GungiQModel old_model;
    FILE *csv;
    int new_wins = 0;
    int old_wins = 0;
    int draws = 0;
    int errors = 0;
    int new_black_wins = 0;
    int new_black_losses = 0;
    int new_black_draws = 0;
    int new_white_wins = 0;
    int new_white_losses = 0;
    int new_white_draws = 0;
    long total_ply = 0;
    int timeouts = 0;
    int i;

    if (argc > 8) {
        config.mode = parse_mode(argv[8]);
    }
    if (argc > 9) {
        config.model_scale = (float)atof(argv[9]);
    }
    if (argc > 10) {
        config.draw_penalty = atoi(argv[10]);
    }
    if (argc > 11) {
        config.repetition_penalty = atoi(argv[11]);
    }
    if (argc > 12) {
        config.late_game_penalty = atoi(argv[12]);
    }
    if (argc > 13) {
        config.late_game_start_ply = (unsigned int)atoi(argv[13]);
    }

    if (total_games <= 0) {
        total_games = DEFAULT_TOTAL_GAMES;
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

    gungi_q_init(&new_model);
    gungi_q_init(&old_model);
    if (!gungi_q_load(&new_model, new_model_path)) {
        fprintf(stderr, "Failed to load new model %s\n", new_model_path);
        return 1;
    }
    if (!gungi_q_load(&old_model, old_model_path)) {
        fprintf(stderr, "Failed to load old model %s\n", old_model_path);
        return 1;
    }

    csv = fopen(csv_path, "w");
    if (csv == NULL) {
        fprintf(stderr, "Failed to open CSV %s\n", csv_path);
        return 1;
    }

    fprintf(csv, "game,new_player,old_player,status,winner,ply,timed_out,new_result\n");
    fflush(csv);

    printf("Compare Q models | new %s | old %s | games %d | depth %d | max ply %d | mode %s | scale %.1f | seed %d\n",
           new_model_path,
           old_model_path,
           total_games,
           depth,
           max_ply,
           mode_name(config.mode),
           config.model_scale,
           seed);
    fflush(stdout);

    for (i = 0; i < total_games; ++i) {
        GungiPlayer new_player = (i % 2 == 0) ? GUNGI_PLAYER_BLACK : GUNGI_PLAYER_WHITE;
        int ply = 0;
        int timed_out = 0;
        int result = play_match(&new_model,
                                &old_model,
                                new_player,
                                i + 1,
                                depth,
                                max_ply,
                                seed + i,
                                &config,
                                csv,
                                &ply,
                                &timed_out);

        total_ply += ply;
        timeouts += timed_out;

        if (result > 0) {
            new_wins++;
            if (new_player == GUNGI_PLAYER_BLACK) {
                new_black_wins++;
            } else {
                new_white_wins++;
            }
        } else if (result < 0) {
            if (result == -2) {
                errors++;
            } else {
                old_wins++;
                if (new_player == GUNGI_PLAYER_BLACK) {
                    new_black_losses++;
                } else {
                    new_white_losses++;
                }
            }
        } else {
            draws++;
            if (new_player == GUNGI_PLAYER_BLACK) {
                new_black_draws++;
            } else {
                new_white_draws++;
            }
        }

        printf("Game %d/%d | new %s vs old %s | result %d | ply %d\n",
               i + 1,
               total_games,
               player_name(new_player),
               player_name(gungi_opponent(new_player)),
               result,
               ply);
        fflush(stdout);
    }

    fclose(csv);

    printf("Q model compare complete | mode %s | depth %d | new wins %d | old wins %d | draws %d | errors %d | timeouts %d | avg ply %.2f | new as black %d-%d-%d | new as white %d-%d-%d | csv %s\n",
           mode_name(config.mode),
           depth,
           new_wins,
           old_wins,
           draws,
           errors,
           timeouts,
           total_games > 0 ? (double)total_ply / (double)total_games : 0.0,
           new_black_wins,
           new_black_losses,
           new_black_draws,
           new_white_wins,
           new_white_losses,
           new_white_draws,
           csv_path);

    return errors == 0 ? 0 : 1;
}
