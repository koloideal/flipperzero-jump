#include <furi.h>
#include <gui/gui.h>
#include <gui/view_dispatcher.h>
#include "flipperjump.h"

int32_t flipperjump_app(void* p) {
    UNUSED(p);
    flipperjump_start();  // основной игровой цикл
    return 0;
}
