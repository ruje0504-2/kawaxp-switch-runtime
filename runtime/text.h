/* GPL-2.0-or-later. Text rendering abstraction: SDL_ttf on host, raw
 * FreeType2 on Switch (SDL2_ttf is not packaged for devkitA64).
 * Fonts render UTF-8 (libai5 converts script SJIS text to UTF-8 at parse). */
#ifndef KAWA_TEXT_H
#define KAWA_TEXT_H
#include <SDL.h>
struct kawa_font;
int kawa_text_init(void);
void kawa_text_fini(void);
struct kawa_font *kawa_font_open(const char *path, int px);
void kawa_font_close(struct kawa_font *f);
/* Render UTF-8 text wrapped at <width> px into a new 32-bit RGBA surface. */
SDL_Surface *kawa_text_render(struct kawa_font *f, const char *text,
                              int width, Uint32 color);
#endif
