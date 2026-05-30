#ifndef GUNGI_RULES_H
#define GUNGI_RULES_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define GUNGI_BOARD_SIZE 9
#define GUNGI_MAX_STACK 3
#define GUNGI_MAX_HAND_ITEMS 32
#define GUNGI_PIECE_CODE_LEN 8
#define GUNGI_STATUS_LEN 160
#define GUNGI_MAX_REPETITIONS 1024
#define GUNGI_REPETITION_DRAW_COUNT 4

typedef enum GungiPlayer {
    GUNGI_PLAYER_NONE = -1,
    GUNGI_PLAYER_BLACK = 0,
    GUNGI_PLAYER_WHITE = 1
} GungiPlayer;

typedef enum GungiPieceType {
    GUNGI_PIECE_NONE = 0,
    GUNGI_PIECE_MARSHAL,
    GUNGI_PIECE_SAMURAI,
    GUNGI_PIECE_SPEAR,
    GUNGI_PIECE_KNIGHT,
    GUNGI_PIECE_SPY,
    GUNGI_PIECE_FORT,
    GUNGI_PIECE_PAWN,
    GUNGI_PIECE_CAPTAIN,
    GUNGI_PIECE_LIEUTENANT,
    GUNGI_PIECE_MAJOR,
    GUNGI_PIECE_GENERAL,
    GUNGI_PIECE_CANNON,
    GUNGI_PIECE_MUSKETEER,
    GUNGI_PIECE_ARCHER,
    GUNGI_PIECE_TYPE_COUNT
} GungiPieceType;

#define GUNGI_PIECE_HORSE GUNGI_PIECE_KNIGHT
#define GUNGI_PIECE_FORTRESS GUNGI_PIECE_FORT
#define GUNGI_PIECE_SOLDIER GUNGI_PIECE_PAWN
#define GUNGI_PIECE_LIEUTENANT_GENERAL GUNGI_PIECE_LIEUTENANT
#define GUNGI_PIECE_MAJOR_GENERAL GUNGI_PIECE_MAJOR

typedef enum GungiMoveKind {
    GUNGI_MOVE_NORMAL = 0,
    GUNGI_MOVE_DROP,
    GUNGI_MOVE_RESIGN
} GungiMoveKind;

typedef enum GungiMoveIntent {
    GUNGI_INTENT_AUTO = 0,
    GUNGI_INTENT_STACK,
    GUNGI_INTENT_CAPTURE
} GungiMoveIntent;

typedef enum GungiGameStatus {
    GUNGI_STATUS_ONGOING = 0,
    GUNGI_STATUS_BLACK_WIN,
    GUNGI_STATUS_WHITE_WIN,
    GUNGI_STATUS_DRAW,
    GUNGI_STATUS_RESIGNED
} GungiGameStatus;

typedef enum GungiGamePhase {
    GUNGI_PHASE_SETUP = 0,
    GUNGI_PHASE_PLAYING = 1,
    GUNGI_PHASE_FINISHED = 2
} GungiGamePhase;

typedef enum GungiActionType {
    GUNGI_ACTION_NEW = 0,
    GUNGI_ACTION_MOVE = 1,
    GUNGI_ACTION_CAPTURE = 2,
    GUNGI_ACTION_STACK = 3
} GungiActionType;

typedef enum GungiRuleCode {
    GUNGI_RULE_OK = 0,
    GUNGI_RULE_ERR_NULL_STATE,
    GUNGI_RULE_ERR_GAME_OVER,
    GUNGI_RULE_ERR_WRONG_TURN,
    GUNGI_RULE_ERR_BAD_COORD,
    GUNGI_RULE_ERR_BAD_PIECE,
    GUNGI_RULE_ERR_NO_PIECE,
    GUNGI_RULE_ERR_NOT_OWNER,
    GUNGI_RULE_ERR_BAD_MOVE,
    GUNGI_RULE_ERR_BLOCKED,
    GUNGI_RULE_ERR_FULL_STACK,
    GUNGI_RULE_ERR_MARSHAL_STACK,
    GUNGI_RULE_ERR_STACK_TOO_HIGH,
    GUNGI_RULE_ERR_NEEDS_HAND,
    GUNGI_RULE_ERR_BAD_DROP,
    GUNGI_RULE_ERR_BAD_INTENT,
    GUNGI_RULE_ERR_SELF_CHECK
} GungiRuleCode;

typedef struct Piece {
    GungiPieceType type;
    GungiPlayer owner;
} Piece;

typedef struct BoardCell {
    Piece stack[GUNGI_MAX_STACK];
    int height;
} BoardCell;

typedef struct PieceDef {
    GungiPieceType type;
    const char *name;
    const char *short_name;
    int inventory_count;
    int special_jump;
} PieceDef;

typedef struct Move {
    GungiMoveKind kind;
    GungiPlayer player;
    int from_x;
    int from_y;
    int to_x;
    int to_y;
    GungiPieceType drop_type;
    GungiMoveIntent intent;
    int betray_mask;
} Move;

typedef struct RulesResult {
    int ok;
    GungiRuleCode code;
    GungiGameStatus status;
    GungiPlayer winner;
    int gives_check;
    int checkmate;
    int draw;
    int captured_count;
    int captured_marshal;
    char message[128];
} RulesResult;

typedef struct GameState {
    BoardCell board[GUNGI_BOARD_SIZE][GUNGI_BOARD_SIZE];
    int hands[2][GUNGI_PIECE_TYPE_COUNT];
    GungiPlayer current_player;
    GungiGameStatus status;
    GungiPlayer winner;
    unsigned int ply_count;
    uint64_t repetition_history[GUNGI_MAX_REPETITIONS];
    int repetition_len;
} GameState;

typedef struct GungiPieceView {
    char code[GUNGI_PIECE_CODE_LEN];
    GungiPlayer owner;
} GungiPieceView;

typedef struct GungiCellView {
    GungiPieceView stack[GUNGI_MAX_STACK];
    int height;
} GungiCellView;

typedef struct GungiHandEntryView {
    char code[GUNGI_PIECE_CODE_LEN];
    int count;
} GungiHandEntryView;

typedef struct GungiGameView {
    GungiPlayer current_player;
    GungiGamePhase phase;
    GungiPlayer winner;
    GungiCellView board[GUNGI_BOARD_SIZE][GUNGI_BOARD_SIZE];
    GungiHandEntryView hands[2][GUNGI_MAX_HAND_ITEMS];
    int hand_count[2];
    char status[GUNGI_STATUS_LEN];
} GungiGameView;

typedef struct GungiMoveRequest {
    GungiActionType action;
    GungiPlayer player;
    int from_x;
    int from_y;
    int from_level;
    int to_x;
    int to_y;
    int hand_index;
    int betray_mask;
} GungiMoveRequest;

typedef struct GungiGame GungiGame;

const PieceDef *gungi_get_piece_def(GungiPieceType type);
const char *gungi_piece_name(GungiPieceType type);
GungiPlayer gungi_opponent(GungiPlayer player);

void gungi_clear(GameState *state);
void gungi_init(GameState *state);
void gungi_reset_repetition(GameState *state);
uint64_t gungi_position_hash(const GameState *state);
int gungi_count_repetition(const GameState *state, uint64_t hash);

Move gungi_make_move(GungiPlayer player, int from_x, int from_y, int to_x, int to_y);
Move gungi_make_stack_move(GungiPlayer player, int from_x, int from_y, int to_x, int to_y);
Move gungi_make_capture_move(GungiPlayer player, int from_x, int from_y, int to_x, int to_y);
Move gungi_make_drop(GungiPlayer player, GungiPieceType type, int to_x, int to_y);
Move gungi_make_resign(GungiPlayer player);

RulesResult gungi_validate_move(const GameState *state, Move move);
RulesResult gungi_apply_move(GameState *state, Move move);

int gungi_is_in_check(const GameState *state, GungiPlayer player);
int gungi_is_square_attacked(const GameState *state, int x, int y, GungiPlayer by_player);

int gungi_cell_height(const GameState *state, int x, int y);
Piece gungi_top_piece(const GameState *state, int x, int y);
Piece gungi_stack_piece(const GameState *state, int x, int y, int level);
int gungi_hand_count(const GameState *state, GungiPlayer player, GungiPieceType type);

int gungi_place_piece(GameState *state, GungiPlayer owner, GungiPieceType type, int x, int y);
void gungi_set_hand(GameState *state, GungiPlayer player, GungiPieceType type, int count);
void gungi_set_current_player(GameState *state, GungiPlayer player);

GungiGame *gungi_create(void);
// 取得遊戲內部的 GameState 指標 (供 AI 讀取與計算)
GameState *gungi_game_get_state(GungiGame *game);
void gungi_destroy(GungiGame *game);
void gungi_reset(GungiGame *game);
bool gungi_get_view(const GungiGame *game, GungiGameView *out_view);
bool gungi_apply_request(GungiGame *game, const GungiMoveRequest *request);
bool gungi_resign(GungiGame *game, GungiPlayer player);
const char *gungi_last_error(const GungiGame *game);

#ifdef __cplusplus
}
#endif

#endif
