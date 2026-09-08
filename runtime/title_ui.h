/* GPL-2.0-or-later */
#pragma once
#include <SDL.h>
#include <stdbool.h>
void title_ui_begin(SDL_Surface *parts, SDL_Surface *background, const Uint8 *mask, bool intro);
bool title_ui_update(unsigned elapsed_ms);
bool title_ui_active(void);
void title_ui_skip(void);
void title_ui_close(void);
/* AI.exe 44b3a0 / 447430: START.MES result IDs, not display indices. */
bool title_ui_button(unsigned result, unsigned row, bool selected,
                     SDL_Rect *source, SDL_Rect *destination);
void title_ui_draw(SDL_Surface *parts, SDL_Surface *canvas,
                   const unsigned *results, unsigned count, unsigned selected);
