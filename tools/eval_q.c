#include "../src/q_model.h"

#include <stdio.h>
#include <stdlib.h>

#define MAX_EVAL_PLY 20000

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

static void write_header(FILE *log)
{
    fprintf(log,
            "ply,player,kind,intent,from_x,from_y,to_x,to_y,drop_type,capture_value,gives_check,status,winner,timed_out,reward,score,value_before,value_after\n");
}

int main(int argc, char **argv)
{
    const char *model_path = argc > 1 ? argv[1] : GUNGI_V_DEFAULT_MODEL_PATH;
    const char *log_path = argc > 2 ? argv[2] : "q_selfplay_eval.csv";
    const GungiQProfileConfig *config = gungi_q_profile_config(GUNGI_Q_PROFILE_BALANCED);
    GungiQModel model;
    GameState state;
    FILE *log;
    int ply = 0;
    int timed_out = 0;

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

    gungi_init(&state);
    write_header(log);

    while (state.status == GUNGI_STATUS_ONGOING && ply < MAX_EVAL_PLY) {
        GameState before = state;
        Move move = gungi_get_q_move(&before, &model, 0.0f);
        float value_before = gungi_v_evaluate(&model, &before);
        float score = gungi_v_score_move(&model, &before, move, config->gamma);
        int capture_value = gungi_q_capture_value(&before, move);
        RulesResult result = gungi_apply_move(&state, move);
        float value_after = gungi_v_evaluate(&model, &state);
        float reward;

        if (!result.ok) {
            fprintf(stderr, "Illegal Q move at ply %d: code %d\n", ply + 1, result.code);
            fclose(log);
            return 1;
        }

        ply++;
        timed_out = state.status == GUNGI_STATUS_ONGOING && ply >= MAX_EVAL_PLY;
        reward = gungi_v_immediate_reward(&before, move, &state, &result, timed_out);

        fprintf(log,
                "%d,%s,%d,%d,%d,%d,%d,%d,%d,%d,%d,%s,%s,%d,%.6f,%.6f,%.6f,%.6f\n",
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
                result.gives_check,
                status_name(state.status),
                player_name(state.winner),
                timed_out,
                reward,
                score,
                value_before,
                value_after);

        if (timed_out) {
            break;
        }
    }

    fclose(log);

    printf("Q self-play finished | status %s | winner %s | ply %d | timeout %d | log %s\n",
           status_name(state.status),
           player_name(state.winner),
           ply,
           timed_out,
           log_path);

    return 0;
}
