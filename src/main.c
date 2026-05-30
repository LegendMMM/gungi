#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <stdlib.h>

#include "raylib.h"
#include "gungi_rules.h"
#include "ai_core.h"

#define WINDOW_WIDTH 1180
#define WINDOW_HEIGHT 790
#define CELL_SIZE 64
#define HAND_WIDTH 220
#define HAND_ROW_HEIGHT 30
#define BUTTON_HEIGHT 32
#define MAX_HISTORY 256
#define TARGET_MOVE 1
#define TARGET_CAPTURE 2
#define TARGET_STACK 4

typedef unsigned char UiTargetFlags;

typedef enum UiSourceKind {
    UI_SOURCE_NONE = 0,
    UI_SOURCE_BOARD = 1,
    UI_SOURCE_HAND = 2
} UiSourceKind;

typedef struct UiSelection {
    UiSourceKind kind;
    int x;
    int y;
    int level;
    int hand_index;
    GungiPlayer player;
} UiSelection;

typedef enum PlayerControlType {
    CONTROL_MANUAL = 0,
    CONTROL_RANDOM_AI = 1,
    CONTROL_GREEDY_AI = 2,
    CONTROL_MINIMAX_AI = 3
} PlayerControlType;

typedef struct AppState {
    GungiGame *game;
    GungiGameView view;
    bool has_view;
    GungiActionType action;
    UiSelection selection;
    char message[GUNGI_STATUS_LEN];
    Rectangle board_rect;
    Rectangle black_hand_rect;
    Rectangle white_hand_rect;
    Rectangle action_buttons[5];
    Rectangle restart_button;
    Rectangle resign_button;
    bool auto_play;

    bool betray_layer0; 
    bool betray_layer1; 
    Rectangle toggle_l0_btn;
    Rectangle toggle_l1_btn;

    GameState history[MAX_HISTORY];
    int history_count;
    int total_turns;
    UiTargetFlags valid_targets[GUNGI_BOARD_SIZE][GUNGI_BOARD_SIZE];
    int target_count;

    PlayerControlType black_control;
    PlayerControlType white_control;
    Rectangle black_ctrl_btns[4];
    Rectangle white_ctrl_btns[4];

    double black_ai_time;
    int black_ai_moves;
    double white_ai_time;
    int white_ai_moves;

} AppState;

static const Color COLOR_BG = { 29, 32, 36, 255 };
static const Color COLOR_PANEL = { 42, 47, 52, 255 };
static const Color COLOR_PANEL_ACTIVE = { 54, 68, 76, 255 };
static const Color COLOR_BOARD_A = { 194, 162, 106, 255 };
static const Color COLOR_BOARD_B = { 178, 145, 92, 255 };
static const Color COLOR_GRID = { 61, 45, 30, 255 };
static const Color COLOR_TEXT = { 235, 235, 228, 255 };
static const Color COLOR_MUTED = { 167, 172, 174, 255 };
static const Color COLOR_ACCENT = { 42, 147, 114, 255 };
static const Color COLOR_WARN = { 198, 84, 68, 255 };
static const Color COLOR_BLACK_PIECE = { 38, 43, 48, 255 };
static const Color COLOR_WHITE_PIECE = { 232, 229, 212, 255 };
static const Color COLOR_SELECTED = { 255, 214, 99, 255 };

static const char *PlayerName(GungiPlayer player)
{
    switch (player) {
    case GUNGI_PLAYER_BLACK:
        return "Black";
    case GUNGI_PLAYER_WHITE:
        return "White";
    default:
        return "None";
    }
}

static const char *ActionName(GungiActionType action)
{
    switch (action) {
    case GUNGI_ACTION_NEW:
        return "New";
    case GUNGI_ACTION_MOVE:
        return "Move";
    case GUNGI_ACTION_CAPTURE:
        return "Capture";
    case GUNGI_ACTION_STACK:
        return "Stack";
    default:
        return "Move";
    }
}

static Color PlayerPieceColor(GungiPlayer player)
{
    return (player == GUNGI_PLAYER_WHITE) ? COLOR_WHITE_PIECE : COLOR_BLACK_PIECE;
}

static Color PlayerTextColor(GungiPlayer player)
{
    return (player == GUNGI_PLAYER_WHITE) ? COLOR_BLACK_PIECE : COLOR_TEXT;
}

static void SetMessage(AppState *state, const char *text)
{
    if (text == NULL || text[0] == '\0') {
        state->message[0] = '\0';
        return;
    }

    snprintf(state->message, sizeof(state->message), "%s", text);
}

static void ClearSelection(AppState *state)
{
    state->selection.kind = UI_SOURCE_NONE;
    state->selection.x = -1;
    state->selection.y = -1;
    state->selection.level = -1;
    state->selection.hand_index = -1;
    state->selection.player = (GungiPlayer)-1;
    state->target_count = 0;
    
    memset(state->valid_targets, 0, sizeof(state->valid_targets));
}

static void RefreshView(AppState *state)
{
    memset(&state->view, 0, sizeof(state->view));

    if (state->game == NULL) {
        state->has_view = false;
        SetMessage(state, "Rules engine did not create a game.");
        return;
    }

    state->has_view = gungi_get_view(state->game, &state->view);
    if (!state->has_view) {
        const char *error = gungi_last_error(state->game);
        SetMessage(state, (error != NULL && error[0] != '\0') ? error : "Rules engine could not produce a view.");
        return;
    }

    if (state->view.status[0] != '\0') {
        SetMessage(state, state->view.status);
    }
}

static void LayoutApp(AppState *state)
{
    state->black_hand_rect = (Rectangle){ 24.0f, 92.0f, HAND_WIDTH, 590.0f };
    state->board_rect = (Rectangle){ 302.0f, 92.0f, CELL_SIZE * GUNGI_BOARD_SIZE, CELL_SIZE * GUNGI_BOARD_SIZE };
    state->white_hand_rect = (Rectangle){ 936.0f, 92.0f, HAND_WIDTH, 590.0f };

    state->black_control = CONTROL_MANUAL;
    state->white_control = CONTROL_MANUAL;
    
    for (int i = 0; i < 4; i++) {
        float btn_x_offset = 14.0f + (float)i * 38.0f;
        state->black_ctrl_btns[i] = (Rectangle){ state->black_hand_rect.x + btn_x_offset, state->black_hand_rect.y + 470.0f, 42.0f, 30.0f };
        state->white_ctrl_btns[i] = (Rectangle){ state->white_hand_rect.x + btn_x_offset, state->white_hand_rect.y + 470.0f, 42.0f, 30.0f };
    }

    float action_x = state->board_rect.x;
    const char *labels[] = { "New", "Move", "Capture", "Stack", "Betrayal" };
    (void)labels;
    for (int i = 0; i < 5; i++) {
        state->action_buttons[i] = (Rectangle){
            action_x + (float)i * 116.0f,
            36.0f,
            104.0f,
            BUTTON_HEIGHT
        };
    }

    float betray_x = state->action_buttons[4].x + state->action_buttons[4].width + 12.0f;
    state->toggle_l0_btn = (Rectangle){ betray_x, 32.0f, 18.0f, 18.0f };
    state->toggle_l1_btn = (Rectangle){ betray_x, 52.0f, 18.0f, 18.0f };

    state->restart_button = (Rectangle){ 930.0f, 36.0f, 100.0f, BUTTON_HEIGHT };
    state->resign_button = (Rectangle){ 1042.0f, 36.0f, 96.0f, BUTTON_HEIGHT };
}

static bool PointToBoardCell(const AppState *state, Vector2 point, int *out_x, int *out_y)
{
    if (!CheckCollisionPointRec(point, state->board_rect)) {
        return false;
    }

    int x = (int)((point.x - state->board_rect.x) / CELL_SIZE);
    int y = (int)((point.y - state->board_rect.y) / CELL_SIZE);
    if (x < 0 || x >= GUNGI_BOARD_SIZE || y < 0 || y >= GUNGI_BOARD_SIZE) {
        return false;
    }

    *out_x = x;
    *out_y = y;
    return true;
}

static Rectangle HandEntryRect(Rectangle panel, int index)
{
    return (Rectangle){
        panel.x + 14.0f,
        panel.y + 54.0f + (float)index * HAND_ROW_HEIGHT,
        panel.width - 28.0f,
        HAND_ROW_HEIGHT - 4.0f
    };
}

static bool PointToHandEntry(const AppState *state, Vector2 point, GungiPlayer player, int *out_index)
{
    int player_index = (int)player;
    if (player_index < 0 || player_index > 1) {
        return false;
    }

    Rectangle panel = (player == GUNGI_PLAYER_BLACK) ? state->black_hand_rect : state->white_hand_rect;
    if (!CheckCollisionPointRec(point, panel)) {
        return false;
    }

    int count = state->view.hand_count[player_index];
    if (count > GUNGI_MAX_HAND_ITEMS) {
        count = GUNGI_MAX_HAND_ITEMS;
    }

    for (int i = 0; i < count; i++) {
        Rectangle row = HandEntryRect(panel, i);
        if (CheckCollisionPointRec(point, row)) {
            *out_index = i;
            return true;
        }
    }

    return false;
}

static bool CellHasCurrentPlayerTop(const AppState *state, int x, int y)
{
    const GungiCellView *cell;
    int top;

    if (state == NULL || !state->has_view || x < 0 || x >= GUNGI_BOARD_SIZE || y < 0 || y >= GUNGI_BOARD_SIZE) {
        return false;
    }

    cell = &state->view.board[y][x];
    if (cell->height <= 0) {
        return false;
    }

    top = cell->height - 1;
    if (top >= GUNGI_MAX_STACK) {
        top = GUNGI_MAX_STACK - 1;
    }

    return cell->stack[top].owner == state->view.current_player;
}

static UiTargetFlags BoardTargetFlags(const GameState *internal, GungiPlayer player, int from_x, int from_y, int to_x, int to_y)
{
    UiTargetFlags flags = 0;
    int target_height;

    if (internal == NULL) {
        return 0;
    }

    target_height = gungi_cell_height(internal, to_x, to_y);
    if (target_height <= 0) {
        Move move = gungi_make_move(player, from_x, from_y, to_x, to_y);
        if (gungi_validate_move(internal, move).ok) {
            flags |= TARGET_MOVE;
        }
        return flags;
    }

    {
        Move capture = gungi_make_capture_move(player, from_x, from_y, to_x, to_y);
        if (gungi_validate_move(internal, capture).ok) {
            flags |= TARGET_CAPTURE;
        }
    }

    {
        Move stack = gungi_make_stack_move(player, from_x, from_y, to_x, to_y);
        if (gungi_validate_move(internal, stack).ok) {
            flags |= TARGET_STACK;
        }
    }

    return flags;
}

static int ComputeBoardTargets(AppState *state, int from_x, int from_y)
{
    GameState *internal = gungi_game_get_state(state->game);
    int count = 0;

    memset(state->valid_targets, 0, sizeof(state->valid_targets));
    state->target_count = 0;

    if (internal == NULL) {
        return 0;
    }

    for (int ty = 0; ty < GUNGI_BOARD_SIZE; ty++) {
        for (int tx = 0; tx < GUNGI_BOARD_SIZE; tx++) {
            UiTargetFlags flags = BoardTargetFlags(internal, internal->current_player, from_x, from_y, tx, ty);
            state->valid_targets[ty][tx] = flags;
            if (flags != 0) {
                count++;
            }
        }
    }

    state->target_count = count;
    return count;
}

static bool TargetSupportsAction(UiTargetFlags flags, GungiActionType action)
{
    if (action == GUNGI_ACTION_MOVE) {
        return (flags & TARGET_MOVE) != 0;
    }
    if (action == GUNGI_ACTION_CAPTURE) {
        return (flags & TARGET_CAPTURE) != 0;
    }
    if (action == GUNGI_ACTION_STACK) {
        return (flags & TARGET_STACK) != 0;
    }
    return false;
}

static GungiActionType PreferredTargetAction(const AppState *state, int to_x, int to_y)
{
    UiTargetFlags flags = state->valid_targets[to_y][to_x];

    if (TargetSupportsAction(flags, state->action)) {
        return state->action;
    }
    if ((flags & TARGET_CAPTURE) != 0) {
        return GUNGI_ACTION_CAPTURE;
    }
    if ((flags & TARGET_STACK) != 0) {
        return GUNGI_ACTION_STACK;
    }
    return GUNGI_ACTION_MOVE;
}

static Color TargetHintColor(UiTargetFlags flags)
{
    if ((flags & TARGET_CAPTURE) != 0) {
        return (Color){ 213, 74, 69, 128 };
    }
    if ((flags & TARGET_STACK) != 0) {
        return (Color){ 245, 185, 66, 128 };
    }
    return (Color){ 74, 144, 226, 128 };
}

static void DrawCenteredText(const char *text, Rectangle rect, int font_size, Color color)
{
    int width = MeasureText(text, font_size);
    int x = (int)(rect.x + (rect.width - (float)width) * 0.5f);
    int y = (int)(rect.y + (rect.height - (float)font_size) * 0.5f);
    DrawText(text, x, y, font_size, color);
}

static bool DrawButton(Rectangle rect, const char *text, bool active, Color accent)
{
    Vector2 mouse = GetMousePosition();
    bool hovered = CheckCollisionPointRec(mouse, rect);
    Color fill = active ? accent : COLOR_PANEL;
    if (hovered && !active) {
        fill = (Color){ 57, 63, 68, 255 };
    }

    DrawRectangleRounded(rect, 0.12f, 8, fill);
    DrawRectangleLinesEx(rect, 1.0f, hovered ? accent : (Color){ 84, 90, 94, 255 });
    DrawCenteredText(text, rect, 18, COLOR_TEXT);

    return hovered && IsMouseButtonPressed(MOUSE_LEFT_BUTTON);
}

static void DrawPieceToken(Rectangle rect, const GungiPieceView *piece, int stack_height)
{
    Color fill = PlayerPieceColor(piece->owner);
    Color text = PlayerTextColor(piece->owner);
    float radius = rect.width < rect.height ? rect.width * 0.42f : rect.height * 0.42f;
    Vector2 center = { rect.x + rect.width * 0.5f, rect.y + rect.height * 0.5f };

    DrawCircleV(center, radius, fill);
    DrawCircleLines((int)center.x, (int)center.y, radius, COLOR_GRID);
    DrawCenteredText(piece->code, rect, 20, text);

    if (stack_height > 1) {
        DrawText(TextFormat("%d", stack_height), (int)(rect.x + 6.0f), (int)(rect.y + 5.0f), 16, COLOR_SELECTED);
    }
}

static void DrawBoard(const AppState *state)
{
    DrawRectangleRec(state->board_rect, COLOR_GRID);

    for (int y = 0; y < GUNGI_BOARD_SIZE; y++) {
        for (int x = 0; x < GUNGI_BOARD_SIZE; x++) {
            Rectangle cell_rect = {
                state->board_rect.x + (float)x * CELL_SIZE,
                state->board_rect.y + (float)y * CELL_SIZE,
                CELL_SIZE,
                CELL_SIZE
            };
            Color cell_color = ((x + y) % 2 == 0) ? COLOR_BOARD_A : COLOR_BOARD_B;
            DrawRectangleRec(cell_rect, cell_color);
            DrawRectangleLinesEx(cell_rect, 1.0f, COLOR_GRID);

            if (state->valid_targets[y][x] != 0) {
                UiTargetFlags flags = state->valid_targets[y][x];
                Color hint = TargetHintColor(flags);
                DrawRectangleRec(cell_rect, hint);
                DrawRectangleLinesEx(cell_rect, 2.0f, (Color){ hint.r, hint.g, hint.b, 255 });

                if ((flags & (flags - 1)) != 0) {
                    float dot_x = cell_rect.x + 12.0f;
                    float dot_y = cell_rect.y + cell_rect.height - 10.0f;
                    if ((flags & TARGET_MOVE) != 0) {
                        DrawCircleV((Vector2){ dot_x, dot_y }, 4.0f, (Color){ 74, 144, 226, 255 });
                        dot_x += 12.0f;
                    }
                    if ((flags & TARGET_CAPTURE) != 0) {
                        DrawCircleV((Vector2){ dot_x, dot_y }, 4.0f, (Color){ 213, 74, 69, 255 });
                        dot_x += 12.0f;
                    }
                    if ((flags & TARGET_STACK) != 0) {
                        DrawCircleV((Vector2){ dot_x, dot_y }, 4.0f, (Color){ 245, 185, 66, 255 });
                    }
                }
            }

            bool selected = state->selection.kind == UI_SOURCE_BOARD &&
                            state->selection.x == x &&
                            state->selection.y == y;
            if (selected) {
                DrawRectangleLinesEx((Rectangle){ cell_rect.x + 2.0f, cell_rect.y + 2.0f, cell_rect.width - 4.0f, cell_rect.height - 4.0f },
                                     3.0f, COLOR_SELECTED);
            }

            const GungiCellView *cell = &state->view.board[y][x];
            if (cell->height > 0) {
                int top = cell->height - 1;
                if (top >= GUNGI_MAX_STACK) {
                    top = GUNGI_MAX_STACK - 1;
                }
                DrawPieceToken(cell_rect, &cell->stack[top], cell->height);
            }
        }
    }

    for (int i = 0; i < GUNGI_BOARD_SIZE; i++) {
        DrawText(TextFormat("%d", i + 1),
                 (int)(state->board_rect.x - 22.0f),
                 (int)(state->board_rect.y + (float)i * CELL_SIZE + 22.0f),
                 18,
                 COLOR_MUTED);
        DrawText(TextFormat("%d", i + 1),
                 (int)(state->board_rect.x + (float)i * CELL_SIZE + 26.0f),
                 (int)(state->board_rect.y + state->board_rect.height + 8.0f),
                 18,
                 COLOR_MUTED);
    }
}

static void DrawHandPanel(AppState *state, GungiPlayer player, Rectangle panel)
{
    int player_index = (int)player;
    bool active = state->has_view && state->view.current_player == player;
    DrawRectangleRounded(panel, 0.05f, 8, active ? COLOR_PANEL_ACTIVE : COLOR_PANEL);
    DrawRectangleLinesEx(panel, 1.0f, active ? COLOR_ACCENT : (Color){ 80, 86, 91, 255 });

    DrawText(TextFormat("%s Hand", PlayerName(player)), (int)(panel.x + 14.0f), (int)(panel.y + 15.0f), 22, COLOR_TEXT);

    if (!state->has_view || player_index < 0 || player_index > 1) {
        return;
    }

    int count = state->view.hand_count[player_index];
    if (count > GUNGI_MAX_HAND_ITEMS) {
        count = GUNGI_MAX_HAND_ITEMS;
    }

    for (int i = 0; i < count; i++) {
        const GungiHandEntryView *entry = &state->view.hands[player_index][i];
        Rectangle row = HandEntryRect(panel, i);
        bool selected = state->selection.kind == UI_SOURCE_HAND &&
                        state->selection.player == player &&
                        state->selection.hand_index == i;
        Color row_color = selected ? (Color){ 117, 93, 39, 255 } : (Color){ 55, 60, 64, 255 };
        DrawRectangleRounded(row, 0.18f, 6, row_color);
        DrawRectangleLinesEx(row, 1.0f, selected ? COLOR_SELECTED : (Color){ 76, 82, 87, 255 });
        DrawText(entry->code, (int)(row.x + 10.0f), (int)(row.y + 5.0f), 18, COLOR_TEXT);
        DrawText(TextFormat("x%d", entry->count), (int)(row.x + row.width - 48.0f), (int)(row.y + 5.0f), 18, COLOR_MUTED);
    }

    if (count == 0) {
        DrawText("No pieces", (int)(panel.x + 14.0f), (int)(panel.y + 58.0f), 18, COLOR_MUTED);
    }

    const char *ctrl_labels[4] = { "M", "0", "1", "2" };
    PlayerControlType current_ctrl = (player == GUNGI_PLAYER_BLACK) ? state->black_control : state->white_control;
    Rectangle *btns = (player == GUNGI_PLAYER_BLACK) ? state->black_ctrl_btns : state->white_ctrl_btns;

    for (int i = 0; i < 4; i++) {
        if (DrawButton(btns[i], ctrl_labels[i], current_ctrl == (PlayerControlType)i, COLOR_ACCENT)) {
            if (player == GUNGI_PLAYER_BLACK) state->black_control = (PlayerControlType)i;
            else state->white_control = (PlayerControlType)i;
        }
    }

    int moves = (player == GUNGI_PLAYER_BLACK) ? state->black_ai_moves : state->white_ai_moves;
    double total_time = (player == GUNGI_PLAYER_BLACK) ? state->black_ai_time : state->white_ai_time;
    double avg_time = (moves > 0) ? (total_time / moves) : 0.0;
    
    DrawText(TextFormat("Avg Think: %.3f s", avg_time), (int)panel.x + 14, (int)panel.y + 520, 18, COLOR_MUTED);
}

static void SetActionMode(AppState *state, GungiActionType action)
{
    state->action = action;
    if (action == GUNGI_ACTION_NEW || state->selection.kind == UI_SOURCE_HAND) {
        ClearSelection(state);
    }
    SetMessage(state, TextFormat("Operation: %s", ActionName(action)));
}

static void DrawHeader(AppState *state)
{
    DrawText("Gungi", 24, 32, 30, COLOR_TEXT);
    DrawText(TextFormat("Turn: %s", PlayerName(state->view.current_player)), 24, 64, 18, COLOR_MUTED);

    const char *labels[] = { "New", "Move", "Capture", "Stack", "Betrayal" };
    for (int i = 0; i < 5; i++) {
        if (i == 4) {
            // 第 5 個按鈕 (Betrayal) 只是純視覺標示，沒有功能，用 COLOR_MUTED 畫成暗色
            DrawButton(state->action_buttons[i], labels[i], false, COLOR_MUTED);
        } else {
            GungiActionType action = (GungiActionType)i;
            if (DrawButton(state->action_buttons[i], labels[i], state->action == action, COLOR_ACCENT)) {
                SetActionMode(state, action);
            }
        }
    }

    if (DrawButton(state->restart_button, "Restart", false, COLOR_ACCENT)) {
        if (state->game != NULL) {
            gungi_reset(state->game);
            ClearSelection(state);

            state->black_ai_time = 0.0;
            state->black_ai_moves = 0;
            state->white_ai_time = 0.0;
            state->white_ai_moves = 0;
            state->history_count = 0;
            state->total_turns = 0;

            SetMessage(state, "Game restarted.");
            RefreshView(state);
        }
    }

    if (DrawButton(state->resign_button, "Resign", false, COLOR_WARN)) {
        if (state->game != NULL && state->has_view) {
            if (gungi_resign(state->game, state->view.current_player)) {
                ClearSelection(state);
                SetMessage(state, TextFormat("%s resigned.", PlayerName(state->view.current_player)));
                RefreshView(state);
            } else {
                const char *error = gungi_last_error(state->game);
                SetMessage(state, (error != NULL && error[0] != '\0') ? error : "Resign failed.");
            }
        }
    }

    Vector2 mouse = GetMousePosition();
    if (CheckCollisionPointRec(mouse, state->toggle_l0_btn) && IsMouseButtonPressed(MOUSE_LEFT_BUTTON)) state->betray_layer0 = !state->betray_layer0;
    if (CheckCollisionPointRec(mouse, state->toggle_l1_btn) && IsMouseButtonPressed(MOUSE_LEFT_BUTTON)) state->betray_layer1 = !state->betray_layer1;
    
    DrawRectangleRec(state->toggle_l0_btn, state->betray_layer0 ? COLOR_ACCENT : COLOR_PANEL);
    DrawRectangleLinesEx(state->toggle_l0_btn, 1.0f, COLOR_GRID);
    DrawRectangle((int)state->toggle_l0_btn.x + 4, (int)state->toggle_l0_btn.y + 4, 10, 10, state->betray_layer0 ? COLOR_TEXT : COLOR_BG);
    DrawText("L1", (int)state->toggle_l0_btn.x + 20, (int)state->toggle_l0_btn.y + 3, 14, COLOR_TEXT);

    DrawRectangleRec(state->toggle_l1_btn, state->betray_layer1 ? COLOR_ACCENT : COLOR_PANEL);
    DrawRectangleLinesEx(state->toggle_l1_btn, 1.0f, COLOR_GRID);
    DrawRectangle((int)state->toggle_l1_btn.x + 4, (int)state->toggle_l1_btn.y + 4, 10, 10, state->betray_layer1 ? COLOR_TEXT : COLOR_BG);
    DrawText("L2", (int)state->toggle_l1_btn.x + 20, (int)state->toggle_l1_btn.y + 3, 14, COLOR_TEXT);
}

static void DrawStatus(const AppState *state)
{
    Rectangle bar = { 24.0f, 712.0f, WINDOW_WIDTH - 48.0f, 54.0f };
    DrawRectangleRounded(bar, 0.05f, 8, COLOR_PANEL);
    DrawRectangleLinesEx(bar, 1.0f, (Color){ 80, 86, 91, 255 });

    const char *message = state->message[0] != '\0' ? state->message : "Select an operation, then select a hand piece or board piece.";
    DrawText(message, (int)(bar.x + 16.0f), (int)(bar.y + 10.0f), 18, COLOR_TEXT);

    char selection[96] = "Selection: none";
    if (state->selection.kind == UI_SOURCE_HAND) {
        snprintf(selection, sizeof(selection), "Selection: %s hand slot %d",
                 PlayerName(state->selection.player), state->selection.hand_index + 1);
    } else if (state->selection.kind == UI_SOURCE_BOARD) {
        snprintf(selection, sizeof(selection), "Selection: board %d,%d level %d | targets: %d",
                 state->selection.x + 1, state->selection.y + 1, state->selection.level + 1, state->target_count);
    }

    char prompt[192];
    snprintf(prompt, sizeof(prompt), "Operation: %s | %s | Keys: N/M/C/S, R restart, Esc/X clear",
             ActionName(state->action), selection);
    DrawText(prompt,
             (int)(bar.x + 16.0f),
             (int)(bar.y + 31.0f),
             16,
             COLOR_MUTED);
    
    if (state->game != NULL && state->has_view) {
        GameState *internal = gungi_game_get_state(state->game);
        if (internal) {
            int eval_score = gungi_evaluate_board(internal);
            char eval_msg[64];
            
            // 正分代表黑方優勢，負分代表白方優勢
            if (eval_score > 0) snprintf(eval_msg, sizeof(eval_msg), "Advantage: Black (+%d)", eval_score);
            else if (eval_score < 0) snprintf(eval_msg, sizeof(eval_msg), "Advantage: White (+%d)", -eval_score);
            else snprintf(eval_msg, sizeof(eval_msg), "Advantage: Even (0)");

            // 畫在狀態列的右邊
            DrawText(eval_msg, (int)(bar.x + bar.width - 260.0f), (int)(bar.y + 18.0f), 18, COLOR_SELECTED);

            DrawText(TextFormat("Current Turn: %d", state->total_turns + 1), (int)(bar.x + bar.width - 450.0f), (int)(bar.y + 18.0f), 18, COLOR_TEXT);
        }
    }
}

static void DrawApp(AppState *state)
{
    BeginDrawing();
    ClearBackground(COLOR_BG);

    DrawHeader(state);
    DrawHandPanel(state, GUNGI_PLAYER_BLACK, state->black_hand_rect);
    DrawBoard(state);
    DrawHandPanel(state, GUNGI_PLAYER_WHITE, state->white_hand_rect);
    DrawStatus(state);

    EndDrawing();
}

static void SaveHistory(AppState *state) {
    if (state->game == NULL) return;
    GameState *internal = gungi_game_get_state(state->game);
    
    if (internal != NULL) {
        if (state->history_count < MAX_HISTORY) {
            // 情況一：空間還夠，直接存在當前的位置
            state->history[state->history_count] = *internal;
            state->history_count++;
        } else {
            // 情況二：歷史紀錄滿了 (達到 256 步)
            // 使用 memmove 將 第 1~255 步 往前搬移到 第 0~254 步的位置 (最舊的第 0 步會被覆蓋丟棄)
            memmove(&state->history[0], &state->history[1], sizeof(GameState) * (MAX_HISTORY - 1));
            
            // 將最新的一步存在陣列的最後一個位子 (索引 255)
            state->history[MAX_HISTORY - 1] = *internal;
        }
    }
}

static void SelectBoardPiece(AppState *state, int x, int y)
{
    const GungiCellView *cell = &state->view.board[y][x];
    int top = cell->height - 1;
    int targets;

    if (top >= GUNGI_MAX_STACK) {
        top = GUNGI_MAX_STACK - 1;
    }

    state->selection.kind = UI_SOURCE_BOARD;
    state->selection.x = x;
    state->selection.y = y;
    state->selection.level = top;
    state->selection.hand_index = -1;
    state->selection.player = cell->stack[top].owner;
    if (state->action == GUNGI_ACTION_NEW) {
        state->action = GUNGI_ACTION_MOVE;
    }

    targets = ComputeBoardTargets(state, x, y);
    if (targets > 0) {
        SetMessage(state, TextFormat("Selected board %d,%d. %d target(s) highlighted.", x + 1, y + 1, targets));
    } else {
        SetMessage(state, TextFormat("Selected board %d,%d. No legal targets.", x + 1, y + 1));
    }
}

static void SubmitRequest(AppState *state, int to_x, int to_y)
{
    if (state->selection.kind == UI_SOURCE_NONE) {
        SetMessage(state, "Select a hand piece or board piece first.");
        return;
    }

    GungiMoveRequest request;
    memset(&request, 0, sizeof(request));
    request.player = state->view.current_player;
    request.from_x = -1;
    request.from_y = -1;
    request.from_level = -1;
    request.to_x = to_x;
    request.to_y = to_y;
    request.hand_index = -1;

    if (state->selection.kind == UI_SOURCE_HAND) {
        request.action = GUNGI_ACTION_NEW;
        request.hand_index = state->selection.hand_index;
    } else {
        UiTargetFlags flags = state->valid_targets[to_y][to_x];
        if (flags == 0) {
            SetMessage(state, TextFormat("Board %d,%d is not a legal target. Pick a highlighted square.", to_x + 1, to_y + 1));
            return;
        }
        request.action = PreferredTargetAction(state, to_x, to_y);
        request.from_x = state->selection.x;
        request.from_y = state->selection.y;
        request.from_level = state->selection.level;
    }

    request.betray_mask = 0;
    if (request.action == GUNGI_ACTION_NEW || request.action == GUNGI_ACTION_STACK) {
        if (state->betray_layer0) request.betray_mask |= 1;
        if (state->betray_layer1) request.betray_mask |= 2;
    }

    // --- 修改：在應用移動前，先備份當前的狀態 ---
    GameState *internal = gungi_game_get_state(state->game);
    GameState backup;
    if (internal) backup = *internal; // 暫存起來

    if (gungi_apply_request(state->game, &request)) {
        // 如果移動被引擎接受了，就把備份正式存入歷史
        if (internal && state->history_count < MAX_HISTORY) {
            state->history[state->history_count++] = backup;
        }
        state->total_turns++;
        SetMessage(state, TextFormat("%s accepted.", ActionName(request.action)));
        ClearSelection(state);
        RefreshView(state);
    } else {
        const char *error = gungi_last_error(state->game);
        SetMessage(state, (error != NULL && error[0] != '\0') ? error : "Move rejected by rules engine.");
    }
}

static void SelectBoardCell(AppState *state, int x, int y)
{
    const GungiCellView *cell = &state->view.board[y][x];

    if (state->selection.kind == UI_SOURCE_BOARD) {
        bool same_cell = state->selection.x == x && state->selection.y == y;
        bool own_top = CellHasCurrentPlayerTop(state, x, y);
        bool explicit_stack = state->action == GUNGI_ACTION_STACK && (state->valid_targets[y][x] & TARGET_STACK) != 0;

        if (same_cell) {
            SelectBoardPiece(state, x, y);
            return;
        }
        if (own_top && !explicit_stack) {
            SelectBoardPiece(state, x, y);
            return;
        }
        if (state->valid_targets[y][x] == 0) {
            SetMessage(state, TextFormat("Board %d,%d is not a legal target. Pick a highlighted square or another own piece.", x + 1, y + 1));
            return;
        }
        SubmitRequest(state, x, y);
        return;
    }

    if (state->selection.kind == UI_SOURCE_HAND) {
        SubmitRequest(state, x, y);
        return;
    }

    if (cell->height <= 0) {
        SetMessage(state, "Select one of your pieces first.");
        return;
    }

    if (!CellHasCurrentPlayerTop(state, x, y)) {
        SetMessage(state, "Select one of your own top pieces.");
        return;
    }

    SelectBoardPiece(state, x, y);
}

static void SelectHandEntry(AppState *state, GungiPlayer player, int hand_index)
{
    if (player != state->view.current_player) {
        SetMessage(state, "Only the current player's hand can be selected.");
        return;
    }

    state->selection.kind = UI_SOURCE_HAND;
    state->selection.x = -1;
    state->selection.y = -1;
    state->selection.level = -1;
    state->selection.hand_index = hand_index;
    state->selection.player = player;
    state->action = GUNGI_ACTION_NEW;
    SetMessage(state, TextFormat("Selected hand slot %d. Choose a board cell.", hand_index + 1));
}

static void HandleKeyboard(AppState *state)
{
    if (IsKeyPressed(KEY_N)) {
        SetActionMode(state, GUNGI_ACTION_NEW);
    } else if (IsKeyPressed(KEY_M)) {
        SetActionMode(state, GUNGI_ACTION_MOVE);
    } else if (IsKeyPressed(KEY_C)) {
        SetActionMode(state, GUNGI_ACTION_CAPTURE);
    } else if (IsKeyPressed(KEY_S)) {
        SetActionMode(state, GUNGI_ACTION_STACK);
    } else if (IsKeyPressed(KEY_R)) {
        if (state->game != NULL) {
            gungi_reset(state->game);
            ClearSelection(state);

            state->black_ai_time = 0.0;
            state->black_ai_moves = 0;
            state->white_ai_time = 0.0;
            state->white_ai_moves = 0;
            state->history_count = 0;
            state->total_turns = 0;

            SetMessage(state, "Game restarted.");
            RefreshView(state);
        }
    } else if (IsKeyPressed(KEY_Z)) {
        // --- 新增：按下 Z 鍵發動悔棋 ---
        if (state->history_count > 0) {
            // 從堆疊中取出上一步的狀態
            state->history_count--;
            state->total_turns--;
            GameState *internal = gungi_game_get_state(state->game);
            
            if (internal != NULL) {
                // 直接將內部狀態覆蓋回上一步
                *internal = state->history[state->history_count];
                ClearSelection(state);
                RefreshView(state);
                SetMessage(state, "Undo: Returned to the previous turn.");
            }
        } else {
            SetMessage(state, "Cannot Undo: No history available.");
        }
    } else if (IsKeyPressed(KEY_ESCAPE) || IsKeyPressed(KEY_X)) {
        ClearSelection(state);
        SetMessage(state, "Selection cleared.");
    }
}

static void HandleMouse(AppState *state)
{
    if (!IsMouseButtonPressed(MOUSE_LEFT_BUTTON) || !state->has_view) {
        return;
    }

    Vector2 mouse = GetMousePosition();
    int hand_index = -1;
    int x = -1;
    int y = -1;

    if (PointToHandEntry(state, mouse, GUNGI_PLAYER_BLACK, &hand_index)) {
        SelectHandEntry(state, GUNGI_PLAYER_BLACK, hand_index);
        return;
    }

    if (PointToHandEntry(state, mouse, GUNGI_PLAYER_WHITE, &hand_index)) {
        SelectHandEntry(state, GUNGI_PLAYER_WHITE, hand_index);
        return;
    }

    if (PointToBoardCell(state, mouse, &x, &y)) {
        SelectBoardCell(state, x, y);
    }
}

int main(void)
{
    srand((unsigned int)time(NULL));
    
    static AppState state;
    memset(&state, 0, sizeof(state));
    state.action = GUNGI_ACTION_MOVE;
    ClearSelection(&state);
    LayoutApp(&state);

    state.game = gungi_create();
    RefreshView(&state);

    InitWindow(WINDOW_WIDTH, WINDOW_HEIGHT, "Gungi raylib");
    SetTargetFPS(60);

    while (!WindowShouldClose()) {
        HandleKeyboard(&state);
        HandleMouse(&state);

        if (state.game != NULL && state.has_view) {
            GameState *internal_state = gungi_game_get_state(state.game);
            
            if (internal_state != NULL && internal_state->status == GUNGI_STATUS_ONGOING) {
                // 判斷當前輪到的玩家，它的控制模式是什麼？
                PlayerControlType current_ctrl = (internal_state->current_player == GUNGI_PLAYER_BLACK) ? state.black_control : state.white_control;
                
                // 如果不是手動模式，代表該 AI 出手了
                if (current_ctrl != CONTROL_MANUAL) {
                    SetMessage(&state, TextFormat("%s AI is thinking...", PlayerName(internal_state->current_player)));
                    // 強制刷新畫面一次，讓玩家看到 Thinking... 與最新的盤面
                    DrawApp(&state);
                    
                    clock_t start_time = clock();
                    Move next_move = gungi_make_resign(internal_state->current_player);
                    if (current_ctrl == CONTROL_RANDOM_AI) {
                        next_move = gungi_get_random_move(internal_state);
                    } else if (current_ctrl == CONTROL_GREEDY_AI) {
                        next_move = gungi_get_ai_move(internal_state, 1);
                    } else if (current_ctrl == CONTROL_MINIMAX_AI) {
                        next_move = gungi_get_ai_move(internal_state, 2);
                    } else {
                        continue;
                    }

                    clock_t end_time = clock();
                    double time_spent = (double)(end_time - start_time) / CLOCKS_PER_SEC;
                    
                    if (internal_state->current_player == GUNGI_PLAYER_BLACK) {
                        state.black_ai_time += time_spent;
                        state.black_ai_moves++;
                    } else {
                        state.white_ai_time += time_spent;
                        state.white_ai_moves++;
                    }


                    // 落子並存入歷史 (悔棋用)
                    SaveHistory(&state);
                    gungi_apply_move(internal_state, next_move);
                    state.total_turns++;
                    ClearSelection(&state);
                    RefreshView(&state);
                }
            }
        }

        DrawApp(&state);
    }

    if (state.game != NULL) {
        gungi_destroy(state.game);
    }

    CloseWindow();
    return 0;
}
