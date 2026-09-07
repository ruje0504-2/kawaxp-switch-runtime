/* GPL-2.0-or-later */
#pragma once
#include <SDL.h>
#include <stdbool.h>
#define EXTRA_PREV 1000u
#define EXTRA_NEXT 1001u
#define EXTRA_PLAY 1002u
extern const char *const music_files[14];
extern int music_playing;
unsigned album_flag(unsigned id);
bool extras_enabled(unsigned kind,unsigned value);
bool extras_rect(unsigned kind,unsigned value,SDL_Rect *r);
void extras_draw(SDL_Surface *canvas,unsigned kind,unsigned selected_value);
void extras_close(void);
