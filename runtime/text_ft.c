/* GPL-2.0-or-later. Switch text backend: raw FreeType2.
 * Renders wrapped UTF-8 into a 32-bit RGBA surface (simple per-char wrap,
 * no kerning; adequate dev quality). Replaces SDL_ttf for devkitA64. */
#include "text.h"
#include <ft2build.h>
#include FT_FREETYPE_H
#include <stdlib.h>
#include <string.h>
struct kawa_font { FT_Face face; FT_Library lib; int px; };
static FT_Library g_lib;
static int g_lib_ok;
int kawa_text_init(void){return 0;}
void kawa_text_fini(void){if(g_lib_ok){FT_Done_FreeType(g_lib);g_lib_ok=0;}}
static void blend_px(unsigned char *px, int stride, int x, int y, int w, int h,
                     const unsigned char *src, Uint32 color, int xo, int yo) {
	unsigned cr = color & 255, cg = (color >> 8) & 255, cb = (color >> 16) & 255;
	for (int j = 0; j < h; j++) {
		for (int i = 0; i < w; i++) {
			unsigned a = src[(size_t)j * w + i];
			if (!a) continue;
			int dx = x + xo + i, dy = y + yo + j;
			if (dx < 0 || dy < 0) continue;
			unsigned char *p = px + (size_t)dy * stride + (size_t)dx * 4;
			unsigned ia = 255 - a;
			p[0] = (unsigned char)((cr * a + p[0] * ia) / 255);
			p[1] = (unsigned char)((cg * a + p[1] * ia) / 255);
			p[2] = (unsigned char)((cb * a + p[2] * ia) / 255);
			p[3] = 255;
		}
	}
}
struct kawa_font *kawa_font_open(const char *path, int px) {
	if (!g_lib_ok) { if (FT_Init_FreeType(&g_lib)) return NULL; g_lib_ok = 1; }
	struct kawa_font *k = calloc(1, sizeof(*k));
	if (!k) return NULL;
	k->px = px; k->lib = g_lib;
	if (FT_New_Face(g_lib, path, 0, &k->face)) { free(k); return NULL; }
	FT_Set_Pixel_Sizes(k->face, 0, px);
	return k;
}
void kawa_font_close(struct kawa_font *k) { if (k) { FT_Done_Face(k->face); free(k); } }
static unsigned next_cp(const char **sp) {
	const unsigned char *s = (const unsigned char *)*sp;
	unsigned c = s[0]; int n = 1;
	if (c >= 0xf0 && (s[1] & 0xc0) == 0x80) { c = ((c & 7) << 18) | ((s[1] & 63) << 12) | ((s[2] & 63) << 6) | (s[3] & 63); n = 4; }
	else if (c >= 0xe0 && (s[1] & 0xc0) == 0x80) { c = ((c & 15) << 12) | ((s[1] & 63) << 6) | (s[2] & 63); n = 3; }
	else if (c >= 0xc0 && (s[1] & 0xc0) == 0x80) { c = ((c & 31) << 6) | (s[1] & 63); n = 2; }
	*sp += n;
	return c;
}
static unsigned glyph_advance(struct kawa_font *k, unsigned cp) {
	FT_UInt gi = FT_Get_Char_Index(k->face, cp);
	if (!gi) return 0;
	if (FT_Load_Glyph(k->face, gi, FT_LOAD_DEFAULT)) return 0;
	return (unsigned)(k->face->glyph->advance.x >> 6);
}
SDL_Surface *kawa_text_render(struct kawa_font *k, const char *text, int width, Uint32 color) {
	if (!k || !text || !text[0]) return NULL;
	/* layout lines: greedy per-char wrap */
	struct { const char *s, *e; } ln[512];
	unsigned nlines = 0, maxw = 0;
	const char *p = text, *ls = text;
	unsigned curw = 0;
	for (;;) {
		if (!*p || *p == '\n') {
			if (nlines < 512) { ln[nlines].s = ls; ln[nlines].e = p; }
			nlines++;
			if (curw > maxw) maxw = curw;
			if (!*p) break;
			p++; ls = p; curw = 0; continue;
		}
		const char *c0 = p;
		unsigned cp = next_cp(&p);
		unsigned a = glyph_advance(k, cp);
		if (curw + a > (unsigned)width && curw > 0) {
			if (nlines < 512) { ln[nlines].s = ls; ln[nlines].e = c0; }
			nlines++;
			if (curw > maxw) maxw = curw;
			ls = c0; curw = a;
		} else curw += a;
	}
	int W = maxw + k->px, H = (int)nlines * (k->px + k->px / 3) + k->px / 2;
	if (W < 4) W = 4; if (H < 4) H = 4;
	SDL_Surface *s = SDL_CreateRGBSurfaceWithFormat(0, W, H, 32, SDL_PIXELFORMAT_RGBA32);
	if (!s) return NULL;
	SDL_FillRect(s, NULL, 0);
	unsigned char *px = s->pixels;
	int lh = k->px + k->px / 3;
	for (unsigned li = 0; li < nlines && li < 512; li++) {
		const char *q = ln[li].s;
		int x = 0, y = (int)li * lh;
		while (q < ln[li].e) {
			unsigned cp = next_cp(&q);
			if (cp == ' ' || cp == '　') { x += (int)glyph_advance(k, cp); continue; }
			FT_UInt gi = FT_Get_Char_Index(k->face, cp);
			if (!gi) continue;
			if (FT_Load_Glyph(k->face, gi, FT_LOAD_RENDER)) continue;
			FT_Bitmap *bm = &k->face->glyph->bitmap;
			blend_px(px, s->pitch, x, y, bm->width, bm->rows, bm->buffer, color,
			         k->face->glyph->bitmap_left, k->px - k->face->glyph->bitmap_top);
			x += (int)(k->face->glyph->advance.x >> 6);
		}
	}
	return s;
}
