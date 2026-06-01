#include "../src/ai_core.h"

#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#define DEFAULT_GAMES 40
#define MAX_PLY_PER_GAME 400

static int g_value_depth = GUNGI_VALUE_AI_DEFAULT_DEPTH;
static int g_value_top_k = GUNGI_VALUE_AI_DEFAULT_TOP_K;
static int g_max_ply = MAX_PLY_PER_GAME;

typedef enum AiKind {
    AI_OLD_MINIMAX = 0,
    AI_VALUE_MINIMAX = 1
} AiKind;

static const char *player_name(GungiPlayer player)
{
    if (player == GUNGI_PLAYER_BLACK) {
        return "black";
    }
    if (player == GUNGI_PLAYER_WHITE) {
        return "white";
    }
    return "none";
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

static Move choose_move(AiKind kind,
                        const GameState *state,
                        const GungiValueModel *model,
                        GungiAiSearchStats *stats)
{
    if (kind == AI_VALUE_MINIMAX) {
        return gungi_get_value_ai_move(state,
                                       model,
                                       g_value_depth,
                                       g_value_top_k,
                                       stats);
    }

    return gungi_get_ai_move(state, 2);
}

int main(int argc, char **argv)
{
    int games = argc > 1 ? atoi(argv[1]) : DEFAULT_GAMES;
    const char *model_path = argc > 2 ? argv[2] : GUNGI_VALUE_DEFAULT_MODEL_PATH;
    unsigned int seed = argc > 3 ? (unsigned int)strtoul(argv[3], NULL, 10) : 1U;
    GungiValueModel model;
    FILE *csv;
    int value_wins = 0;
    int old_wins = 0;
    int draws = 0;
    double total_value_time = 0.0;
    double total_old_time = 0.0;
    int total_value_moves = 0;
    int total_old_moves = 0;
    long long total_nodes = 0;
    long long total_pruned_root = 0;
    int game_index;

    if (games <= 0) {
        games = DEFAULT_GAMES;
    }
    if (argc > 4) {
        g_value_depth = atoi(argv[4]);
        if (g_value_depth <= 0) {
            g_value_depth = GUNGI_VALUE_AI_DEFAULT_DEPTH;
        }
    }
    if (argc > 5) {
        g_value_top_k = atoi(argv[5]);
        if (g_value_top_k <= 0) {
            g_value_top_k = GUNGI_VALUE_AI_DEFAULT_TOP_K;
        }
    }
    if (argc > 6) {
        g_max_ply = atoi(argv[6]);
        if (g_max_ply <= 0) {
            g_max_ply = MAX_PLY_PER_GAME;
        }
    }

    srand(seed);
    gungi_value_init(&model);
    if (!gungi_value_load(&model, model_path)) {
        fprintf(stderr, "Failed to load value model %s\n", model_path);
        return 1;
    }

    csv = fopen("value_ai_eval.csv", "w");
    if (csv == NULL) {
        fprintf(stderr, "Failed to open value_ai_eval.csv\n");
        return 1;
    }
    fprintf(csv, "game,value_player,status,winner,ply,value_time,old_time,value_nodes,value_root_moves,value_pruned_root\n");

    for (game_index = 0; game_index < games; ++game_index) {
        GameState state;
        GungiPlayer value_player = (game_index % 2 == 0) ? GUNGI_PLAYER_BLACK : GUNGI_PLAYER_WHITE;
        int ply = 0;
        double game_value_time = 0.0;
        double game_old_time = 0.0;
        long long game_nodes = 0;
        long long game_pruned_root = 0;
        int game_root_moves = 0;

        gungi_init(&state);

        while (state.status == GUNGI_STATUS_ONGOING && ply < g_max_ply) {
            AiKind kind = state.current_player == value_player ? AI_VALUE_MINIMAX : AI_OLD_MINIMAX;
            GungiAiSearchStats stats;
            clock_t start = clock();
            Move move = choose_move(kind, &state, &model, &stats);
            clock_t end = clock();
            double elapsed = (double)(end - start) / CLOCKS_PER_SEC;
            RulesResult result = gungi_apply_move(&state, move);

            if (!result.ok) {
                fprintf(stderr, "Illegal move in game %d ply %d\n", game_index + 1, ply + 1);
                fclose(csv);
                return 1;
            }

            if (kind == AI_VALUE_MINIMAX) {
                game_value_time += elapsed;
                total_value_time += elapsed;
                total_value_moves++;
                game_nodes += stats.nodes;
                game_pruned_root += stats.pruned_root_moves;
                game_root_moves += stats.root_moves;
            } else {
                game_old_time += elapsed;
                total_old_time += elapsed;
                total_old_moves++;
            }

            ply++;
        }

        if (state.status == GUNGI_STATUS_ONGOING) {
            draws++;
        } else if (state.status == GUNGI_STATUS_DRAW) {
            draws++;
        } else if (state.winner == value_player) {
            value_wins++;
        } else if (state.winner == gungi_opponent(value_player)) {
            old_wins++;
        } else {
            draws++;
        }

        total_nodes += game_nodes;
        total_pruned_root += game_pruned_root;
        fprintf(csv,
                "%d,%s,%s,%s,%d,%.6f,%.6f,%lld,%d,%lld\n",
                game_index + 1,
                player_name(value_player),
                status_name(state.status),
                player_name(state.winner),
                ply,
                game_value_time,
                game_old_time,
                game_nodes,
                game_root_moves,
                game_pruned_root);

        printf("Game %d/%d | value %s | status %s | winner %s | ply %d\n",
               game_index + 1,
               games,
               player_name(value_player),
               status_name(state.status),
               player_name(state.winner),
               ply);
    }

    fclose(csv);
    printf("Value wins: %d | Old wins: %d | Draws/timeouts: %d\n", value_wins, old_wins, draws);
    printf("Avg value think: %.6f s | Avg old think: %.6f s\n",
           total_value_moves > 0 ? total_value_time / (double)total_value_moves : 0.0,
           total_old_moves > 0 ? total_old_time / (double)total_old_moves : 0.0);
    printf("Total value nodes: %lld | Total root pruned: %lld | log value_ai_eval.csv\n",
           total_nodes,
           total_pruned_root);
    return 0;
}
