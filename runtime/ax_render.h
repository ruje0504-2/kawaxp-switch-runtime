/* GPL-2.0-or-later */
#pragma once
#include <SDL.h>
/* PC 41b160: green color key, per-term /255 truncation, opaque result. */
void ax_blit(SDL_Surface *src,SDL_Surface *dst,int sx,int sy,int w,int h,int dx,int dy);
