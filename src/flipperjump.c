#include "flipperjump.h"
#include <gui/gui.h>
#include <gui/view_port.h>
#include <input/input.h>
#include <furi.h>
#include <stdlib.h>
#include <math.h>
#include <furi_hal.h>

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define PLAYER_WIDTH 8
#define PLAYER_HEIGHT 8
#define PLATFORM_WIDTH 24
#define PLATFORM_HEIGHT 4
#define MAX_PLATFORMS 2
#define PLATFORM_DISTANCE 30.0f
#define SCROLL_SPEED 1.5f
#define START_PLATFORM_Y (SCREEN_HEIGHT - 15)
#define GRAVITY 0.2f
#define JUMP_VELOCITY -4.0f
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
    int current_platform;  // Индекс текущей платформы (0 или 1)
    float scroll_offset;   // Текущее смещение для плавного скроллинга
    float target_scroll;   // Целевое смещение
    bool scrolling;        // Идет ли скроллинг
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

    // Отрисовываем платформы с учетом скроллинга
    for(int i = 0; i < MAX_PLATFORMS; i++) {
        if(game->platforms[i].active) {
            int draw_y = (int)(game->platforms[i].y + game->scroll_offset);
            canvas_draw_box(canvas, 
                (int)game->platforms[i].x, 
                draw_y, 
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
    // Платформа 0 - нижняя (стартовая)
    game->platforms[0].x = (float)SCREEN_WIDTH / 2 - (float)PLATFORM_WIDTH / 2;
    game->platforms[0].y = START_PLATFORM_Y;
    game->platforms[0].active = true;
    
    // Платформа 1 - верхняя (следующая)
    game->platforms[1].x = (float)(rand() % (SCREEN_WIDTH - PLATFORM_WIDTH));
    game->platforms[1].y = START_PLATFORM_Y - PLATFORM_DISTANCE;
    game->platforms[1].active = true;
}

static void update_scroll(GameState* game) {
    if(!game->scrolling) return;
    
    // Проверяем, достигли ли цели
    float diff = game->target_scroll - game->scroll_offset;
    if(fabsf(diff) < 0.5f) {
        // Достигли цели - завершаем скроллинг
        game->scroll_offset = game->target_scroll;
        game->scrolling = false;
        
        // Применяем финальное смещение к платформам
        for(int i = 0; i < MAX_PLATFORMS; i++) {
            game->platforms[i].y += game->scroll_offset;
        }
        game->player.y += game->scroll_offset;
        game->scroll_offset = 0;
        game->target_scroll = 0;
        
        // Переключаем текущую платформу
        game->current_platform = 1 - game->current_platform;  // 0->1 или 1->0
    }
    // Скроллинг происходит автоматически через движение персонажа
}

static void check_platform_collision(GameState* game) {
    Player* player = &game->player;
    
    // Проверяем коллизию только если игрок падает вниз
    if(player->velocity_y <= 0) {
        return;
    }
    
    // Проверяем обе платформы
    for(int i = 0; i < MAX_PLATFORMS; i++) {
        if(!game->platforms[i].active) continue;
        
        Platform* platform = &game->platforms[i];
        float platform_y = platform->y + game->scroll_offset;
        
        // Проверяем пересечение по X
        if(player->x + PLAYER_WIDTH > platform->x && 
           player->x < platform->x + PLATFORM_WIDTH) {
            
            // Проверяем пересечение по Y (игрок приземляется сверху)
            if(player->y + PLAYER_HEIGHT >= platform_y && 
               player->y + PLAYER_HEIGHT <= platform_y + PLATFORM_HEIGHT + 5) {
                
                // Прыжок с платформы
                player->velocity_y = JUMP_VELOCITY;
                
                // Если это верхняя платформа и мы еще не скроллим
                if(i != game->current_platform && !game->scrolling) {
                    game->score++;
                    game->scrolling = true;
                    game->target_scroll = PLATFORM_DISTANCE;
                    
                    // Сразу генерируем новую платформу на старом месте
                    int old_platform = game->current_platform;
                    int new_platform = i;
                    
                    // Старая платформа становится новой верхней (до скроллинга)
                    game->platforms[old_platform].x = (float)(rand() % (SCREEN_WIDTH - PLATFORM_WIDTH));
                    game->platforms[old_platform].y = game->platforms[new_platform].y - PLATFORM_DISTANCE;
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
    float old_y = player->y;
    player->y += player->velocity_y;
    
    // Если идет скроллинг, двигаем платформы синхронно с персонажем
    if(game->scrolling && player->velocity_y < 0) {
        // Персонаж движется вверх - скроллим платформы вниз с той же скоростью
        float scroll_delta = player->y - old_y;  // Отрицательное значение
        game->scroll_offset -= scroll_delta;  // Инвертируем для движения вниз
        
        // Проверяем, достигли ли цели
        if(game->scroll_offset >= game->target_scroll) {
            game->scroll_offset = game->target_scroll;
        }
    }
    
    // Обновляем скроллинг (проверка завершения)
    update_scroll(game);
    
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
        .current_platform = 0,
        .scroll_offset = 0,
        .target_scroll = 0,
        .scrolling = false
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
