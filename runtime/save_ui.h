/* GPL-2.0-or-later */
#pragma once
#include <SDL.h>
#include <stdbool.h>
#define SLOT_PAGE 1200u
#define SLOT_YES 1210u
#define SLOT_NO 1211u
#define SLOT_MEMO 1212u
extern unsigned slot_page;
extern int slot_pending;
bool save_ui_kind(unsigned kind);
bool save_ui_enabled(unsigned kind,unsigned value);
bool save_ui_rect(unsigned kind,unsigned value,SDL_Rect *r);
void save_ui_draw(SDL_Surface *canvas,unsigned kind,unsigned selected);
void save_ui_scan(void);
void save_ui_prepare(unsigned slot);
void save_ui_edit(void);
bool save_ui_event(SDL_Event *e);
void save_ui_write_memo(unsigned slot);
void save_ui_close(void);
