/* GPL-2.0-or-later. Host text backend: SDL_ttf. */
#include "text.h"
#include <SDL_ttf.h>
struct kawa_font { TTF_Font *f; };
int kawa_text_init(void){return TTF_Init()<0?-1:0;}
void kawa_text_fini(void){TTF_Quit();}
struct kawa_font *kawa_font_open(const char *path, int px) {
	struct kawa_font *k = calloc(1, sizeof(*k));
	if (!k) return NULL;
	k->f = TTF_OpenFont(path, px);
	if (!k->f) { free(k); return NULL; }
	return k;
}
void kawa_font_close(struct kawa_font *k) { if (k) { if (k->f) TTF_CloseFont(k->f); free(k); } }
SDL_Surface *kawa_text_render(struct kawa_font *k, const char *text, int width, Uint32 color) {
	if (!k || !k->f || !text || !text[0]) return NULL;
	SDL_Color c = {(color >> 16) & 255, (color >> 8) & 255, color & 255, 255};
	return TTF_RenderUTF8_Blended_Wrapped(k->f, text, c, width);
}
