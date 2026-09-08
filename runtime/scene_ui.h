/* GPL-2.0-or-later */
#pragma once
#include <SDL.h>
#include <stdbool.h>
bool scene_ui_rect(unsigned value,SDL_Rect *r);
bool scene_ui_update(unsigned page,unsigned selected,unsigned elapsed);
void scene_ui_draw(SDL_Surface *canvas,unsigned page,unsigned selected);
void scene_ui_close(void);
