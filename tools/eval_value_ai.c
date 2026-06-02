#include "../src/ai_core.h"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define DEFAULT_GAMES 100
#define DEFAULT_MAX_PLY_PER_GAME 3000
#define DEFAULT_OPENING_MIN 6
#define DEFAULT_OPENING_MAX 12
#define DEFAULT_SEED_COUNT 4
#define DEFAULT_OUTPUT_PATH "value_ai_eval_v2.csv"
#define DEFAULT_SUMMARY_PATH "value_ai_eval_v2_summary.csv"

typedef enum AiKind {
    AI_OLD_MINIMAX = 0,
    AI_VALUE_MINIMAX = 1
} AiKind;

typedef struct EvalConfig {
    int games;
    const char *model_path;
    unsigned int base_seed;
    int value_depth;
    int value_top_k;
    int max_ply;
    int opening_min;
    int opening_max;
    int seed_count;
    const char *output_path;
    const char *summary_path;
} EvalConfig;

typedef struct GameResult {
    GungiGameStatus status;
    GungiPlayer winner;
    int ply;
    int timeout;
    int value_moves;
    int old_moves;
    double value_time;
    double old_time;
    long long value_nodes;
    long long value_leaves;
    long long value_cutoffs;
    long long value_pruned_root;
    int value_root_moves;
    int value_searched_root;
    int value_tactical_root;
    int value_searched_tactical_root;
} GameResult;

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

static int read_int_arg(int argc, char **argv, int *index, int fallback)
{
    if (*index + 1 >= argc) {
        return fallback;
    }
    (*index)++;
    return atoi(argv[*index]);
}

static const char *read_string_arg(int argc, char **argv, int *index, const char *fallback)
{
    if (*index + 1 >= argc) {
        return fallback;
    }
    (*index)++;
    return argv[*index];
}

static void parse_args(int argc, char **argv, EvalConfig *config)
{
    int positional = 0;
    int i;

    config->games = DEFAULT_GAMES;
    config->model_path = GUNGI_VALUE_DEFAULT_MODEL_PATH;
    config->base_seed = 1U;
    config->value_depth = GUNGI_VALUE_AI_DEFAULT_DEPTH;
    config->value_top_k = GUNGI_VALUE_AI_DEFAULT_TOP_K;
    config->max_ply = DEFAULT_MAX_PLY_PER_GAME;
    config->opening_min = DEFAULT_OPENING_MIN;
    config->opening_max = DEFAULT_OPENING_MAX;
    config->seed_count = DEFAULT_SEED_COUNT;
    config->output_path = DEFAULT_OUTPUT_PATH;
    config->summary_path = DEFAULT_SUMMARY_PATH;

    for (i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--games") == 0) {
            config->games = read_int_arg(argc, argv, &i, config->games);
        } else if (strcmp(argv[i], "--model") == 0) {
            config->model_path = read_string_arg(argc, argv, &i, config->model_path);
        } else if (strcmp(argv[i], "--seed") == 0) {
            config->base_seed = (unsigned int)strtoul(read_string_arg(argc, argv, &i, "1"), NULL, 10);
        } else if (strcmp(argv[i], "--seed-count") == 0) {
            config->seed_count = read_int_arg(argc, argv, &i, config->seed_count);
        } else if (strcmp(argv[i], "--depth") == 0) {
            config->value_depth = read_int_arg(argc, argv, &i, config->value_depth);
        } else if (strcmp(argv[i], "--top-k") == 0) {
            config->value_top_k = read_int_arg(argc, argv, &i, config->value_top_k);
        } else if (strcmp(argv[i], "--max-ply") == 0) {
            config->max_ply = read_int_arg(argc, argv, &i, config->max_ply);
        } else if (strcmp(argv[i], "--opening-min") == 0) {
            config->opening_min = read_int_arg(argc, argv, &i, config->opening_min);
        } else if (strcmp(argv[i], "--opening-max") == 0) {
            config->opening_max = read_int_arg(argc, argv, &i, config->opening_max);
        } else if (strcmp(argv[i], "--opening-plies") == 0) {
            config->opening_min = read_int_arg(argc, argv, &i, config->opening_min);
            config->opening_max = config->opening_min;
        } else if (strcmp(argv[i], "--output") == 0) {
            config->output_path = read_string_arg(argc, argv, &i, config->output_path);
        } else if (strcmp(argv[i], "--summary") == 0) {
            config->summary_path = read_string_arg(argc, argv, &i, config->summary_path);
        } else if (argv[i][0] != '-') {
            if (positional == 0) {
                config->games = atoi(argv[i]);
            } else if (positional == 1) {
                config->model_path = argv[i];
            } else if (positional == 2) {
                config->base_seed = (unsigned int)strtoul(argv[i], NULL, 10);
            } else if (positional == 3) {
                config->value_depth = atoi(argv[i]);
            } else if (positional == 4) {
                config->value_top_k = atoi(argv[i]);
            } else if (positional == 5) {
                config->max_ply = atoi(argv[i]);
            } else if (positional == 6) {
                config->opening_min = atoi(argv[i]);
            } else if (positional == 7) {
                config->opening_max = atoi(argv[i]);
            } else if (positional == 8) {
                config->seed_count = atoi(argv[i]);
            } else if (positional == 9) {
                config->output_path = argv[i];
            }
            positional++;
        }
    }

    if (config->games <= 0) {
        config->games = DEFAULT_GAMES;
    }
    if (config->value_depth <= 0) {
        config->value_depth = GUNGI_VALUE_AI_DEFAULT_DEPTH;
    }
    if (config->value_top_k <= 0) {
        config->value_top_k = GUNGI_VALUE_AI_DEFAULT_TOP_K;
    }
    if (config->max_ply <= 0) {
        config->max_ply = DEFAULT_MAX_PLY_PER_GAME;
    }
    if (config->opening_min < 0) {
        config->opening_min = 0;
    }
    if (config->opening_max < config->opening_min) {
        config->opening_max = config->opening_min;
    }
    if (config->seed_count <= 0) {
        config->seed_count = 1;
    }
    if (config->seed_count > (config->games + 1) / 2) {
        config->seed_count = (config->games + 1) / 2;
    }
}

static Move choose_greedy_move(const GameState *state)
{
    Move moves[GUNGI_MAX_LEGAL_MOVES];
    int count = gungi_generate_legal_moves(state, moves, GUNGI_MAX_LEGAL_MOVES);
    int is_black = state != NULL && state->current_player == GUNGI_PLAYER_BLACK;
    int best_score = is_black ? -2147483647 : 2147483647;
    Move best;
    int i;

    if (state == NULL || count == 0) {
        return gungi_make_resign(state != NULL ? state->current_player : GUNGI_PLAYER_BLACK);
    }

    best = moves[0];
    for (i = 0; i < count; ++i) {
        GameState next = *state;
        int score;
        if (!gungi_apply_move(&next, moves[i]).ok) {
            continue;
        }
        score = gungi_evaluate_board(&next);
        if ((is_black && score > best_score) || (!is_black && score < best_score)) {
            best_score = score;
            best = moves[i];
        }
    }

    return best;
}

static Move choose_opening_move(const GameState *state)
{
    if (rand() % 100 < 70) {
        return gungi_get_random_move(state);
    }
    return choose_greedy_move(state);
}

static int build_opening(GameState *out,
                         int min_plies,
                         int max_plies,
                         int *requested,
                         int *applied)
{
    int attempts;

    for (attempts = 0; attempts < 50; ++attempts) {
        GameState state;
        int plies;
        int ply;

        gungi_init(&state);
        plies = min_plies;
        if (max_plies > min_plies) {
            plies += rand() % (max_plies - min_plies + 1);
        }

        for (ply = 0; ply < plies && state.status == GUNGI_STATUS_ONGOING; ++ply) {
            Move move = choose_opening_move(&state);
            RulesResult result = gungi_apply_move(&state, move);
            if (!result.ok) {
                break;
            }
        }

        if (state.status == GUNGI_STATUS_ONGOING) {
            *out = state;
            if (requested != NULL) {
                *requested = plies;
            }
            if (applied != NULL) {
                *applied = ply;
            }
            return 1;
        }
    }

    gungi_init(out);
    if (requested != NULL) {
        *requested = 0;
    }
    if (applied != NULL) {
        *applied = 0;
    }
    return 0;
}

static Move choose_move(AiKind kind,
                        const GameState *state,
                        const GungiValueModel *model,
                        const EvalConfig *config,
                        GungiAiSearchStats *stats)
{
    if (kind == AI_VALUE_MINIMAX) {
        return gungi_get_value_ai_move(state,
                                       model,
                                       config->value_depth,
                                       config->value_top_k,
                                       stats);
    }

    return gungi_get_ai_move(state, 2);
}

static int run_game(const GameState *opening,
                    GungiPlayer value_player,
                    const GungiValueModel *model,
                    const EvalConfig *config,
                    GameResult *out)
{
    GameState state = *opening;
    int ply = 0;

    memset(out, 0, sizeof(*out));

    while (state.status == GUNGI_STATUS_ONGOING && ply < config->max_ply) {
        AiKind kind = state.current_player == value_player ? AI_VALUE_MINIMAX : AI_OLD_MINIMAX;
        GungiAiSearchStats stats;
        clock_t start = clock();
        Move move = choose_move(kind, &state, model, config, &stats);
        clock_t end = clock();
        double elapsed = (double)(end - start) / CLOCKS_PER_SEC;
        RulesResult result = gungi_apply_move(&state, move);

        if (!result.ok) {
            return 0;
        }

        if (kind == AI_VALUE_MINIMAX) {
            out->value_time += elapsed;
            out->value_moves++;
            out->value_nodes += stats.nodes;
            out->value_leaves += stats.leaves;
            out->value_cutoffs += stats.cutoffs;
            out->value_pruned_root += stats.pruned_root_moves;
            out->value_root_moves += stats.root_moves;
            out->value_searched_root += stats.searched_root_moves;
            out->value_tactical_root += stats.tactical_root_moves;
            out->value_searched_tactical_root += stats.searched_tactical_root_moves;
        } else {
            out->old_time += elapsed;
            out->old_moves++;
        }

        ply++;
    }

    out->status = state.status;
    out->winner = state.winner;
    out->ply = ply;
    out->timeout = state.status == GUNGI_STATUS_ONGOING;
    return 1;
}

int main(int argc, char **argv)
{
    EvalConfig config;
    GungiValueModel model;
    FILE *csv;
    FILE *summary;
    int value_wins = 0;
    int old_wins = 0;
    int draws = 0;
    int timeouts = 0;
    int total_ply = 0;
    int total_value_moves = 0;
    int total_old_moves = 0;
    double total_value_time = 0.0;
    double total_old_time = 0.0;
    long long total_nodes = 0;
    long long total_pruned_root = 0;
    long long total_tactical_root = 0;
    long long total_searched_tactical_root = 0;
    int global_game = 0;
    int total_pairs;
    int seed_index;

    parse_args(argc, argv, &config);
    total_pairs = (config.games + 1) / 2;

    gungi_value_init(&model);
    if (!gungi_value_load(&model, config.model_path)) {
        fprintf(stderr, "Failed to load value model %s\n", config.model_path);
        return 1;
    }

    csv = fopen(config.output_path, "w");
    if (csv == NULL) {
        fprintf(stderr, "Failed to open %s\n", config.output_path);
        return 1;
    }
    summary = fopen(config.summary_path, "w");
    if (summary == NULL) {
        fprintf(stderr, "Failed to open %s\n", config.summary_path);
        fclose(csv);
        return 1;
    }

    fprintf(csv,
            "seed_index,seed,game,opening_id,opening_ply_requested,opening_ply_applied,opening_hash,value_player,status,winner,ply,timeout,depth,top_k,value_time,old_time,value_moves,old_moves,value_nodes,value_leaves,value_cutoffs,value_root_moves,value_searched_root,value_pruned_root,value_tactical_root,value_searched_tactical_root\n");

    for (seed_index = 0; seed_index < config.seed_count && global_game < config.games; ++seed_index) {
        unsigned int seed = config.base_seed + (unsigned int)seed_index * 1000003U;
        int pairs_for_seed = total_pairs / config.seed_count;
        int remainder = total_pairs % config.seed_count;
        int pair_index;

        if (seed_index < remainder) {
            pairs_for_seed++;
        }
        srand(seed);

        for (pair_index = 0; pair_index < pairs_for_seed && global_game < config.games; ++pair_index) {
            GameState opening;
            int requested = 0;
            int applied = 0;
            uint64_t opening_hash;
            int opening_id = global_game / 2 + 1;
            int side;

            build_opening(&opening, config.opening_min, config.opening_max, &requested, &applied);
            opening_hash = gungi_position_hash(&opening);

            for (side = 0; side < 2 && global_game < config.games; ++side) {
                GungiPlayer value_player = side == 0 ? GUNGI_PLAYER_BLACK : GUNGI_PLAYER_WHITE;
                GameResult result;

                if (!run_game(&opening, value_player, &model, &config, &result)) {
                    fprintf(stderr, "Illegal move in game %d\n", global_game + 1);
                    fclose(summary);
                    fclose(csv);
                    return 1;
                }

                if (result.timeout || result.status == GUNGI_STATUS_DRAW) {
                    draws++;
                } else if (result.winner == value_player) {
                    value_wins++;
                } else if (result.winner == gungi_opponent(value_player)) {
                    old_wins++;
                } else {
                    draws++;
                }
                if (result.timeout) {
                    timeouts++;
                }

                total_ply += result.ply;
                total_value_time += result.value_time;
                total_old_time += result.old_time;
                total_value_moves += result.value_moves;
                total_old_moves += result.old_moves;
                total_nodes += result.value_nodes;
                total_pruned_root += result.value_pruned_root;
                total_tactical_root += result.value_tactical_root;
                total_searched_tactical_root += result.value_searched_tactical_root;

                fprintf(csv,
                        "%d,%u,%d,%d,%d,%d,%" PRIu64 ",%s,%s,%s,%d,%d,%d,%d,%.6f,%.6f,%d,%d,%lld,%lld,%lld,%d,%d,%lld,%d,%d\n",
                        seed_index + 1,
                        seed,
                        global_game + 1,
                        opening_id,
                        requested,
                        applied,
                        opening_hash,
                        player_name(value_player),
                        status_name(result.status),
                        player_name(result.winner),
                        result.ply,
                        result.timeout,
                        config.value_depth,
                        config.value_top_k,
                        result.value_time,
                        result.old_time,
                        result.value_moves,
                        result.old_moves,
                        result.value_nodes,
                        result.value_leaves,
                        result.value_cutoffs,
                        result.value_root_moves,
                        result.value_searched_root,
                        result.value_pruned_root,
                        result.value_tactical_root,
                        result.value_searched_tactical_root);

                global_game++;
                printf("Game %d/%d | seed %u | value %s | status %s | winner %s | ply %d\n",
                       global_game,
                       config.games,
                       seed,
                       player_name(value_player),
                       status_name(result.status),
                       player_name(result.winner),
                       result.ply);
            }
        }
    }

    fprintf(summary, "metric,value\n");
    fprintf(summary, "games,%d\n", config.games);
    fprintf(summary, "seed_count,%d\n", config.seed_count);
    fprintf(summary, "model_path,%s\n", config.model_path);
    fprintf(summary, "depth,%d\n", config.value_depth);
    fprintf(summary, "top_k,%d\n", config.value_top_k);
    fprintf(summary, "opening_min,%d\n", config.opening_min);
    fprintf(summary, "opening_max,%d\n", config.opening_max);
    fprintf(summary, "max_ply,%d\n", config.max_ply);
    fprintf(summary, "value_wins,%d\n", value_wins);
    fprintf(summary, "old_wins,%d\n", old_wins);
    fprintf(summary, "draws,%d\n", draws);
    fprintf(summary, "timeouts,%d\n", timeouts);
    fprintf(summary, "avg_ply,%.6f\n", config.games > 0 ? (double)total_ply / (double)config.games : 0.0);
    fprintf(summary, "total_value_time,%.6f\n", total_value_time);
    fprintf(summary, "total_old_time,%.6f\n", total_old_time);
    fprintf(summary, "avg_value_think,%.6f\n", total_value_moves > 0 ? total_value_time / (double)total_value_moves : 0.0);
    fprintf(summary, "avg_old_think,%.6f\n", total_old_moves > 0 ? total_old_time / (double)total_old_moves : 0.0);
    fprintf(summary, "total_value_moves,%d\n", total_value_moves);
    fprintf(summary, "total_old_moves,%d\n", total_old_moves);
    fprintf(summary, "total_value_nodes,%lld\n", total_nodes);
    fprintf(summary, "total_pruned_root,%lld\n", total_pruned_root);
    fprintf(summary, "total_tactical_root,%lld\n", total_tactical_root);
    fprintf(summary, "total_searched_tactical_root,%lld\n", total_searched_tactical_root);

    fclose(summary);
    fclose(csv);

    printf("Value wins: %d | Old wins: %d | Draws: %d | Timeouts: %d\n",
           value_wins,
           old_wins,
           draws,
           timeouts);
    printf("Avg value think: %.6f s | Avg old think: %.6f s\n",
           total_value_moves > 0 ? total_value_time / (double)total_value_moves : 0.0,
           total_old_moves > 0 ? total_old_time / (double)total_old_moves : 0.0);
    printf("Total value nodes: %lld | Total root pruned: %lld | logs %s / %s\n",
           total_nodes,
           total_pruned_root,
           config.output_path,
           config.summary_path);
    return 0;
}
