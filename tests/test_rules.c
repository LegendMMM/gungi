#include "../src/gungi_rules.h"
#include "../src/ai_core.h"

#include <stdio.h>
#include <stdlib.h>

static int tests_run = 0;

#define CHECK(expr) \
    do { \
        tests_run++; \
        if (!(expr)) { \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #expr); \
            exit(1); \
        } \
    } while (0)

static int total_owned_pieces(const GameState *state, GungiPlayer player)
{
    int total = 0;
    int y;
    int x;
    int level;
    int type;

    for (y = 0; y < GUNGI_BOARD_SIZE; ++y) {
        for (x = 0; x < GUNGI_BOARD_SIZE; ++x) {
            int height = gungi_cell_height(state, x, y);
            for (level = 0; level < height; ++level) {
                Piece piece = gungi_stack_piece(state, x, y, level);
                if (piece.owner == player) {
                    total++;
                }
            }
        }
    }

    for (type = GUNGI_PIECE_NONE + 1; type < GUNGI_PIECE_TYPE_COUNT; ++type) {
        total += gungi_hand_count(state, player, (GungiPieceType)type);
    }

    return total;
}

static void setup_marshals(GameState *state)
{
    gungi_clear(state);
    CHECK(gungi_place_piece(state, GUNGI_PLAYER_BLACK, GUNGI_PIECE_MARSHAL, 8, 0));
    CHECK(gungi_place_piece(state, GUNGI_PLAYER_WHITE, GUNGI_PIECE_MARSHAL, 0, 8));
    gungi_set_current_player(state, GUNGI_PLAYER_BLACK);
    gungi_reset_repetition(state);
}

static void test_initialization(void)
{
    GameState state;
    Piece black_marshal;
    Piece white_marshal;

    gungi_init(&state);
    black_marshal = gungi_top_piece(&state, 4, 0);
    white_marshal = gungi_top_piece(&state, 4, 8);

    CHECK(state.current_player == GUNGI_PLAYER_BLACK);
    CHECK(state.status == GUNGI_STATUS_ONGOING);
    CHECK(black_marshal.owner == GUNGI_PLAYER_BLACK);
    CHECK(black_marshal.type == GUNGI_PIECE_MARSHAL);
    CHECK(white_marshal.owner == GUNGI_PLAYER_WHITE);
    CHECK(white_marshal.type == GUNGI_PIECE_MARSHAL);
    CHECK(total_owned_pieces(&state, GUNGI_PLAYER_BLACK) == 25);
    CHECK(total_owned_pieces(&state, GUNGI_PLAYER_WHITE) == 25);
    CHECK(gungi_hand_count(&state, GUNGI_PLAYER_BLACK, GUNGI_PIECE_CAPTAIN) == 1);
    CHECK(gungi_hand_count(&state, GUNGI_PLAYER_WHITE, GUNGI_PIECE_MUSKETEER) == 1);
}

static void test_legal_move_generation(void)
{
    GameState state;
    Move moves[GUNGI_MAX_LEGAL_MOVES];
    int count;
    int i;

    gungi_init(&state);
    count = gungi_generate_legal_moves(&state, moves, GUNGI_MAX_LEGAL_MOVES);

    CHECK(count > 0);
    CHECK(count <= GUNGI_MAX_LEGAL_MOVES);
    for (i = 0; i < count; ++i) {
        CHECK(gungi_validate_move(&state, moves[i]).ok);
    }
}

static void test_normal_move(void)
{
    GameState state;
    RulesResult result;
    Piece moved;

    setup_marshals(&state);
    CHECK(gungi_place_piece(&state, GUNGI_PLAYER_BLACK, GUNGI_PIECE_SAMURAI, 4, 4));
    gungi_reset_repetition(&state);

    result = gungi_apply_move(&state, gungi_make_move(GUNGI_PLAYER_BLACK, 4, 4, 4, 5));
    moved = gungi_top_piece(&state, 4, 5);

    CHECK(result.ok);
    CHECK(gungi_cell_height(&state, 4, 4) == 0);
    CHECK(moved.owner == GUNGI_PLAYER_BLACK);
    CHECK(moved.type == GUNGI_PIECE_SAMURAI);
    CHECK(state.current_player == GUNGI_PLAYER_WHITE);
}

static void test_stacking_and_top_piece_rules(void)
{
    GameState state;
    RulesResult result;
    Piece top;

    setup_marshals(&state);
    CHECK(gungi_place_piece(&state, GUNGI_PLAYER_BLACK, GUNGI_PIECE_PAWN, 4, 5));
    CHECK(gungi_place_piece(&state, GUNGI_PLAYER_BLACK, GUNGI_PIECE_SAMURAI, 4, 4));
    gungi_reset_repetition(&state);

    result = gungi_apply_move(&state, gungi_make_stack_move(GUNGI_PLAYER_BLACK, 4, 4, 4, 5));
    top = gungi_top_piece(&state, 4, 5);
    CHECK(result.ok);
    CHECK(gungi_cell_height(&state, 4, 5) == 2);
    CHECK(top.type == GUNGI_PIECE_SAMURAI);

    gungi_clear(&state);
    CHECK(gungi_place_piece(&state, GUNGI_PLAYER_BLACK, GUNGI_PIECE_MARSHAL, 4, 5));
    CHECK(gungi_place_piece(&state, GUNGI_PLAYER_WHITE, GUNGI_PIECE_MARSHAL, 0, 8));
    CHECK(gungi_place_piece(&state, GUNGI_PLAYER_BLACK, GUNGI_PIECE_SAMURAI, 4, 4));
    gungi_reset_repetition(&state);
    result = gungi_apply_move(&state, gungi_make_stack_move(GUNGI_PLAYER_BLACK, 4, 4, 4, 5));
    CHECK(!result.ok);
    CHECK(result.code == GUNGI_RULE_ERR_MARSHAL_STACK);

    gungi_clear(&state);
    CHECK(gungi_place_piece(&state, GUNGI_PLAYER_BLACK, GUNGI_PIECE_MARSHAL, 8, 8));
    CHECK(gungi_place_piece(&state, GUNGI_PLAYER_WHITE, GUNGI_PIECE_MARSHAL, 0, 8));
    CHECK(gungi_place_piece(&state, GUNGI_PLAYER_BLACK, GUNGI_PIECE_PAWN, 4, 4));
    CHECK(gungi_place_piece(&state, GUNGI_PLAYER_WHITE, GUNGI_PIECE_SPY, 4, 4));
    gungi_reset_repetition(&state);
    result = gungi_apply_move(&state, gungi_make_move(GUNGI_PLAYER_BLACK, 4, 4, 4, 5));
    CHECK(!result.ok);
    CHECK(result.code == GUNGI_RULE_ERR_NOT_OWNER);
}

static void test_capture_and_marshal_capture(void)
{
    GameState state;
    RulesResult result;
    Piece top;

    setup_marshals(&state);
    CHECK(gungi_place_piece(&state, GUNGI_PLAYER_BLACK, GUNGI_PIECE_SAMURAI, 4, 4));
    CHECK(gungi_place_piece(&state, GUNGI_PLAYER_WHITE, GUNGI_PIECE_PAWN, 4, 5));
    gungi_reset_repetition(&state);

    result = gungi_apply_move(&state, gungi_make_capture_move(GUNGI_PLAYER_BLACK, 4, 4, 4, 5));
    top = gungi_top_piece(&state, 4, 5);
    CHECK(result.ok);
    CHECK(result.captured_count == 1);
    CHECK(!result.captured_marshal);
    CHECK(gungi_cell_height(&state, 4, 5) == 1);
    CHECK(top.owner == GUNGI_PLAYER_BLACK);
    CHECK(top.type == GUNGI_PIECE_SAMURAI);

    gungi_clear(&state);
    CHECK(gungi_place_piece(&state, GUNGI_PLAYER_BLACK, GUNGI_PIECE_MARSHAL, 8, 8));
    CHECK(gungi_place_piece(&state, GUNGI_PLAYER_BLACK, GUNGI_PIECE_SAMURAI, 4, 4));
    CHECK(gungi_place_piece(&state, GUNGI_PLAYER_WHITE, GUNGI_PIECE_MARSHAL, 4, 5));
    gungi_reset_repetition(&state);
    result = gungi_apply_move(&state, gungi_make_capture_move(GUNGI_PLAYER_BLACK, 4, 4, 4, 5));
    CHECK(result.ok);
    CHECK(result.captured_marshal);
    CHECK(state.status == GUNGI_STATUS_BLACK_WIN);
    CHECK(state.winner == GUNGI_PLAYER_BLACK);
}

static void test_new_drop(void)
{
    GameState state;
    RulesResult result;
    Piece top;

    setup_marshals(&state);
    CHECK(gungi_place_piece(&state, GUNGI_PLAYER_BLACK, GUNGI_PIECE_PAWN, 4, 4));
    gungi_set_hand(&state, GUNGI_PLAYER_BLACK, GUNGI_PIECE_SPEAR, 1);
    gungi_reset_repetition(&state);

    result = gungi_validate_move(&state, gungi_make_drop(GUNGI_PLAYER_BLACK, GUNGI_PIECE_SPEAR, 4, 5));
    CHECK(!result.ok);
    CHECK(result.code == GUNGI_RULE_ERR_BAD_DROP);

    result = gungi_apply_move(&state, gungi_make_drop(GUNGI_PLAYER_BLACK, GUNGI_PIECE_SPEAR, 3, 3));
    top = gungi_top_piece(&state, 3, 3);
    CHECK(result.ok);
    CHECK(top.owner == GUNGI_PLAYER_BLACK);
    CHECK(top.type == GUNGI_PIECE_SPEAR);
    CHECK(gungi_hand_count(&state, GUNGI_PLAYER_BLACK, GUNGI_PIECE_SPEAR) == 0);
    CHECK(state.current_player == GUNGI_PLAYER_WHITE);

    setup_marshals(&state);
    CHECK(gungi_place_piece(&state, GUNGI_PLAYER_BLACK, GUNGI_PIECE_PAWN, 4, 4));
    CHECK(gungi_place_piece(&state, GUNGI_PLAYER_WHITE, GUNGI_PIECE_PAWN, 2, 2));
    gungi_set_hand(&state, GUNGI_PLAYER_BLACK, GUNGI_PIECE_ARCHER, 1);
    gungi_reset_repetition(&state);
    result = gungi_validate_move(&state, gungi_make_drop(GUNGI_PLAYER_BLACK, GUNGI_PIECE_ARCHER, 2, 2));
    CHECK(!result.ok);
    CHECK(result.code == GUNGI_RULE_ERR_BAD_DROP);

    result = gungi_apply_move(&state, gungi_make_drop(GUNGI_PLAYER_BLACK, GUNGI_PIECE_ARCHER, 4, 4));
    CHECK(result.ok);
    CHECK(gungi_cell_height(&state, 4, 4) == 2);
    CHECK(gungi_top_piece(&state, 4, 4).type == GUNGI_PIECE_ARCHER);
}

static void test_resignation(void)
{
    GameState state;
    RulesResult result;

    gungi_init(&state);
    result = gungi_apply_move(&state, gungi_make_resign(GUNGI_PLAYER_BLACK));
    CHECK(result.ok);
    CHECK(state.status == GUNGI_STATUS_RESIGNED);
    CHECK(state.winner == GUNGI_PLAYER_WHITE);
}

static void test_check_detection(void)
{
    GameState state;

    gungi_clear(&state);
    CHECK(gungi_place_piece(&state, GUNGI_PLAYER_BLACK, GUNGI_PIECE_MARSHAL, 4, 4));
    CHECK(gungi_place_piece(&state, GUNGI_PLAYER_WHITE, GUNGI_PIECE_MARSHAL, 0, 8));
    CHECK(gungi_place_piece(&state, GUNGI_PLAYER_WHITE, GUNGI_PIECE_SAMURAI, 4, 5));
    gungi_reset_repetition(&state);

    CHECK(gungi_is_square_attacked(&state, 4, 4, GUNGI_PLAYER_WHITE));
    CHECK(gungi_is_in_check(&state, GUNGI_PLAYER_BLACK));
    CHECK(!gungi_is_in_check(&state, GUNGI_PLAYER_WHITE));
}

static void test_repetition_draw(void)
{
    GameState state;
    RulesResult result;
    int cycle;

    gungi_clear(&state);
    CHECK(gungi_place_piece(&state, GUNGI_PLAYER_BLACK, GUNGI_PIECE_MARSHAL, 8, 8));
    CHECK(gungi_place_piece(&state, GUNGI_PLAYER_WHITE, GUNGI_PIECE_MARSHAL, 0, 8));
    CHECK(gungi_place_piece(&state, GUNGI_PLAYER_BLACK, GUNGI_PIECE_KNIGHT, 4, 4));
    CHECK(gungi_place_piece(&state, GUNGI_PLAYER_WHITE, GUNGI_PIECE_KNIGHT, 0, 0));
    gungi_reset_repetition(&state);

    for (cycle = 0; cycle < 3; ++cycle) {
        result = gungi_apply_move(&state, gungi_make_move(GUNGI_PLAYER_BLACK, 4, 4, 4, 6));
        CHECK(result.ok);
        result = gungi_apply_move(&state, gungi_make_move(GUNGI_PLAYER_WHITE, 0, 0, 1, 0));
        CHECK(result.ok);
        result = gungi_apply_move(&state, gungi_make_move(GUNGI_PLAYER_BLACK, 4, 6, 4, 4));
        CHECK(result.ok);
        result = gungi_apply_move(&state, gungi_make_move(GUNGI_PLAYER_WHITE, 1, 0, 0, 0));
        CHECK(result.ok);
    }

    CHECK(state.status == GUNGI_STATUS_DRAW);
    CHECK(result.draw);
}

static void test_special_jump_ignores_height(void)
{
    GameState state;
    RulesResult result;

    setup_marshals(&state);
    CHECK(gungi_place_piece(&state, GUNGI_PLAYER_BLACK, GUNGI_PIECE_CANNON, 4, 4));
    CHECK(gungi_place_piece(&state, GUNGI_PLAYER_BLACK, GUNGI_PIECE_PAWN, 4, 5));
    gungi_reset_repetition(&state);

    result = gungi_apply_move(&state, gungi_make_move(GUNGI_PLAYER_BLACK, 4, 4, 4, 7));
    CHECK(result.ok);
    CHECK(gungi_top_piece(&state, 4, 7).type == GUNGI_PIECE_CANNON);

    setup_marshals(&state);
    CHECK(gungi_place_piece(&state, GUNGI_PLAYER_BLACK, GUNGI_PIECE_CANNON, 4, 4));
    CHECK(gungi_place_piece(&state, GUNGI_PLAYER_BLACK, GUNGI_PIECE_PAWN, 4, 5));
    CHECK(gungi_place_piece(&state, GUNGI_PLAYER_BLACK, GUNGI_PIECE_PAWN, 4, 5)); 
    CHECK(gungi_place_piece(&state, GUNGI_PLAYER_BLACK, GUNGI_PIECE_PAWN, 4, 5));
    gungi_reset_repetition(&state);

    result = gungi_apply_move(&state, gungi_make_move(GUNGI_PLAYER_BLACK, 4, 4, 4, 7));
    CHECK(result.ok); 
    CHECK(gungi_top_piece(&state, 4, 7).type == GUNGI_PIECE_CANNON);
}

int main(void)
{
    test_initialization();
    test_legal_move_generation();
    test_normal_move();
    test_stacking_and_top_piece_rules();
    test_capture_and_marshal_capture();
    test_new_drop();
    test_resignation();
    test_check_detection();
    test_repetition_draw();
    test_special_jump_ignores_height();

    printf("test_rules: %d checks passed\n", tests_run);
    return 0;
}
