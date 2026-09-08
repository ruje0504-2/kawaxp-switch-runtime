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
void vm_jump_at(const char *name,uint32_t addr); /* engine-side jump (album B-back) */
void vm_step(void);
void vm_choose(unsigned i);
unsigned vm_menu_nav(unsigned sel,int dx,int dy); /* grid nav in engine menus */
void vm_menu_cancel(void);      /* B = 戻る / close engine menu */
void vm_advance(void);
extern unsigned vm_choice_kind(void);
unsigned vm_menu_value(unsigned i);
void vm_album_menu(void);
void frontend_refresh(void);
extern unsigned vm_last_menu_addr(void);
extern void vm_slot_menu(bool save); /* engine save/load slot menu (keys/smoke) */
uint32_t ev(struct mes_expression *e);
void draw_image(const char *name,unsigned dst,int x,int y);
void copy_rect(int sx,int sy,int ex,int ey,unsigned src,int dx,int dy,unsigned dst,bool mask);
void fill_rect(int x,int y,int ex,int ey,unsigned dst,uint32_t color);
void frontend_present(void);
void frontend_text(const char *text);
void frontend_quake(unsigned level); /* 0=stop, >0=start screen shake at that level */
void frontend_msk_note(unsigned idx); /* util35 transition mask index validation */
bool frontend_xfade_start(unsigned idx); /* util35 masked crossfade: true while animating */
bool frontend_xfade_active(void);  /* true while the wipe frames are still running */
bool frontend_fadein_start(void); /* util4 page fade-in from black; true while animating */
bool frontend_fadein_active(void);
void set_choices(const char **items,unsigned count,int kind);
void audio_load(int ch,const char *name);
void audio_play(int ch,bool loop);
void audio_stop(int ch);
void audio_stop_all(void); /* silence all channels, cancel pending decodes */
void audio_stop_voice_se(void); /* stop voice/SE only, keep ch0 BGM */
extern int audio_bgm_dirty;   /* ch0 load happened since last replay */
void audio_bgm_snapshot(void); /* remember ch0 track before a replay */
void audio_bgm_restore(void);  /* resume that ch0 track after a replay */
void audio_init(void);
void audio_fini(void);
int save_state(unsigned slot);
int load_state(unsigned slot);

bool frontend_ppf_start(int x,int y,int w,int h);
bool frontend_ppf_active(void);
