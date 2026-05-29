#include "gungi_rules.h"

#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct MovementRule {
    int step_x;
    int step_y;
    int min_distance;
    int max_distance;
    int unlimited;
    int scales_with_height;
    int special_jump;
} MovementRule;

typedef struct MovementSet {
    const MovementRule *rules;
    int count;
} MovementSet;

typedef struct SetupPiece {
    int x;
    int y;
    GungiPieceType type;
} SetupPiece;

#define RULE(dx, dy, min_d, max_d, inf, scale, jump) \
    { (dx), (dy), (min_d), (max_d), (inf), (scale), (jump) }

static const Piece EMPTY_PIECE = { GUNGI_PIECE_NONE, GUNGI_PLAYER_NONE };

struct GungiGame {
    GameState state;
    char last_error[GUNGI_STATUS_LEN];
};

static const PieceDef PIECE_DEFS[GUNGI_PIECE_TYPE_COUNT] = {
    { GUNGI_PIECE_NONE, "None", "--", 0, 0 },
    { GUNGI_PIECE_MARSHAL, "Marshal", "Su", 1, 0 }, // 帥
    { GUNGI_PIECE_SAMURAI, "Samurai", "Si", 2, 0 }, // 侍
    { GUNGI_PIECE_SPEAR, "Spear", "Qi", 3, 0 }, // 槍
    { GUNGI_PIECE_KNIGHT, "Knight", "Ma", 2, 0 }, // 馬
    { GUNGI_PIECE_SPY, "Spy", "Rn", 2, 0 }, // 忍
    { GUNGI_PIECE_FORT, "Fort", "Zi", 2, 0 }, // 砦
    { GUNGI_PIECE_PAWN, "Pawn", "Bi", 4, 0 }, // 兵
    { GUNGI_PIECE_CAPTAIN, "Captain", "Xi", 2, 0 }, // 小 (小將)
    { GUNGI_PIECE_LIEUTENANT, "Lieutenant General", "Zo", 1, 0 }, // 中 (中將)
    { GUNGI_PIECE_MAJOR, "Major General", "Da", 1, 0 }, // 大 (大將)
    { GUNGI_PIECE_GENERAL, "General", "Mo", 1, 0 }, // 謀
    { GUNGI_PIECE_CANNON, "Cannon", "Po", 1, 1 }, // 砲
    { GUNGI_PIECE_MUSKETEER, "Musketeer", "To", 1, 1 }, // 筒
    { GUNGI_PIECE_ARCHER, "Archer", "Go", 2, 1 } // 弓
};

static const MovementRule MARSHAL_RULES[] = {
    RULE(-1, -1, 1, 1, 0, 1, 0), RULE(0, -1, 1, 1, 0, 1, 0), RULE(1, -1, 1, 1, 0, 1, 0),
    RULE(-1, 0, 1, 1, 0, 1, 0),                                      RULE(1, 0, 1, 1, 0, 1, 0),
    RULE(-1, 1, 1, 1, 0, 1, 0),  RULE(0, 1, 1, 1, 0, 1, 0),  RULE(1, 1, 1, 1, 0, 1, 0)
};

static const MovementRule GENERAL_RULES[] = {
    RULE(-1, 0, 1, 8, 1, 0, 0), RULE(1, 0, 1, 8, 1, 0, 0),
    RULE(0, -1, 1, 8, 1, 0, 0), RULE(0, 1, 1, 8, 1, 0, 0),
    RULE(-1, -1, 1, 1, 0, 1, 0), RULE(1, -1, 1, 1, 0, 1, 0),
    RULE(-1, 1, 1, 1, 0, 1, 0),  RULE(1, 1, 1, 1, 0, 1, 0)
};

static const MovementRule LIEUTENANT_RULES[] = {
    RULE(-1, -1, 1, 8, 1, 0, 0), RULE(1, -1, 1, 8, 1, 0, 0),
    RULE(-1, 1, 1, 8, 1, 0, 0),  RULE(1, 1, 1, 8, 1, 0, 0),
    RULE(-1, 0, 1, 1, 0, 1, 0), RULE(1, 0, 1, 1, 0, 1, 0),
    RULE(0, -1, 1, 1, 0, 1, 0), RULE(0, 1, 1, 1, 0, 1, 0)
};

static const MovementRule MAJOR_RULES[] = {
    RULE(-1, 0, 1, 1, 0, 1, 0), RULE(1, 0, 1, 1, 0, 1, 0),
    RULE(0, -1, 1, 1, 0, 1, 0), RULE(0, 1, 1, 1, 0, 1, 0),
    RULE(-1, 1, 1, 1, 0, 1, 0), RULE(1, 1, 1, 1, 0, 1, 0)
};

static const MovementRule SAMURAI_RULES[] = {
    RULE(0, 1, 1, 1, 0, 1, 0), RULE(0, -1, 1, 1, 0, 1, 0),
    RULE(-1, 1, 1, 1, 0, 1, 0), RULE(1, 1, 1, 1, 0, 1, 0)
};

static const MovementRule SPEAR_RULES[] = {
    RULE(0, 1, 1, 2, 0, 1, 0),
    RULE(-1, 1, 1, 1, 0, 1, 0), RULE(1, 1, 1, 1, 0, 1, 0),
    RULE(0, -1, 1, 1, 0, 1, 0)
};

static const MovementRule KNIGHT_RULES[] = {
    RULE(0, 1, 1, 2, 0, 1, 0), RULE(0, -1, 1, 2, 0, 1, 0),
    RULE(-1, 0, 1, 1, 0, 1, 0), RULE(1, 0, 1, 1, 0, 1, 0)
};

static const MovementRule SPY_RULES[] = {
    RULE(-1, -1, 1, 2, 0, 1, 0), RULE(1, -1, 1, 2, 0, 1, 0),
    RULE(-1, 1, 1, 2, 0, 1, 0),  RULE(1, 1, 1, 2, 0, 1, 0)
};

static const MovementRule FORT_RULES[] = {
    RULE(0, 1, 1, 1, 0, 1, 0),
    RULE(-1, 0, 1, 1, 0, 1, 0), RULE(1, 0, 1, 1, 0, 1, 0),
    RULE(-1, -1, 1, 1, 0, 1, 0), RULE(1, -1, 1, 1, 0, 1, 0)
};

static const MovementRule PAWN_RULES[] = {
    RULE(0, 1, 1, 1, 0, 1, 0), RULE(0, -1, 1, 1, 0, 1, 0)
};

static const MovementRule CAPTAIN_RULES[] = {
    RULE(-1, 1, 1, 1, 0, 1, 0), RULE(1, 1, 1, 1, 0, 1, 0),
    RULE(0, -1, 1, 1, 0, 1, 0)
};

static const MovementRule CANNON_RULES[] = {
    RULE(-1, 0, 1, 1, 0, 1, 0), RULE(1, 0, 1, 1, 0, 1, 0),
    RULE(0, -1, 1, 1, 0, 1, 0),
    RULE(0, 1, 3, 3, 0, 1, 1)
};

static const MovementRule MUSKETEER_RULES[] = {
    RULE(-1, -1, 1, 1, 0, 1, 0), RULE(1, -1, 1, 1, 0, 1, 0),
    RULE(0, 1, 2, 2, 0, 1, 1)
};

static const MovementRule ARCHER_RULES[] = {
    RULE(0, -1, 1, 1, 0, 1, 0),
    RULE(0, 1, 2, 2, 0, 1, 1),
    RULE(-1, 1, 2, 2, 0, 1, 1), RULE(1, 1, 2, 2, 0, 1, 1)
};

static const SetupPiece DEFAULT_SETUP[] = {
    { 3, 0, GUNGI_PIECE_LIEUTENANT }, // 中 (中將)
    { 4, 0, GUNGI_PIECE_MARSHAL },    // 帥
    { 5, 0, GUNGI_PIECE_MAJOR },      // 大 (大將)

    // --- 第二排 (y = 1) ---
    { 1, 1, GUNGI_PIECE_KNIGHT },     // 馬
    { 2, 1, GUNGI_PIECE_ARCHER },     // 弓
    { 4, 1, GUNGI_PIECE_SPEAR },      // 槍
    { 6, 1, GUNGI_PIECE_ARCHER },     // 弓
    { 7, 1, GUNGI_PIECE_SPY },        // 忍

    // --- 第三排 (y = 2) ---
    { 0, 2, GUNGI_PIECE_PAWN },       // 兵
    { 2, 2, GUNGI_PIECE_FORT },       // 砦
    { 3, 2, GUNGI_PIECE_SAMURAI },    // 侍
    { 4, 2, GUNGI_PIECE_PAWN },       // 兵
    { 5, 2, GUNGI_PIECE_SAMURAI },    // 侍
    { 6, 2, GUNGI_PIECE_FORT },       // 砦
    { 8, 2, GUNGI_PIECE_PAWN }        // 兵
};

static int in_bounds(int x, int y)
{
    return x >= 0 && x < GUNGI_BOARD_SIZE && y >= 0 && y < GUNGI_BOARD_SIZE;
}

static int valid_player(GungiPlayer player)
{
    return player == GUNGI_PLAYER_BLACK || player == GUNGI_PLAYER_WHITE;
}

static int valid_piece_type(GungiPieceType type)
{
    return type > GUNGI_PIECE_NONE && type < GUNGI_PIECE_TYPE_COUNT;
}

static int signum(int value)
{
    return (value > 0) - (value < 0);
}

static void set_result(RulesResult *result, int ok, GungiRuleCode code, const char *message)
{
    if (result == NULL) {
        return;
    }

    memset(result, 0, sizeof(*result));
    result->ok = ok;
    result->code = code;
    result->status = GUNGI_STATUS_ONGOING;
    result->winner = GUNGI_PLAYER_NONE;
    if (message != NULL) {
        snprintf(result->message, sizeof(result->message), "%s", message);
    }
}

static RulesResult make_result(int ok, GungiRuleCode code, const char *message)
{
    RulesResult result;
    set_result(&result, ok, code, message);
    return result;
}

static BoardCell *mutable_cell(GameState *state, int x, int y)
{
    return &state->board[y][x];
}

static const BoardCell *const_cell(const GameState *state, int x, int y)
{
    return &state->board[y][x];
}

static Piece top_of_cell(const BoardCell *cell)
{
    if (cell == NULL || cell->height <= 0) {
        return EMPTY_PIECE;
    }
    return cell->stack[cell->height - 1];
}

static int push_piece(BoardCell *cell, Piece piece)
{
    if (cell == NULL || cell->height >= GUNGI_MAX_STACK || !valid_piece_type(piece.type) || !valid_player(piece.owner)) {
        return 0;
    }
    cell->stack[cell->height] = piece;
    cell->height++;
    return 1;
}

static Piece pop_piece(BoardCell *cell)
{
    Piece piece = EMPTY_PIECE;
    if (cell == NULL || cell->height <= 0) {
        return piece;
    }
    piece = cell->stack[cell->height - 1];
    cell->stack[cell->height - 1] = EMPTY_PIECE;
    cell->height--;
    return piece;
}

static int remove_owner_pieces(BoardCell *cell, GungiPlayer owner, int *captured_marshal)
{
    int read_index;
    int write_index = 0;
    int removed = 0;

    if (captured_marshal != NULL) {
        *captured_marshal = 0;
    }

    for (read_index = 0; read_index < cell->height; ++read_index) {
        Piece piece = cell->stack[read_index];
        if (piece.owner == owner) {
            if (captured_marshal != NULL && piece.type == GUNGI_PIECE_MARSHAL) {
                *captured_marshal = 1;
            }
            removed++;
        } else {
            cell->stack[write_index] = piece;
            write_index++;
        }
    }

    while (write_index < cell->height) {
        cell->stack[write_index] = EMPTY_PIECE;
        write_index++;
    }
    cell->height -= removed;
    return removed;
}

static MovementSet movement_set_for(GungiPieceType type)
{
    MovementSet set;
    set.rules = NULL;
    set.count = 0;

    switch (type) {
    case GUNGI_PIECE_MARSHAL:
        set.rules = MARSHAL_RULES;
        set.count = (int)(sizeof(MARSHAL_RULES) / sizeof(MARSHAL_RULES[0]));
        break;
    case GUNGI_PIECE_GENERAL:
        set.rules = GENERAL_RULES;
        set.count = (int)(sizeof(GENERAL_RULES) / sizeof(GENERAL_RULES[0]));
        break;
    case GUNGI_PIECE_LIEUTENANT:
        set.rules = LIEUTENANT_RULES;
        set.count = (int)(sizeof(LIEUTENANT_RULES) / sizeof(LIEUTENANT_RULES[0]));
        break;
    case GUNGI_PIECE_MAJOR:
        set.rules = MAJOR_RULES;
        set.count = (int)(sizeof(MAJOR_RULES) / sizeof(MAJOR_RULES[0]));
        break;
    case GUNGI_PIECE_SAMURAI:
        set.rules = SAMURAI_RULES;
        set.count = (int)(sizeof(SAMURAI_RULES) / sizeof(SAMURAI_RULES[0]));
        break;
    case GUNGI_PIECE_SPEAR:
        set.rules = SPEAR_RULES;
        set.count = (int)(sizeof(SPEAR_RULES) / sizeof(SPEAR_RULES[0]));
        break;
    case GUNGI_PIECE_KNIGHT:
        set.rules = KNIGHT_RULES;
        set.count = (int)(sizeof(KNIGHT_RULES) / sizeof(KNIGHT_RULES[0]));
        break;
    case GUNGI_PIECE_SPY:
        set.rules = SPY_RULES;
        set.count = (int)(sizeof(SPY_RULES) / sizeof(SPY_RULES[0]));
        break;
    case GUNGI_PIECE_FORT:
        set.rules = FORT_RULES;
        set.count = (int)(sizeof(FORT_RULES) / sizeof(FORT_RULES[0]));
        break;
    case GUNGI_PIECE_PAWN:
        set.rules = PAWN_RULES;
        set.count = (int)(sizeof(PAWN_RULES) / sizeof(PAWN_RULES[0]));
        break;
    case GUNGI_PIECE_CAPTAIN:
        set.rules = CAPTAIN_RULES;
        set.count = (int)(sizeof(CAPTAIN_RULES) / sizeof(CAPTAIN_RULES[0]));
        break;
    case GUNGI_PIECE_CANNON:
        set.rules = CANNON_RULES;
        set.count = (int)(sizeof(CANNON_RULES) / sizeof(CANNON_RULES[0]));
        break;
    case GUNGI_PIECE_MUSKETEER:
        set.rules = MUSKETEER_RULES;
        set.count = (int)(sizeof(MUSKETEER_RULES) / sizeof(MUSKETEER_RULES[0]));
        break;
    case GUNGI_PIECE_ARCHER:
        set.rules = ARCHER_RULES;
        set.count = (int)(sizeof(ARCHER_RULES) / sizeof(ARCHER_RULES[0]));
        break;
    default:
        break;
    }

    return set;
}

static int rule_matches_delta(const MovementRule *rule, int dx, int dy_forward, int *distance)
{
    int dist = 0;

    if (rule->step_x == 0 && rule->step_y == 0) {
        return 0;
    }

    if (rule->step_x == 0) {
        if (dx != 0 || signum(dy_forward) != rule->step_y) {
            return 0;
        }
        dist = dy_forward * rule->step_y;
    } else if (rule->step_y == 0) {
        if (dy_forward != 0 || signum(dx) != rule->step_x) {
            return 0;
        }
        dist = dx * rule->step_x;
    } else {
        if (signum(dx) != rule->step_x || signum(dy_forward) != rule->step_y) {
            return 0;
        }
        if (dx * rule->step_x != dy_forward * rule->step_y) {
            return 0;
        }
        dist = dx * rule->step_x;
    }

    if (dist <= 0) {
        return 0;
    }

    if (distance != NULL) {
        *distance = dist;
    }
    return 1;
}

static int path_is_clear_or_jumpable(const GameState *state, int from_x, int from_y, int to_x, int to_y,
                                     int can_jump, int source_height)
{
    int step_x = signum(to_x - from_x);
    int step_y = signum(to_y - from_y);
    int x = from_x + step_x;
    int y = from_y + step_y;

    while (x != to_x || y != to_y) {
        int height = const_cell(state, x, y)->height;
        if (height > 0) {
            if (!can_jump || height > source_height) {
                return 0;
            }
        }
        x += step_x;
        y += step_y;
    }

    return 1;
}

static int piece_can_reach(const GameState *state, Piece piece, int source_height,
                           int from_x, int from_y, int to_x, int to_y, GungiRuleCode *fail_code)
{
    MovementSet set;
    int forward;
    int dx;
    int dy_forward;
    int i;

    if (fail_code != NULL) {
        *fail_code = GUNGI_RULE_ERR_BAD_MOVE;
    }

    if (!valid_piece_type(piece.type) || !valid_player(piece.owner)) {
        return 0;
    }

    set = movement_set_for(piece.type);
    if (set.rules == NULL) {
        return 0;
    }

    forward = (piece.owner == GUNGI_PLAYER_BLACK) ? 1 : -1;
    dx = to_x - from_x;
    dy_forward = (to_y - from_y) * forward;

    for (i = 0; i < set.count; ++i) {
        const MovementRule *rule = &set.rules[i];
        int distance = 0;
        int max_distance;

        if (!rule_matches_delta(rule, dx, dy_forward, &distance)) {
            continue;
        }

        max_distance = rule->unlimited ? 8 : rule->max_distance;
        if (rule->scales_with_height) {
            max_distance += source_height - 1;
        }

        if (distance < rule->min_distance || distance > max_distance) {
            continue;
        }

        if (!path_is_clear_or_jumpable(state, from_x, from_y, to_x, to_y, rule->special_jump, source_height)) {
            if (fail_code != NULL) {
                *fail_code = GUNGI_RULE_ERR_BLOCKED;
            }
            return 0;
        }

        return 1;
    }

    return 0;
}

static int find_top_marshal(const GameState *state, GungiPlayer player, int *out_x, int *out_y)
{
    int y;
    int x;

    for (y = 0; y < GUNGI_BOARD_SIZE; ++y) {
        for (x = 0; x < GUNGI_BOARD_SIZE; ++x) {
            Piece top = top_of_cell(const_cell(state, x, y));
            if (top.owner == player && top.type == GUNGI_PIECE_MARSHAL) {
                if (out_x != NULL) {
                    *out_x = x;
                }
                if (out_y != NULL) {
                    *out_y = y;
                }
                return 1;
            }
        }
    }

    return 0;
}

static int owner_has_marshal(const GameState *state, GungiPlayer player)
{
    return find_top_marshal(state, player, NULL, NULL);
}

static int most_advanced_row(const GameState *state, GungiPlayer player, int *row)
{
    int y;
    int x;
    int found = 0;
    int best = (player == GUNGI_PLAYER_BLACK) ? 0 : GUNGI_BOARD_SIZE - 1;

    for (y = 0; y < GUNGI_BOARD_SIZE; ++y) {
        for (x = 0; x < GUNGI_BOARD_SIZE; ++x) {
            const BoardCell *cell = const_cell(state, x, y);
            int level;
            for (level = 0; level < cell->height; ++level) {
                if (cell->stack[level].owner == player) {
                    if (!found) {
                        best = y;
                        found = 1;
                    } else if (player == GUNGI_PLAYER_BLACK && y > best) {
                        best = y;
                    } else if (player == GUNGI_PLAYER_WHITE && y < best) {
                        best = y;
                    }
                }
            }
        }
    }

    if (row != NULL) {
        if (found) {
            *row = best;
        } else {
            *row = (player == GUNGI_PLAYER_BLACK) ? 2 : GUNGI_BOARD_SIZE - 3;
        }
    }

    return found;
}

static int drop_row_allowed(const GameState *state, GungiPlayer player, int y)
{
    int advanced;

    most_advanced_row(state, player, &advanced);
    if (player == GUNGI_PLAYER_BLACK) {
        return y >= 0 && y <= advanced;
    }
    if (player == GUNGI_PLAYER_WHITE) {
        return y <= GUNGI_BOARD_SIZE - 1 && y >= advanced;
    }
    return 0;
}

static GungiMoveIntent resolved_intent(const BoardCell *dest, Piece moving, GungiMoveIntent requested)
{
    Piece dest_top;

    if (requested != GUNGI_INTENT_AUTO) {
        return requested;
    }

    if (dest->height <= 0) {
        return GUNGI_INTENT_AUTO;
    }

    dest_top = top_of_cell(dest);
    if (dest_top.owner == moving.owner) {
        return GUNGI_INTENT_STACK;
    }

    return GUNGI_INTENT_CAPTURE;
}

static RulesResult validate_drop_basic(const GameState *state, Move move)
{
    const BoardCell *dest;
    Piece top;

    if (!valid_piece_type(move.drop_type)) {
        return make_result(0, GUNGI_RULE_ERR_BAD_PIECE, "invalid drop piece");
    }
    if (!in_bounds(move.to_x, move.to_y)) {
        return make_result(0, GUNGI_RULE_ERR_BAD_COORD, "drop target is outside the board");
    }
    if (state->hands[move.player][move.drop_type] <= 0) {
        return make_result(0, GUNGI_RULE_ERR_NEEDS_HAND, "piece is not available in hand");
    }
    if (!drop_row_allowed(state, move.player, move.to_y)) {
        return make_result(0, GUNGI_RULE_ERR_BAD_DROP, "New piece must be placed between own edge and most advanced piece");
    }

    dest = const_cell(state, move.to_x, move.to_y);
    if (dest->height >= GUNGI_MAX_STACK) {
        return make_result(0, GUNGI_RULE_ERR_FULL_STACK, "target stack is full");
    }

    top = top_of_cell(dest);
    if (dest->height > 0) {
        if (top.owner != move.player) {
            return make_result(0, GUNGI_RULE_ERR_BAD_DROP, "New piece can only be placed on empty squares or own pieces");
        }
        if (top.type == GUNGI_PIECE_MARSHAL) {
            return make_result(0, GUNGI_RULE_ERR_MARSHAL_STACK, "cannot stack on a Marshal");
        }
    }

    return make_result(1, GUNGI_RULE_OK, "ok");
}

static RulesResult validate_normal_basic(const GameState *state, Move move)
{
    const BoardCell *source;
    const BoardCell *dest;
    Piece moving;
    Piece dest_top;
    GungiMoveIntent intent;
    GungiRuleCode reach_fail = GUNGI_RULE_ERR_BAD_MOVE;

    if (!in_bounds(move.from_x, move.from_y) || !in_bounds(move.to_x, move.to_y)) {
        return make_result(0, GUNGI_RULE_ERR_BAD_COORD, "move coordinate is outside the board");
    }
    if (move.from_x == move.to_x && move.from_y == move.to_y) {
        return make_result(0, GUNGI_RULE_ERR_BAD_MOVE, "source and destination are the same");
    }

    source = const_cell(state, move.from_x, move.from_y);
    dest = const_cell(state, move.to_x, move.to_y);
    if (source->height <= 0) {
        return make_result(0, GUNGI_RULE_ERR_NO_PIECE, "source square is empty");
    }

    moving = top_of_cell(source);
    if (moving.owner != move.player) {
        return make_result(0, GUNGI_RULE_ERR_NOT_OWNER, "only the top piece owned by the mover can move");
    }

    if (!piece_can_reach(state, moving, source->height, move.from_x, move.from_y, move.to_x, move.to_y, &reach_fail)) {
        if (reach_fail == GUNGI_RULE_ERR_BLOCKED) {
            return make_result(0, GUNGI_RULE_ERR_BLOCKED, "movement path is blocked");
        }
        return make_result(0, GUNGI_RULE_ERR_BAD_MOVE, "piece cannot move to the target square");
    }

    if (move.intent != GUNGI_INTENT_AUTO && move.intent != GUNGI_INTENT_STACK && move.intent != GUNGI_INTENT_CAPTURE) {
        return make_result(0, GUNGI_RULE_ERR_BAD_INTENT, "invalid move intent");
    }

    intent = resolved_intent(dest, moving, move.intent);
    if (dest->height <= 0) {
        if (intent == GUNGI_INTENT_CAPTURE) {
            return make_result(0, GUNGI_RULE_ERR_BAD_INTENT, "cannot capture an empty square");
        }
        return make_result(1, GUNGI_RULE_OK, "ok");
    }

    dest_top = top_of_cell(dest);
    if (intent == GUNGI_INTENT_CAPTURE) {
        if (dest_top.owner == moving.owner) {
            return make_result(0, GUNGI_RULE_ERR_BAD_INTENT, "cannot capture an own piece");
        }
        if (source->height < dest->height) {
            return make_result(0, GUNGI_RULE_ERR_STACK_TOO_HIGH, "cannot capture a taller stack");
        }
        return make_result(1, GUNGI_RULE_OK, "ok");
    }

    if (intent == GUNGI_INTENT_STACK) {
        if (dest->height >= GUNGI_MAX_STACK) {
            return make_result(0, GUNGI_RULE_ERR_FULL_STACK, "target stack is full");
        }
        if (dest_top.type == GUNGI_PIECE_MARSHAL) {
            return make_result(0, GUNGI_RULE_ERR_MARSHAL_STACK, "cannot stack on a Marshal");
        }
        if (source->height < dest->height) {
            return make_result(0, GUNGI_RULE_ERR_STACK_TOO_HIGH, "cannot stack on a taller stack");
        }
        return make_result(1, GUNGI_RULE_OK, "ok");
    }

    return make_result(0, GUNGI_RULE_ERR_BAD_INTENT, "invalid destination intent");
}

static void execute_drop_unchecked(GameState *state, Move move)
{
    BoardCell *dest = mutable_cell(state, move.to_x, move.to_y);
    Piece piece;

    piece.type = move.drop_type;
    piece.owner = move.player;
    push_piece(dest, piece);
    state->hands[move.player][move.drop_type]--;
}

static void execute_normal_unchecked(GameState *state, Move move, RulesResult *result)
{
    BoardCell *source = mutable_cell(state, move.from_x, move.from_y);
    BoardCell *dest = mutable_cell(state, move.to_x, move.to_y);
    Piece moving = top_of_cell(source);
    GungiMoveIntent intent = resolved_intent(dest, moving, move.intent);

    moving = pop_piece(source);

    if (dest->height <= 0 || intent == GUNGI_INTENT_AUTO) {
        push_piece(dest, moving);
        return;
    }

    if (intent == GUNGI_INTENT_CAPTURE) {
        GungiPlayer captured_owner = gungi_opponent(moving.owner);
        int captured_marshal = 0;
        int captured = remove_owner_pieces(dest, captured_owner, &captured_marshal);
        if (result != NULL) {
            result->captured_count += captured;
            result->captured_marshal = result->captured_marshal || captured_marshal;
        }
        push_piece(dest, moving);
        return;
    }

    push_piece(dest, moving);
}

static void execute_move_unchecked(GameState *state, Move move, RulesResult *result)
{
    if (move.kind == GUNGI_MOVE_DROP) {
        execute_drop_unchecked(state, move);
    } else if (move.kind == GUNGI_MOVE_NORMAL) {
        execute_normal_unchecked(state, move, result);
    }
}

static RulesResult validate_basic(const GameState *state, Move move)
{
    if (state == NULL) {
        return make_result(0, GUNGI_RULE_ERR_NULL_STATE, "state is null");
    }
    if (!valid_player(move.player)) {
        return make_result(0, GUNGI_RULE_ERR_WRONG_TURN, "invalid player");
    }
    if (state->status != GUNGI_STATUS_ONGOING) {
        return make_result(0, GUNGI_RULE_ERR_GAME_OVER, "game is already over");
    }
    if (state->current_player != move.player) {
        return make_result(0, GUNGI_RULE_ERR_WRONG_TURN, "it is not this player's turn");
    }

    if (move.kind == GUNGI_MOVE_RESIGN) {
        return make_result(1, GUNGI_RULE_OK, "ok");
    }
    if (move.kind == GUNGI_MOVE_DROP) {
        return validate_drop_basic(state, move);
    }
    if (move.kind == GUNGI_MOVE_NORMAL) {
        return validate_normal_basic(state, move);
    }

    return make_result(0, GUNGI_RULE_ERR_BAD_MOVE, "unknown move kind");
}

static int record_position(GameState *state, uint64_t hash)
{
    int count;
    int i;

    if (state->repetition_len >= GUNGI_MAX_REPETITIONS) {
        memmove(&state->repetition_history[0], &state->repetition_history[1],
                sizeof(state->repetition_history[0]) * (GUNGI_MAX_REPETITIONS - 1));
        state->repetition_len = GUNGI_MAX_REPETITIONS - 1;
    }

    state->repetition_history[state->repetition_len] = hash;
    state->repetition_len++;

    count = 0;
    for (i = 0; i < state->repetition_len; ++i) {
        if (state->repetition_history[i] == hash) {
            count++;
        }
    }
    return count;
}

const PieceDef *gungi_get_piece_def(GungiPieceType type)
{
    if (type < GUNGI_PIECE_NONE || type >= GUNGI_PIECE_TYPE_COUNT) {
        return NULL;
    }
    return &PIECE_DEFS[type];
}

const char *gungi_piece_name(GungiPieceType type)
{
    const PieceDef *def = gungi_get_piece_def(type);
    return def == NULL ? "Unknown" : def->name;
}

GungiPlayer gungi_opponent(GungiPlayer player)
{
    if (player == GUNGI_PLAYER_BLACK) {
        return GUNGI_PLAYER_WHITE;
    }
    if (player == GUNGI_PLAYER_WHITE) {
        return GUNGI_PLAYER_BLACK;
    }
    return GUNGI_PLAYER_NONE;
}

void gungi_clear(GameState *state)
{
    if (state == NULL) {
        return;
    }

    memset(state, 0, sizeof(*state));
    state->current_player = GUNGI_PLAYER_BLACK;
    state->status = GUNGI_STATUS_ONGOING;
    state->winner = GUNGI_PLAYER_NONE;
}

int gungi_place_piece(GameState *state, GungiPlayer owner, GungiPieceType type, int x, int y)
{
    Piece piece;

    if (state == NULL || !in_bounds(x, y) || !valid_player(owner) || !valid_piece_type(type)) {
        return 0;
    }

    piece.type = type;
    piece.owner = owner;
    return push_piece(mutable_cell(state, x, y), piece);
}

void gungi_set_hand(GameState *state, GungiPlayer player, GungiPieceType type, int count)
{
    if (state == NULL || !valid_player(player) || !valid_piece_type(type)) {
        return;
    }
    state->hands[player][type] = count < 0 ? 0 : count;
}

void gungi_set_current_player(GameState *state, GungiPlayer player)
{
    if (state != NULL && valid_player(player)) {
        state->current_player = player;
    }
}

void gungi_init(GameState *state)
{
    int player;
    size_t i;

    if (state == NULL) {
        return;
    }

    gungi_clear(state);

    for (player = GUNGI_PLAYER_BLACK; player <= GUNGI_PLAYER_WHITE; ++player) {
        int type;
        for (type = GUNGI_PIECE_NONE + 1; type < GUNGI_PIECE_TYPE_COUNT; ++type) {
            state->hands[player][type] = PIECE_DEFS[type].inventory_count;
        }
    }

    for (i = 0; i < sizeof(DEFAULT_SETUP) / sizeof(DEFAULT_SETUP[0]); ++i) {
        const SetupPiece *piece = &DEFAULT_SETUP[i];
        if (gungi_place_piece(state, GUNGI_PLAYER_BLACK, piece->type, piece->x, piece->y)) {
            state->hands[GUNGI_PLAYER_BLACK][piece->type]--;
        }
        if (gungi_place_piece(state, GUNGI_PLAYER_WHITE, piece->type, GUNGI_BOARD_SIZE - 1 - piece->x, GUNGI_BOARD_SIZE - 1 - piece->y)) {
            state->hands[GUNGI_PLAYER_WHITE][piece->type]--;
        }
    }

    gungi_reset_repetition(state);
}

void gungi_reset_repetition(GameState *state)
{
    if (state == NULL) {
        return;
    }
    state->repetition_len = 0;
    record_position(state, gungi_position_hash(state));
}

uint64_t gungi_position_hash(const GameState *state)
{
    uint64_t hash = 1469598103934665603ULL;
    int y;
    int x;
    int level;
    int player;
    int type;

    if (state == NULL) {
        return 0;
    }

#define FNV_ADD(v) \
    do { \
        hash ^= (uint64_t)(unsigned int)(v); \
        hash *= 1099511628211ULL; \
    } while (0)

    FNV_ADD(state->current_player + 2);
    for (y = 0; y < GUNGI_BOARD_SIZE; ++y) {
        for (x = 0; x < GUNGI_BOARD_SIZE; ++x) {
            const BoardCell *cell = const_cell(state, x, y);
            FNV_ADD(cell->height);
            for (level = 0; level < GUNGI_MAX_STACK; ++level) {
                if (level < cell->height) {
                    FNV_ADD(cell->stack[level].type);
                    FNV_ADD(cell->stack[level].owner + 2);
                } else {
                    FNV_ADD(0);
                    FNV_ADD(0);
                }
            }
        }
    }

    for (player = GUNGI_PLAYER_BLACK; player <= GUNGI_PLAYER_WHITE; ++player) {
        for (type = GUNGI_PIECE_NONE + 1; type < GUNGI_PIECE_TYPE_COUNT; ++type) {
            FNV_ADD(state->hands[player][type]);
        }
    }

#undef FNV_ADD

    return hash;
}

int gungi_count_repetition(const GameState *state, uint64_t hash)
{
    int count = 0;
    int i;

    if (state == NULL) {
        return 0;
    }
    for (i = 0; i < state->repetition_len; ++i) {
        if (state->repetition_history[i] == hash) {
            count++;
        }
    }
    return count;
}

Move gungi_make_move(GungiPlayer player, int from_x, int from_y, int to_x, int to_y)
{
    Move move;
    memset(&move, 0, sizeof(move));
    move.kind = GUNGI_MOVE_NORMAL;
    move.player = player;
    move.from_x = from_x;
    move.from_y = from_y;
    move.to_x = to_x;
    move.to_y = to_y;
    move.drop_type = GUNGI_PIECE_NONE;
    move.intent = GUNGI_INTENT_AUTO;
    return move;
}

Move gungi_make_stack_move(GungiPlayer player, int from_x, int from_y, int to_x, int to_y)
{
    Move move = gungi_make_move(player, from_x, from_y, to_x, to_y);
    move.intent = GUNGI_INTENT_STACK;
    return move;
}

Move gungi_make_capture_move(GungiPlayer player, int from_x, int from_y, int to_x, int to_y)
{
    Move move = gungi_make_move(player, from_x, from_y, to_x, to_y);
    move.intent = GUNGI_INTENT_CAPTURE;
    return move;
}

Move gungi_make_drop(GungiPlayer player, GungiPieceType type, int to_x, int to_y)
{
    Move move;
    memset(&move, 0, sizeof(move));
    move.kind = GUNGI_MOVE_DROP;
    move.player = player;
    move.from_x = -1;
    move.from_y = -1;
    move.to_x = to_x;
    move.to_y = to_y;
    move.drop_type = type;
    move.intent = GUNGI_INTENT_AUTO;
    return move;
}

Move gungi_make_resign(GungiPlayer player)
{
    Move move;
    memset(&move, 0, sizeof(move));
    move.kind = GUNGI_MOVE_RESIGN;
    move.player = player;
    move.from_x = -1;
    move.from_y = -1;
    move.to_x = -1;
    move.to_y = -1;
    move.drop_type = GUNGI_PIECE_NONE;
    move.intent = GUNGI_INTENT_AUTO;
    return move;
}

RulesResult gungi_validate_move(const GameState *state, Move move)
{
    RulesResult result = validate_basic(state, move);
    GameState trial;
    GungiPlayer opponent;

    if (!result.ok) {
        return result;
    }
    if (move.kind == GUNGI_MOVE_RESIGN) {
        return result;
    }

    trial = *state;
    execute_move_unchecked(&trial, move, &result);

    if (owner_has_marshal(&trial, move.player) && gungi_is_in_check(&trial, move.player)) {
        return make_result(0, GUNGI_RULE_ERR_SELF_CHECK, "move leaves own Marshal in check");
    }

    opponent = gungi_opponent(move.player);
    result.gives_check = owner_has_marshal(&trial, opponent) && gungi_is_in_check(&trial, opponent);
    return result;
}

RulesResult gungi_apply_move(GameState *state, Move move)
{
    RulesResult result = gungi_validate_move(state, move);
    uint64_t hash;
    int repetitions;

    if (!result.ok || state == NULL) {
        return result;
    }

    if (move.kind == GUNGI_MOVE_RESIGN) {
        state->status = GUNGI_STATUS_RESIGNED;
        state->winner = gungi_opponent(move.player);
        result.status = state->status;
        result.winner = state->winner;
        return result;
    }

    result.captured_count = 0;
    result.captured_marshal = 0;
    execute_move_unchecked(state, move, &result);
    state->ply_count++;

    if (result.captured_marshal) {
        state->status = (move.player == GUNGI_PLAYER_BLACK) ? GUNGI_STATUS_BLACK_WIN : GUNGI_STATUS_WHITE_WIN;
        state->winner = move.player;
        result.status = state->status;
        result.winner = state->winner;
        return result;
    }

    state->current_player = gungi_opponent(state->current_player);
    result.gives_check = owner_has_marshal(state, state->current_player) && gungi_is_in_check(state, state->current_player);

    hash = gungi_position_hash(state);
    repetitions = record_position(state, hash);
    if (repetitions >= GUNGI_REPETITION_DRAW_COUNT) {
        state->status = GUNGI_STATUS_DRAW;
        state->winner = GUNGI_PLAYER_NONE;
        result.draw = 1;
    }

    result.status = state->status;
    result.winner = state->winner;
    return result;
}

int gungi_is_square_attacked(const GameState *state, int x, int y, GungiPlayer by_player)
{
    int source_y;
    int source_x;
    const BoardCell *target;

    if (state == NULL || !in_bounds(x, y) || !valid_player(by_player)) {
        return 0;
    }

    target = const_cell(state, x, y);
    for (source_y = 0; source_y < GUNGI_BOARD_SIZE; ++source_y) {
        for (source_x = 0; source_x < GUNGI_BOARD_SIZE; ++source_x) {
            const BoardCell *source = const_cell(state, source_x, source_y);
            Piece attacker = top_of_cell(source);
            GungiRuleCode fail_code = GUNGI_RULE_OK;

            if (source->height <= 0 || attacker.owner != by_player) {
                continue;
            }
            if (source_x == x && source_y == y) {
                continue;
            }
            if (!piece_can_reach(state, attacker, source->height, source_x, source_y, x, y, &fail_code)) {
                continue;
            }
            if (target->height > 0 && source->height < target->height) {
                continue;
            }
            return 1;
        }
    }

    return 0;
}

int gungi_is_in_check(const GameState *state, GungiPlayer player)
{
    int marshal_x;
    int marshal_y;

    if (state == NULL || !valid_player(player)) {
        return 0;
    }
    if (!find_top_marshal(state, player, &marshal_x, &marshal_y)) {
        return 0;
    }

    return gungi_is_square_attacked(state, marshal_x, marshal_y, gungi_opponent(player));
}

int gungi_cell_height(const GameState *state, int x, int y)
{
    if (state == NULL || !in_bounds(x, y)) {
        return 0;
    }
    return const_cell(state, x, y)->height;
}

Piece gungi_top_piece(const GameState *state, int x, int y)
{
    if (state == NULL || !in_bounds(x, y)) {
        return EMPTY_PIECE;
    }
    return top_of_cell(const_cell(state, x, y));
}

Piece gungi_stack_piece(const GameState *state, int x, int y, int level)
{
    const BoardCell *cell;

    if (state == NULL || !in_bounds(x, y) || level < 0 || level >= GUNGI_MAX_STACK) {
        return EMPTY_PIECE;
    }

    cell = const_cell(state, x, y);
    if (level >= cell->height) {
        return EMPTY_PIECE;
    }
    return cell->stack[level];
}

int gungi_hand_count(const GameState *state, GungiPlayer player, GungiPieceType type)
{
    if (state == NULL || !valid_player(player) || !valid_piece_type(type)) {
        return 0;
    }
    return state->hands[player][type];
}

static void set_game_error(GungiGame *game, const char *message)
{
    if (game == NULL) {
        return;
    }
    if (message == NULL || message[0] == '\0') {
        game->last_error[0] = '\0';
        return;
    }
    snprintf(game->last_error, sizeof(game->last_error), "%s", message);
}

static void format_status(const GameState *state, char *buffer, size_t buffer_size)
{
    const char *player;

    if (buffer == NULL || buffer_size == 0) {
        return;
    }
    buffer[0] = '\0';
    if (state == NULL) {
        snprintf(buffer, buffer_size, "No game state.");
        return;
    }

    if (state->status == GUNGI_STATUS_DRAW) {
        snprintf(buffer, buffer_size, "Draw by fourfold repetition.");
        return;
    }
    if (state->status == GUNGI_STATUS_RESIGNED) {
        snprintf(buffer, buffer_size, "%s wins by resignation.",
                 state->winner == GUNGI_PLAYER_BLACK ? "Black" : "White");
        return;
    }
    if (state->status == GUNGI_STATUS_BLACK_WIN || state->status == GUNGI_STATUS_WHITE_WIN) {
        snprintf(buffer, buffer_size, "%s wins by capturing the Marshal.",
                 state->winner == GUNGI_PLAYER_BLACK ? "Black" : "White");
        return;
    }

    player = state->current_player == GUNGI_PLAYER_BLACK ? "Black" : "White";
    if (gungi_is_in_check(state, state->current_player)) {
        snprintf(buffer, buffer_size, "%s to move. Marshal is in check.", player);
    } else {
        snprintf(buffer, buffer_size, "%s to move.", player);
    }
}

static GungiPieceType hand_piece_at_index(const GameState *state, GungiPlayer player, int hand_index)
{
    int index = 0;
    int type;

    if (state == NULL || !valid_player(player) || hand_index < 0) {
        return GUNGI_PIECE_NONE;
    }

    for (type = GUNGI_PIECE_NONE + 1; type < GUNGI_PIECE_TYPE_COUNT; ++type) {
        if (state->hands[player][type] <= 0) {
            continue;
        }
        if (index == hand_index) {
            return (GungiPieceType)type;
        }
        index++;
    }

    return GUNGI_PIECE_NONE;
}

GungiGame *gungi_create(void)
{
    GungiGame *game = (GungiGame *)malloc(sizeof(*game));
    if (game == NULL) {
        return NULL;
    }
    game->last_error[0] = '\0';
    gungi_init(&game->state);
    return game;
}

void gungi_destroy(GungiGame *game)
{
    free(game);
}

void gungi_reset(GungiGame *game)
{
    if (game == NULL) {
        return;
    }
    gungi_init(&game->state);
    set_game_error(game, NULL);
}

bool gungi_get_view(const GungiGame *game, GungiGameView *out_view)
{
    int y;
    int x;
    int player;
    int type;

    if (game == NULL || out_view == NULL) {
        return false;
    }

    memset(out_view, 0, sizeof(*out_view));
    out_view->current_player = game->state.current_player;
    out_view->phase = game->state.status == GUNGI_STATUS_ONGOING ? GUNGI_PHASE_PLAYING : GUNGI_PHASE_FINISHED;
    out_view->winner = game->state.winner;

    for (y = 0; y < GUNGI_BOARD_SIZE; ++y) {
        for (x = 0; x < GUNGI_BOARD_SIZE; ++x) {
            const BoardCell *source = const_cell(&game->state, x, y);
            GungiCellView *dest = &out_view->board[y][x];
            int level;
            dest->height = source->height;
            if (dest->height > GUNGI_MAX_STACK) {
                dest->height = GUNGI_MAX_STACK;
            }
            for (level = 0; level < dest->height; ++level) {
                const PieceDef *def = gungi_get_piece_def(source->stack[level].type);
                snprintf(dest->stack[level].code, sizeof(dest->stack[level].code), "%s",
                         def == NULL ? "??" : def->short_name);
                dest->stack[level].owner = source->stack[level].owner;
            }
        }
    }

    for (player = GUNGI_PLAYER_BLACK; player <= GUNGI_PLAYER_WHITE; ++player) {
        for (type = GUNGI_PIECE_NONE + 1; type < GUNGI_PIECE_TYPE_COUNT; ++type) {
            int count = game->state.hands[player][type];
            if (count > 0 && out_view->hand_count[player] < GUNGI_MAX_HAND_ITEMS) {
                GungiHandEntryView *entry = &out_view->hands[player][out_view->hand_count[player]];
                const PieceDef *def = gungi_get_piece_def((GungiPieceType)type);
                snprintf(entry->code, sizeof(entry->code), "%s", def == NULL ? "??" : def->short_name);
                entry->count = count;
                out_view->hand_count[player]++;
            }
        }
    }

    format_status(&game->state, out_view->status, sizeof(out_view->status));
    return true;
}

bool gungi_apply_request(GungiGame *game, const GungiMoveRequest *request)
{
    Move move;
    RulesResult result;

    if (game == NULL || request == NULL) {
        return false;
    }
    if (request->player != game->state.current_player) {
        set_game_error(game, "It is not that player's turn.");
        return false;
    }

    switch (request->action) {
    case GUNGI_ACTION_NEW: {
        GungiPieceType type = hand_piece_at_index(&game->state, request->player, request->hand_index);
        if (type == GUNGI_PIECE_NONE) {
            set_game_error(game, "Selected hand slot is empty.");
            return false;
        }
        move = gungi_make_drop(request->player, type, request->to_x, request->to_y);
        break;
    }
    case GUNGI_ACTION_CAPTURE:
        move = gungi_make_capture_move(request->player, request->from_x, request->from_y, request->to_x, request->to_y);
        break;
    case GUNGI_ACTION_STACK:
        move = gungi_make_stack_move(request->player, request->from_x, request->from_y, request->to_x, request->to_y);
        break;
    case GUNGI_ACTION_MOVE:
        move = gungi_make_move(request->player, request->from_x, request->from_y, request->to_x, request->to_y);
        break;
    default:
        set_game_error(game, "Unknown action.");
        return false;
    }

    result = gungi_apply_move(&game->state, move);
    if (!result.ok) {
        set_game_error(game, result.message);
        return false;
    }

    if (result.captured_marshal) {
        set_game_error(game, "Marshal captured.");
    } else if (result.draw) {
        set_game_error(game, "Draw by fourfold repetition.");
    } else if (result.gives_check) {
        set_game_error(game, "Check.");
    } else {
        set_game_error(game, NULL);
    }
    return true;
}

bool gungi_resign(GungiGame *game, GungiPlayer player)
{
    RulesResult result;

    if (game == NULL) {
        return false;
    }
    result = gungi_apply_move(&game->state, gungi_make_resign(player));
    if (!result.ok) {
        set_game_error(game, result.message);
        return false;
    }
    set_game_error(game, "Player resigned.");
    return true;
}

const char *gungi_last_error(const GungiGame *game)
{
    if (game == NULL) {
        return "No game.";
    }
    return game->last_error;
}

// --- 新增：暴露內部 GameState 給 AI 使用的通道 ---
GameState *gungi_game_get_state(GungiGame *game)
{
    if (game == NULL) {
        return NULL;
    }
    return &game->state;
}