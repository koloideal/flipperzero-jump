#include "flipperjump.h"
#include <gui/gui.h>
#include <gui/view_port.h>
#include <input/input.h>
#include <furi.h>
#include <stdlib.h>
#include <furi_hal.h>

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define PLAYER_WIDTH 8
#define PLAYER_HEIGHT 8
#define PLATFORM_WIDTH 24
#define PLATFORM_HEIGHT 5
#define PLATFORM_COUNT 4
#define GRAVITY 0.15f
#define JUMP_VELOCITY -3.5f
#define PLAYER_SPEED 2.0f

typedef struct {
    float x, y;
    float vy;
    bool alive;
} Player;

typedef struct {
    float x, y;
} Platform;

typedef struct {
    Player player;
    Platform platforms[PLATFORM_COUNT];
    float scroll_offset;
    int score;
    bool running;
    bool move_left;
    bool move_right;
    float target_scroll_offset;
    int current_platform;
    int last_platform_id;
} GameState;

static void draw_flipper(Canvas* canvas, int x, int y) {
    canvas_draw_str_aligned(canvas, x + PLAYER_WIDTH / 2, y + 3, AlignCenter, AlignCenter, "(-_-)"); 
}

static void input_callback(InputEvent* event, void* context) {
    GameState* game = context;
    if (event->type == InputTypeShort) {
        if (event->key == InputKeyBack) game->running = false;
    } else if (event->type == InputTypePress || event->type == InputTypeRepeat) {
        if (event->key == InputKeyLeft) game->move_left = true;
        if (event->key == InputKeyRight) game->move_right = true;
    } else if (event->type == InputTypeRelease) {
        if (event->key == InputKeyLeft) game->move_left = false;
        if (event->key == InputKeyRight) game->move_right = false;
    }
}

static void render_callback(Canvas* canvas, void* context) {
    GameState* game = context;
    canvas_clear(canvas);

    for (int i = 0; i < PLATFORM_COUNT; i++) {
        int py = (int)(game->platforms[i].y + game->scroll_offset);
        if (py > -PLATFORM_HEIGHT && py < SCREEN_HEIGHT) {
            canvas_draw_box(canvas, (int)game->platforms[i].x, py, PLATFORM_WIDTH, PLATFORM_HEIGHT);
        }
    }

    int draw_y = (int)(game->player.y + game->scroll_offset);
    draw_flipper(canvas, (int)game->player.x, draw_y);

    char score_text[16];
    snprintf(score_text, sizeof(score_text), "Score: %d", game->score);
    canvas_draw_str_aligned(canvas, SCREEN_WIDTH - 2, 0, AlignRight, AlignTop, score_text);
}

void spawn_new_platform(GameState* game) {
    int next = game->current_platform % PLATFORM_COUNT;
    game->platforms[next].x = rand() % (SCREEN_WIDTH - PLATFORM_WIDTH);
    game->platforms[next].y = game->platforms[(next + PLATFORM_COUNT - 1) % PLATFORM_COUNT].y - (rand() % 20 + 20);
    game->current_platform++;
}

void init_platforms(GameState* game) {
    game->platforms[0].x = (float)SCREEN_WIDTH / 2 - (float)PLATFORM_WIDTH / 2;
    game->platforms[0].y = SCREEN_HEIGHT - 10;
    for (int i = 1; i < PLATFORM_COUNT; i++) {
        game->platforms[i].x = rand() % (SCREEN_WIDTH - PLATFORM_WIDTH);
        game->platforms[i].y = game->platforms[i - 1].y - 30;
    }
}

void update_game(GameState* game) {
    Player* player = &game->player;
    if (game->move_left) player->x -= PLAYER_SPEED;
    if (game->move_right) player->x += PLAYER_SPEED;
    if (player->x < 0) player->x = SCREEN_WIDTH - PLAYER_WIDTH;
    if (player->x > SCREEN_WIDTH - PLAYER_WIDTH) player->x = 0;
    player->vy += GRAVITY;
    player->y += player->vy;
    if (player->vy > 0) {
        for (int i = 0; i < PLATFORM_COUNT; i++) {
            Platform* p = &game->platforms[i];
            if (player->x + PLAYER_WIDTH > p->x &&
                player->x < p->x + PLATFORM_WIDTH &&
                player->y + PLAYER_HEIGHT >= p->y &&
                player->y + PLAYER_HEIGHT <= p->y + PLATFORM_HEIGHT) {
                if (game->last_platform_id != i) {
                    player->vy = JUMP_VELOCITY;
                    game->target_scroll_offset += 20.0f;
                    spawn_new_platform(game);
                    game->last_platform_id = i;
                    game->score++;
                } else {
                    player->vy = JUMP_VELOCITY;
                }
                break;
            }
        }
    }
    if (game->scroll_offset < game->target_scroll_offset) {
        float delta = game->target_scroll_offset - game->scroll_offset;
        float step = delta * 0.2f;
        if (step < 0.5f) step = 0.5f;
        game->scroll_offset += step;
    }
    if (player->y > SCREEN_HEIGHT) {
        game->running = false;
    }
}

typedef struct {
    bool waiting;
    int score;
} GameOverState;

static void game_over_render_callback(Canvas* canvas, void* context) {
    GameOverState* state = context;
    canvas_clear(canvas);
    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str_aligned(canvas, SCREEN_WIDTH / 2, 10, AlignCenter, AlignTop, "Game Over");

    char score_text[32];
    snprintf(score_text, sizeof(score_text), "Score: %d", state->score);
    canvas_set_font(canvas, FontSecondary);
    canvas_draw_str_aligned(canvas, SCREEN_WIDTH / 2, 30, AlignCenter, AlignTop, score_text);

    canvas_set_font(canvas, FontSecondary);
    canvas_draw_str_aligned(canvas, SCREEN_WIDTH / 2, 50, AlignCenter, AlignTop, "Press BACK");
}

static void game_over_input_callback(InputEvent* event, void* context) {
    GameOverState* state = context;
    if (event->type == InputTypeShort && event->key == InputKeyBack) {
        state->waiting = false;
    }
}

void show_game_over(Gui* gui, GameState* game) {
    ViewPort* viewport = view_port_alloc();
    GameOverState state = {
        .waiting = true,
        .score = game->score,
    };

    view_port_draw_callback_set(viewport, game_over_render_callback, &state);
    view_port_input_callback_set(viewport, game_over_input_callback, &state);

    gui_add_view_port(gui, viewport, GuiLayerFullscreen);

    while (state.waiting) {
        view_port_update(viewport);
        furi_delay_ms(30);
    }

    gui_remove_view_port(gui, viewport);
    view_port_free(viewport);
}

void flipperjump_start(void) {
    srand(furi_get_tick());
    GameState game = {
        .player = {.x = (float)SCREEN_WIDTH / 2, .y = SCREEN_HEIGHT - 20, .vy = 0, .alive = true},
        .scroll_offset = 0,
        .running = true,
        .score = 0,
        .target_scroll_offset = 0,
        .current_platform = 1,
        .last_platform_id = 0,
    };
    init_platforms(&game);
    Gui* gui = furi_record_open("gui");
    ViewPort* viewport = view_port_alloc();
    view_port_draw_callback_set(viewport, render_callback, &game);
    view_port_input_callback_set(viewport, input_callback, &game);
    gui_add_view_port(gui, viewport, GuiLayerFullscreen);
    while (game.running) {
        update_game(&game);
        view_port_update(viewport);
        furi_delay_ms(30);
    }
    show_game_over(gui, &game);
    gui_remove_view_port(gui, viewport);
    view_port_free(viewport);
    furi_record_close("gui");
}
