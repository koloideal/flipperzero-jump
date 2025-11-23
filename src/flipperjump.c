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
#define PLATFORM_HEIGHT 4
#define MAX_PLATFORMS 8
#define GRAVITY 0.3f
#define JUMP_VELOCITY -3.0f
#define HORIZONTAL_SPEED 2.0f
#define DEATH_BOUNDARY SCREEN_HEIGHT

typedef struct {
    float x;
    float y;
    float velocity_y;
    bool is_alive;
} Player;

typedef struct {
    float x;
    float y;
    bool active;
} Platform;

typedef struct {
    Player player;
    Platform platforms[MAX_PLATFORMS];
    int score;
    bool is_running;
    bool move_left;
    bool move_right;
    int last_platform_touched;
} GameState;

static void draw_flipper(Canvas* canvas, int x, int y) {
    canvas_draw_str_aligned(canvas, x + PLAYER_WIDTH / 2, y + 3, AlignCenter, AlignCenter, "(-_-)"); 
}

static void input_callback(InputEvent* event, void* context) {
    GameState* game = (GameState*)context;
    
    // Обработка кнопки Back для выхода
    if (event->type == InputTypeShort && event->key == InputKeyBack) {
        game->is_running = false;
        return;
    }
    
    // Обработка нажатия кнопок направления
    if (event->type == InputTypePress || event->type == InputTypeRepeat) {
        if (event->key == InputKeyLeft) {
            game->move_left = true;
        } else if (event->key == InputKeyRight) {
            game->move_right = true;
        }
    } 
    // Обработка отпускания кнопок направления
    else if (event->type == InputTypeRelease) {
        if (event->key == InputKeyLeft) {
            game->move_left = false;
        } else if (event->key == InputKeyRight) {
            game->move_right = false;
        }
    }
}

static void render_callback(Canvas* canvas, void* context) {
    const GameState* game = (const GameState*)context;
    canvas_clear(canvas);

    // Отрисовываем платформы
    for(int i = 0; i < MAX_PLATFORMS; i++) {
        if(game->platforms[i].active) {
            canvas_draw_box(canvas, 
                (int)game->platforms[i].x, 
                (int)game->platforms[i].y, 
                PLATFORM_WIDTH, 
                PLATFORM_HEIGHT);
        }
    }

    // Отрисовываем спрайт игрока на текущих координатах
    draw_flipper(canvas, (int)game->player.x, (int)game->player.y);

    // Отображаем счет в правом верхнем углу
    char score_text[16];
    snprintf(score_text, sizeof(score_text), "Score: %d", game->score);
    canvas_draw_str_aligned(canvas, SCREEN_WIDTH - 2, 0, AlignRight, AlignTop, score_text);
}

static void apply_gravity(Player* player) {
    player->velocity_y += GRAVITY;
}

static bool check_death_boundary(Player* player) {
    return player->y >= DEATH_BOUNDARY;
}

static void init_platforms(GameState* game) {
    // Первая платформа - стартовая, в центре внизу
    game->platforms[0].x = (float)SCREEN_WIDTH / 2 - (float)PLATFORM_WIDTH / 2;
    game->platforms[0].y = SCREEN_HEIGHT - 15;
    game->platforms[0].active = true;
    
    // Генерируем остальные платформы выше
    for(int i = 1; i < MAX_PLATFORMS; i++) {
        game->platforms[i].x = (float)(rand() % (SCREEN_WIDTH - PLATFORM_WIDTH));
        game->platforms[i].y = game->platforms[i-1].y - (20 + rand() % 15);
        game->platforms[i].active = true;
    }
}

static void check_platform_collision(GameState* game) {
    Player* player = &game->player;
    
    // Проверяем коллизию только если игрок падает вниз
    if(player->velocity_y <= 0) {
        return;
    }
    
    for(int i = 0; i < MAX_PLATFORMS; i++) {
        if(!game->platforms[i].active) continue;
        
        Platform* platform = &game->platforms[i];
        
        // Проверяем пересечение по X
        if(player->x + PLAYER_WIDTH > platform->x && 
           player->x < platform->x + PLATFORM_WIDTH) {
            
            // Проверяем пересечение по Y (игрок приземляется сверху)
            if(player->y + PLAYER_HEIGHT >= platform->y && 
               player->y + PLAYER_HEIGHT <= platform->y + PLATFORM_HEIGHT + 5) {
                
                // Прыжок с платформы
                player->velocity_y = JUMP_VELOCITY;
                player->y = platform->y - PLAYER_HEIGHT;
                
                // Увеличиваем счет только если это новая платформа
                if(game->last_platform_touched != i) {
                    game->score++;
                    game->last_platform_touched = i;
                }
                break;
            }
        }
    }
}

static void update_physics(GameState* game) {
    if (!game->is_running) {
        return;  // Не обновляем позицию если игра закончена
    }

    Player* player = &game->player;
    
    // Применяем гравитацию
    apply_gravity(player);
    
    // Обновляем вертикальную позицию
    player->y += player->velocity_y;
    
    // Проверяем коллизию с платформами
    check_platform_collision(game);
    
    // Горизонтальное движение
    if (game->move_left && !game->move_right) {
        player->x -= HORIZONTAL_SPEED;
    } else if (game->move_right && !game->move_left) {
        player->x += HORIZONTAL_SPEED;
    }
    // Если обе кнопки нажаты или обе отпущены - не двигаемся
    
    // Wrap-around на границах экрана
    if (player->x < 0) {
        player->x = SCREEN_WIDTH - PLAYER_WIDTH;
    } else if (player->x > SCREEN_WIDTH - PLAYER_WIDTH) {
        player->x = 0;
    }
    
    // Проверяем death boundary (выход за нижнюю границу экрана)
    if (check_death_boundary(player)) {
        game->is_running = false;
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
    // Инициализация генератора случайных чисел
    srand(furi_get_tick());
    
    // Инициализация игрового состояния
    GameState game = {
        .score = 0,
        .is_running = true,
        .move_left = false,
        .move_right = false,
        .last_platform_touched = -1
    };
    
    // Инициализируем платформы
    init_platforms(&game);
    
    // Размещаем игрока на первой платформе
    game.player.x = game.platforms[0].x + (PLATFORM_WIDTH - PLAYER_WIDTH) / 2;
    game.player.y = game.platforms[0].y - PLAYER_HEIGHT;
    game.player.velocity_y = 0;
    game.player.is_alive = true;
    
    // Открываем GUI
    Gui* gui = furi_record_open("gui");
    if (!gui) {
        return;  // Ошибка инициализации
    }
    
    // Создаем viewport
    ViewPort* viewport = view_port_alloc();
    if (!viewport) {
        furi_record_close("gui");
        return;  // Ошибка инициализации
    }
    
    // Настраиваем callbacks
    view_port_draw_callback_set(viewport, render_callback, &game);
    view_port_input_callback_set(viewport, input_callback, &game);
    
    // Добавляем viewport в GUI
    gui_add_view_port(gui, viewport, GuiLayerFullscreen);
    
    // Главный игровой цикл: update → render → delay
    while (game.is_running) {
        update_physics(&game);
        view_port_update(viewport);
        furi_delay_ms(30);  // ~30 FPS
    }
    
    // Показываем экран game over
    show_game_over(gui, &game);
    
    // Очистка ресурсов в обратном порядке
    gui_remove_view_port(gui, viewport);
    view_port_free(viewport);
    furi_record_close("gui");
}
