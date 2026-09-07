/* GPL-2.0-or-later. Switch text backend: raw FreeType2.
 * Renders wrapped UTF-8 into a 32-bit RGBA surface (simple per-char wrap,
 * no kerning; adequate dev quality). Replaces SDL_ttf for devkitA64.
 *
 * Glyph cache: FreeType rasterisation (FT_LOAD_RENDER) is by far the most
 * expensive text operation on the Switch A57. Every dialogue line used to
 * re-rasterise every glyph twice (shadow pass + colour pass) per line change,
 * which measured 100-300ms single-frame hitches on device. Glyph bitmaps are
 * now cached per codepoint (8-bit coverage, colour applied at blend time) so a
 * new line is mostly cache hits; only glyphs never seen before are rasterised. */
#include "text.h"
#include <ft2build.h>
#include FT_FREETYPE_H
#include <stdlib.h>
#include <string.h>
struct kawa_font { FT_Face face; FT_Library lib; int px; };
static FT_Library g_lib;
static int g_lib_ok;

#define GLY_CACHE_SLOTS 16384 /* open addressing; Japanese text set is a few thousand */
struct gly_ent { uint32_t cp; int left, top, adv; uint16_t w, h; unsigned char *bmp; uint8_t metric, ready; };
static struct gly_ent *gly_cache;
static void gly_cache_clear(void) {
	if (!gly_cache) return;
	for (unsigned i = 0; i < GLY_CACHE_SLOTS; i++) { free(gly_cache[i].bmp); gly_cache[i].cp = 0; gly_cache[i].metric = 0; gly_cache[i].ready = 0; }
}
int kawa_text_init(void){return 0;}
void kawa_text_fini(void){
	if (gly_cache) { gly_cache_clear(); free(gly_cache); gly_cache = NULL; }
	if (g_lib_ok) { FT_Done_FreeType(g_lib); g_lib_ok = 0; }
}

static struct gly_ent *gly_find_slot(uint32_t cp) {
	if (!gly_cache) gly_cache = calloc(GLY_CACHE_SLOTS, sizeof(struct gly_ent));
	if (!gly_cache) return NULL;
	unsigned h = (unsigned)(cp * 2654435761u);
	for (unsigned i = 0; i < GLY_CACHE_SLOTS; i++) {
		struct gly_ent *e = &gly_cache[(h + i) & (GLY_CACHE_SLOTS - 1)];
		if (!e->cp) return e;
		if (e->cp == cp) return e;
	}
	/* table full: drop everything, start over */
	gly_cache_clear();
	return gly_cache;
}
static void blend_px(unsigned char *px, int stride, int x, int y, int w, int h,
                     const unsigned char *src, int srcpitch, Uint32 color, int xo, int yo) {
	unsigned cr = color & 255, cg = (color >> 8) & 255, cb = (color >> 16) & 255;
	for (int j = 0; j < h; j++) {
		const unsigned char *sr = src + (size_t)j * srcpitch;
		for (int i = 0; i < w; i++) {
			unsigned a = sr[i];
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
/* cached advance for layout (no rasterisation) */
static unsigned glyph_advance(struct kawa_font *k, unsigned cp) {
	struct gly_ent *e = gly_find_slot(cp);
	if (!e) return 0;
	if (!e->metric) {
		if (!e->cp) e->cp = cp;
		FT_UInt gi = FT_Get_Char_Index(k->face, cp);
		e->metric = 1;
		if (gi && !FT_Load_Glyph(k->face, gi, FT_LOAD_DEFAULT))
			e->adv = (int)(k->face->glyph->advance.x >> 6);
	}
	return e->adv > 0 ? (unsigned)e->adv : 0;
}
/* rasterise once per glyph and blend it; returns the advance consumed */
static int glyph_draw(struct kawa_font *k, unsigned cp, int x, int y,
                      unsigned char *px, int stride, Uint32 color) {
	struct gly_ent *e = gly_find_slot(cp);
	if (!e) return 0;
	if (!e->metric) {
		if (!e->cp) e->cp = cp;
		FT_UInt gi = FT_Get_Char_Index(k->face, cp);
		e->metric = 1;
		if (gi && !FT_Load_Glyph(k->face, gi, FT_LOAD_DEFAULT))
			e->adv = (int)(k->face->glyph->advance.x >> 6);
	}
	if (!e->ready) {
		FT_UInt gi = FT_Get_Char_Index(k->face, cp);
		if (!gi) { e->ready = 1; return e->adv; }
		if (FT_Load_Glyph(k->face, gi, FT_LOAD_RENDER)) { e->ready = 1; return e->adv; }
		FT_GlyphSlot sl = k->face->glyph;
		e->left = sl->bitmap_left; e->top = sl->bitmap_top;
		e->w = (uint16_t)sl->bitmap.width; e->h = (uint16_t)sl->bitmap.rows;
		if (e->w && e->h && sl->bitmap.buffer) {
			e->bmp = malloc((size_t)e->h * e->w);
			if (e->bmp) {
				for (int j = 0; j < e->h; j++)
					memcpy(e->bmp + (size_t)j * e->w,
					       sl->bitmap.buffer + (size_t)j * sl->bitmap.pitch, e->w);
			}
		}
		e->ready = 1;
	}
	if (e->bmp && e->w && e->h)
		blend_px(px, stride, x, y, e->w, e->h, e->bmp, e->w, color,
		         e->left, k->px - e->top);
	return e->adv;
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
			if (cp == ' ' || cp == 0x3000) { x += (int)glyph_advance(k, cp); continue; }
			x += glyph_draw(k, cp, x, y, px, s->pitch, color);
		}
	}
	return s;
}
