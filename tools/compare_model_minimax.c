#include "../src/q_search.h"
#include "../src/vp_model.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _OPENMP
#include <omp.h>
#endif

#define DEFAULT_GAMES_PER_SIDE 2
#define DEFAULT_MAX_PLY 1000
#define DEFAULT_SEED 24680U
#define DEFAULT_DEPTH 2

typedef enum CompareMode {
    COMPARE_MODE_VP_POLICY = 0,
    COMPARE_MODE_VP_HYBRID = 1,
    COMPARE_MODE_VP_SIDE_HYBRID = 2
} CompareMode;

typedef struct CompareTotals {
    int wins;
    int losses;
    int draws;
    int timeouts;
    int black_wins;
    int black_losses;
    int black_draws;
    int white_wins;
    int white_losses;
    int white_draws;
} CompareTotals;

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

static int parse_mode(const char *text, CompareMode *out_mode)
{
    if (strcmp(text, "vp-policy") == 0 || strcmp(text, "policy") == 0) {
        *out_mode = COMPARE_MODE_VP_POLICY;
        return 1;
    }
    if (strcmp(text, "vp-hybrid") == 0 || strcmp(text, "hybrid") == 0 || strcmp(text, "vp") == 0) {
        *out_mode = COMPARE_MODE_VP_HYBRID;
        return 1;
    }
    if (strcmp(text, "vp-side-hybrid") == 0 || strcmp(text, "side-hybrid") == 0 || strcmp(text, "side") == 0) {
        *out_mode = COMPARE_MODE_VP_SIDE_HYBRID;
        return 1;
    }
    return 0;
}

static const char *mode_name(CompareMode mode)
{
    if (mode == COMPARE_MODE_VP_POLICY) {
        return "vp-policy";
    }
    if (mode == COMPARE_MODE_VP_SIDE_HYBRID) {
        return "vp-side-hybrid";
    }
    return "vp-hybrid";
}

static Move get_model_move(const GameState *state,
                           const GungiVPModel *model,
                           const GungiVPModel *black_policy_model,
                           const GungiVPModel *white_policy_model,
                           CompareMode mode,
                           int depth,
                           const GungiHybridSearchConfig *config)
{
    if (mode == COMPARE_MODE_VP_POLICY) {
        return gungi_get_vp_move(state, model, 0.0f);
    }
    if (mode == COMPARE_MODE_VP_SIDE_HYBRID) {
        return gungi_get_vp_side_policy_hybrid_ai_move(state,
                                                       model,
                                                       black_policy_model,
                                                       white_policy_model,
                                                       depth,
                                                       config);
    }
    return gungi_get_vp_hybrid_ai_move(state, model, depth, config);
}

static void update_totals(CompareTotals *totals, GungiPlayer model_side, GungiGameStatus status, GungiPlayer winner, int timed_out)
{
    int won = 0;
    int lost = 0;
    int drawn = 0;

    if (timed_out || status == GUNGI_STATUS_DRAW || winner == GUNGI_PLAYER_NONE) {
        drawn = 1;
    } else if (winner == model_side) {
        won = 1;
    } else {
        lost = 1;
    }

    totals->wins += won;
    totals->losses += lost;
    totals->draws += drawn;
    totals->timeouts += timed_out ? 1 : 0;

    if (model_side == GUNGI_PLAYER_BLACK) {
        totals->black_wins += won;
        totals->black_losses += lost;
        totals->black_draws += drawn;
    } else {
        totals->white_wins += won;
        totals->white_losses += lost;
        totals->white_draws += drawn;
    }
}

static int play_game(FILE *csv,
                     int game_index,
                     GungiPlayer model_side,
                     const GungiVPModel *model,
                     const GungiVPModel *black_policy_model,
                     const GungiVPModel *white_policy_model,
                     CompareMode mode,
                     int depth,
                     int max_ply,
                     unsigned int seed,
                     CompareTotals *totals)
{
    GungiHybridSearchConfig config = GUNGI_HYBRID_SEARCH_DEFAULT_CONFIG;
    GameState state;
    int ply = 0;
    int timed_out = 0;

    srand(seed);
    gungi_init(&state);

    while (state.status == GUNGI_STATUS_ONGOING && ply < max_ply) {
        Move move;
        RulesResult result;

        if (state.current_player == model_side) {
            move = get_model_move(&state, model, black_policy_model, white_policy_model, mode, depth, &config);
        } else {
            move = gungi_get_ai_move(&state, 2);
        }

        result = gungi_apply_move(&state, move);
        if (!result.ok) {
            fprintf(stderr, "Illegal move in game %d ply %d: code %d\n", game_index, ply + 1, result.code);
            return 0;
        }
        ply++;
    }

    timed_out = state.status == GUNGI_STATUS_ONGOING && ply >= max_ply;
    update_totals(totals, model_side, state.status, state.winner, timed_out);

    fprintf(csv,
            "%d,%u,%s,%s,%d,%d,%s,%s,%d,%d,%d,%d\n",
            game_index,
            seed,
            mode_name(mode),
            player_name(model_side),
            depth,
            max_ply,
            status_name(state.status),
            player_name(state.winner),
            ply,
            timed_out,
            state.winner == model_side && !timed_out,
            state.winner != model_side && state.winner != GUNGI_PLAYER_NONE && !timed_out);
    fflush(csv);
    return 1;
}

int main(int argc, char **argv)
{
    const char *model_path = argc > 1 ? argv[1] : GUNGI_VP_DEFAULT_MODEL_PATH;
    const char *mode_arg = argc > 2 ? argv[2] : "vp-hybrid";
    int games_per_side = argc > 3 ? atoi(argv[3]) : DEFAULT_GAMES_PER_SIDE;
    int max_ply = argc > 4 ? atoi(argv[4]) : DEFAULT_MAX_PLY;
    unsigned int seed = argc > 5 ? (unsigned int)strtoul(argv[5], NULL, 10) : DEFAULT_SEED;
    const char *csv_path = argc > 6 ? argv[6] : "vp_vs_minimax2.csv";
    const char *log_path = argc > 7 ? argv[7] : "vp_vs_minimax2.log";
    int depth = argc > 8 ? atoi(argv[8]) : DEFAULT_DEPTH;
    const char *black_policy_path = argc > 9 ? argv[9] : model_path;
    const char *white_policy_path = argc > 10 ? argv[10] : model_path;
    CompareMode mode;
    GungiVPModel model;
    GungiVPModel black_policy_model;
    GungiVPModel white_policy_model;
    CompareTotals totals;
    FILE *csv;
    FILE *log;
    int game_index = 1;
    int i;

#ifdef _OPENMP
    omp_set_num_threads(1);
#endif

    if (!parse_mode(mode_arg, &mode)) {
        fprintf(stderr, "Unknown compare mode %s. Use vp-policy or vp-hybrid.\n", mode_arg);
        return 1;
    }
    if (games_per_side <= 0) {
        games_per_side = DEFAULT_GAMES_PER_SIDE;
    }
    if (max_ply <= 0) {
        max_ply = DEFAULT_MAX_PLY;
    }
    if (depth <= 0) {
        depth = DEFAULT_DEPTH;
    }

    gungi_vp_init(&model);
    if (!gungi_vp_load(&model, model_path)) {
        fprintf(stderr, "Failed to load VP model %s\n", model_path);
        return 1;
    }
    if (!gungi_vp_weights_are_finite(&model)) {
        fprintf(stderr, "Model contains NaN or Inf weights: %s\n", model_path);
        return 1;
    }
    black_policy_model = model;
    white_policy_model = model;
    if (mode == COMPARE_MODE_VP_SIDE_HYBRID) {
        gungi_vp_init(&black_policy_model);
        if (!gungi_vp_load(&black_policy_model, black_policy_path)) {
            fprintf(stderr, "Failed to load black policy VP model %s\n", black_policy_path);
            return 1;
        }
        if (!gungi_vp_weights_are_finite(&black_policy_model)) {
            fprintf(stderr, "Black policy model contains NaN or Inf weights: %s\n", black_policy_path);
            return 1;
        }
        gungi_vp_init(&white_policy_model);
        if (!gungi_vp_load(&white_policy_model, white_policy_path)) {
            fprintf(stderr, "Failed to load white policy VP model %s\n", white_policy_path);
            return 1;
        }
        if (!gungi_vp_weights_are_finite(&white_policy_model)) {
            fprintf(stderr, "White policy model contains NaN or Inf weights: %s\n", white_policy_path);
            return 1;
        }
    }

    csv = fopen(csv_path, "w");
    log = fopen(log_path, "w");
    if (csv == NULL || log == NULL) {
        fprintf(stderr, "Failed to open compare outputs\n");
        if (csv != NULL) {
            fclose(csv);
        }
        if (log != NULL) {
            fclose(log);
        }
        return 1;
    }

    memset(&totals, 0, sizeof(totals));
    fprintf(csv, "game,game_seed,mode,model_side,model_depth,max_ply,status,winner,ply,timed_out,model_win,model_loss\n");
    fprintf(log,
            "model=%s mode=%s games_per_side=%d max_ply=%d seed=%u depth=%d opponent=minimax2 black_policy=%s white_policy=%s\n",
            model_path,
            mode_name(mode),
            games_per_side,
            max_ply,
            seed,
            depth,
            black_policy_path,
            white_policy_path);

    for (i = 0; i < games_per_side; ++i) {
        if (!play_game(csv,
                       game_index,
                       GUNGI_PLAYER_BLACK,
                       &model,
                       &black_policy_model,
                       &white_policy_model,
                       mode,
                       depth,
                       max_ply,
                       seed + (unsigned int)game_index * 997U,
                       &totals)) {
            fclose(csv);
            fclose(log);
            return 1;
        }
        game_index++;
    }
    for (i = 0; i < games_per_side; ++i) {
        if (!play_game(csv,
                       game_index,
                       GUNGI_PLAYER_WHITE,
                       &model,
                       &black_policy_model,
                       &white_policy_model,
                       mode,
                       depth,
                       max_ply,
                       seed + (unsigned int)game_index * 997U,
                       &totals)) {
            fclose(csv);
            fclose(log);
            return 1;
        }
        game_index++;
    }

    fprintf(log,
            "summary wins=%d losses=%d draws=%d timeouts=%d black=%d-%d-%d white=%d-%d-%d\n",
            totals.wins,
            totals.losses,
            totals.draws,
            totals.timeouts,
            totals.black_wins,
            totals.black_losses,
            totals.black_draws,
            totals.white_wins,
            totals.white_losses,
            totals.white_draws);

    fclose(csv);
    fclose(log);
    printf("Compare finished | W %d L %d D %d T %d | csv %s | log %s\n",
           totals.wins,
           totals.losses,
           totals.draws,
           totals.timeouts,
           csv_path,
           log_path);
    return 0;
}
