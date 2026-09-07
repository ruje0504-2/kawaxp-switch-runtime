/* GPL-2.0-or-later */
#pragma once
#include <SDL.h>
#include "text.h"
#include "ax.h"
#include "ai5/arc.h"
#include "ai5/mes.h"
#define NSURF 16
#define NPROC 256
#define NFRAME 128
#define NFLAG 5000
#define NVAR 500
#define NSYS 256
#define NTEXT 8192
struct loc { char script[32]; uint32_t addr; };
struct state {
 uint32_t sys[NSYS], var[32]; uint16_t word[NVAR]; uint8_t flag[NFLAG];
 struct loc ip, proc[NPROC], frames[NFRAME]; uint32_t depth;
 char text[NTEXT]; uint32_t color, message_id, waiting, messages;
};
extern struct state st;
extern struct ax_player animation;
void animation_load(const char *name);
void animation_update(unsigned elapsed_ms);
extern SDL_Surface *surfaces[NSURF];
extern SDL_Window *window;
extern SDL_Renderer *renderer;
extern struct kawa_font *font;
extern struct archive *mes_arc,*cg_arc,*seq_arc;
extern bool running, smoke;
extern char error_text[512];
extern char data_dir[1024],save_dir[1024];
extern unsigned unsupported;
void fail(const char *fmt,...);
void note(const char *fmt,...);
void vm_start(const char *script);
void vm_step(void);
void vm_choose(unsigned i);
void vm_advance(void);
extern unsigned vm_choice_kind(void);
extern unsigned vm_last_menu_addr(void);
extern void vm_slot_menu(bool save); /* engine save/load slot menu (keys/smoke) */
uint32_t ev(struct mes_expression *e);
void draw_image(const char *name,unsigned dst,int x,int y);
void copy_rect(int sx,int sy,int ex,int ey,unsigned src,int dx,int dy,unsigned dst,bool mask);
void fill_rect(int x,int y,int ex,int ey,unsigned dst,uint32_t color);
void frontend_present(void);
void frontend_text(const char *text);
void frontend_quake(unsigned level); /* 0=stop, >0=start screen shake at that level */
void set_choices(const char **items,unsigned count,int kind);
void audio_load(int ch,const char *name);
void audio_play(int ch,bool loop);
void audio_stop(int ch);
void audio_init(void);
void audio_fini(void);
int save_state(unsigned slot);
int load_state(unsigned slot);
