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
#define PLATFORM_HEIGHT 3
#define PLATFORM_COUNT 4
#define GRAVITY 0.15f
#define JUMP_VELOCITY -3.0f
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
} GameState;

// Простой "спрайт" флиппера (можно заменить на пиксель-арт позже)
static void draw_flipper(Canvas* canvas, int x, int y) {
    canvas_draw_str_aligned(canvas, x + PLAYER_WIDTH/2, y + 3, AlignCenter, AlignCenter, "><((°>");
}

static void input_callback(InputEvent* event, void* context) {
    GameState* game = context;
    if(event->type == InputTypeShort) {
        if(event->key == InputKeyBack) game->running = false;
    } else if(event->type == InputTypePress || event->type == InputTypeRepeat) {
        if(event->key == InputKeyLeft) game->move_left = true;
        if(event->key == InputKeyRight) game->move_right = true;
    } else if(event->type == InputTypeRelease) {
        if(event->key == InputKeyLeft) game->move_left = false;
        if(event->key == InputKeyRight) game->move_right = false;
    }
}

static void render_callback(Canvas* canvas, void* context) {
    GameState* game = context;

    canvas_clear(canvas);

    // Отрисовка платформ
    for(int i = 0; i < PLATFORM_COUNT; i++) {
        int py = (int)(game->platforms[i].y + game->scroll_offset);
        if(py > -PLATFORM_HEIGHT && py < SCREEN_HEIGHT) {
            canvas_draw_box(canvas, (int)game->platforms[i].x, py, PLATFORM_WIDTH, PLATFORM_HEIGHT);
        }
    }

    // Отрисовка игрока
    int draw_y = (int)(game->player.y + game->scroll_offset);
    draw_flipper(canvas, (int)game->player.x, draw_y);
}

void init_platforms(GameState* game) {
    // Первая платформа прямо под игроком
    game->platforms[0].x = SCREEN_WIDTH / 2 - PLATFORM_WIDTH / 2;
    game->platforms[0].y = SCREEN_HEIGHT - 10;

    // Остальные случайно, но их меньше
    for(int i = 1; i < PLATFORM_COUNT; i++) {
        game->platforms[i].x = rand() % (SCREEN_WIDTH - PLATFORM_WIDTH);
        game->platforms[i].y = SCREEN_HEIGHT - i * 30; // реже, больше расстояние
    }
}


void update_game(GameState* game) {
    Player* player = &game->player;

    // Горизонтальное движение
    if(game->move_left) player->x -= PLAYER_SPEED;
    if(game->move_right) player->x += PLAYER_SPEED;

    // Обернуть экран по горизонтали
    if(player->x < 0) player->x = SCREEN_WIDTH - PLAYER_WIDTH;
    if(player->x > SCREEN_WIDTH - PLAYER_WIDTH) player->x = 0;

    // Гравитация
    player->vy += GRAVITY;
    player->y += player->vy;

    // Если игрок выше середины — начинаем скролл
    if(player->y < SCREEN_HEIGHT / 2) {
        float dy = (SCREEN_HEIGHT / 2 - player->y);
        player->y = SCREEN_HEIGHT / 2;
        game->scroll_offset += dy;

        // Опускаем платформы
        for(int i = 0; i < PLATFORM_COUNT; i++) {
            game->platforms[i].y += dy;

            // Перегенерируем платформу, если она вышла за низ
            if(game->platforms[i].y > SCREEN_HEIGHT) {
                game->platforms[i].y = 0;
                game->platforms[i].x = rand() % (SCREEN_WIDTH - PLATFORM_WIDTH);
                game->score++;
            }
        }
    }

    // Проверка на столкновения с платформами (только если падаем)
    if(player->vy > 0) {
        for(int i = 0; i < PLATFORM_COUNT; i++) {
            Platform* p = &game->platforms[i];
            if(
                player->x + PLAYER_WIDTH > p->x &&
                player->x < p->x + PLATFORM_WIDTH &&
                player->y + PLAYER_HEIGHT >= p->y &&
                player->y + PLAYER_HEIGHT <= p->y + PLATFORM_HEIGHT
            ) {
                player->vy = JUMP_VELOCITY;
            }
        }
    }

    // Если упал ниже экрана
    if(player->y > SCREEN_HEIGHT) {
        game->running = false;
    }
}

void flipperjump_start(void) {
    srand(furi_get_tick());

    GameState game = {
        .player = {.x = SCREEN_WIDTH/2, .y = SCREEN_HEIGHT - 20, .vy = 0, .alive = true},
        .scroll_offset = 0,
        .running = true,
        .score = 0,
    };

    init_platforms(&game);

    Gui* gui = furi_record_open("gui");
    ViewPort* viewport = view_port_alloc();
    view_port_draw_callback_set(viewport, render_callback, &game);
    view_port_input_callback_set(viewport, input_callback, &game);
    gui_add_view_port(gui, viewport, GuiLayerFullscreen);

    while(game.running) {
        update_game(&game);
        view_port_update(viewport);
        furi_delay_ms(30);
    }

    // Очистка
    gui_remove_view_port(gui, viewport);
    view_port_free(viewport);
    furi_record_close("gui");
}
