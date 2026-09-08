/* GPL-2.0-or-later */
#include "kawa.h"
#include "ai5/cg.h"
#include "ai5/game.h"
#include "text.h"
#include "ax_render.h"
#include "title_ui.h"
#include "extras_ui.h"
#include "scene_ui.h"
#include "save_ui.h"
#include <unistd.h>
#include <sys/stat.h>
#include <zlib.h>
#include <errno.h>
#include <stdlib.h>
#include <limits.h>
#include <math.h>
#ifdef __SWITCH__
#include <switch.h>
#endif
SDL_Surface *surfaces[NSURF];SDL_Window *window;SDL_Renderer *renderer;struct kawa_font *font;
bool running=true,smoke=false;
char data_dir[1024],save_dir[1024];
static SDL_Texture *texture;
static SDL_Surface *canvas;
static char labels[100][1024];static unsigned choice_count,selected;
static bool choice_open;
static int smoke_messages=30;
static const char *screenshot;
static char status[256];static Uint32 status_until;
static bool ff_hold; /* fast-forward while controller X / keyboard x held (not in smoke) */
/* Debug bisect switches: enabled by a flag file in data_dir (no env on Switch). */
/* (debug bisect flag switches removed 2026-09-08 per user request) */
static bool hide_msg;
static bool gfx_dirty=true;
void frontend_refresh(void){gfx_dirty=true;}
static unsigned fr_cnt;static Uint32 fr_acc,fr_max,fr_logt; /* rebuild/upload canvas only when the frame actually changed */
static bool frame_log; /* KAWA_FRAMELOG=1: periodic FRAME stats to the log (off by default) */
static char last_msg[1024];static int last_wait=-1,last_sel=-1;static bool last_cho=false,last_hide=false,last_ff=false,last_status=false; /* hide message window/text: B (keyboard b) toggles */
struct ax_player animation;
/* Draw history: every draw_image call (name,dst,x,y) is appended so a v3
 * save can replay them in order on load.  A scene layer often receives
 * several stacked IMAGEs (background then characters); replaying the whole
 * history rebuilds each layer exactly. */
#define IMG_HIST_MAX 256
struct img_hist_ent { char name[40]; unsigned short dst; short x,y; };
static struct img_hist_ent img_hist[IMG_HIST_MAX];
static unsigned img_hist_n;
/* Per-surface last-source kept for quick checks (dst 8 = AX source). */
static char surf_src[NSURF][40];
static short surf_x[NSURF], surf_y[NSURF];
/* AI.exe 43e590 / 437270: timer-driven row displacement, script-controlled. */
static bool quake_level;
static unsigned quake_phase;
static Uint32 quake_next;
static int quake_wave[480];
void frontend_quake(unsigned level) {
 if(level==1){
  for(unsigned i=0;i<480;i++)quake_wave[i]=(int)(100.0*cos(0.02617993833*i));
  quake_phase=0;quake_next=SDL_GetTicks()+80;quake_level=true;
 }else if(level!=255)quake_level=false;
 gfx_dirty=true;
}
static uint8_t *msk_data[16];
static const uint8_t *msk_get(unsigned idx) {
 if(idx>=16)return NULL;
 if(!msk_data[idx]) {
  char name[16];snprintf(name,sizeof(name),"%02u.msk",idx);
  struct archive_data *d=seq_arc?archive_get(seq_arc,name):NULL;
  if(d&&d->size>=307200){msk_data[idx]=(uint8_t*)malloc(307200);if(msk_data[idx])memcpy(msk_data[idx],d->data,307200);}
  if(d)archive_data_release(d);
  if(getenv("KAWA_MSKTRACE"))note("MSK %s %s",name,msk_data[idx]?"loaded":"missing");
 }
 return msk_data[idx];
}
void frontend_msk_note(unsigned idx){(void)msk_get(idx);}

/* Mask bytes are alpha, not a CG palette: 11 soft phases plus final commit. */
static struct { bool on; unsigned idx,step; SDL_Surface *from; Uint32 next; } xf;
void frontend_xfade_skip(void);
bool frontend_xfade_start(unsigned idx) {
 frontend_xfade_skip();
 if(!surfaces[0]||!surfaces[1])return false;
 if(smoke||!msk_get(idx)){
  copy_rect(0,0,639,479,1,0,0,0,false);return false;
 }
 xf.from=SDL_ConvertSurface(surfaces[0],surfaces[0]->format,0);
 if(!xf.from){copy_rect(0,0,639,479,1,0,0,0,false);return false;}
 xf.on=true;xf.idx=idx;xf.step=0;xf.next=SDL_GetTicks();return true;
}
bool frontend_xfade_active(void){return xf.on;}
/* util4 = page fade-in from black (AI.exe 0x43e788): draw the staged page at
 * growing brightness 0..255, one frame at a time; any input skips to full. */
static struct { bool on; unsigned step,steps; Uint32 next; } fd;
bool frontend_fadein_start(void){
 if(smoke||!surfaces[0]||!surfaces[1]){
  if(surfaces[0]&&surfaces[1]){SDL_Rect r={0,0,640,480};SDL_BlitSurface(surfaces[1],&r,surfaces[0],&r);}
  return false;
 }
 fd.on=true;fd.step=0;fd.steps=12;fd.next=SDL_GetTicks()+20;
 return true;
}
void frontend_fadein_poll(void){
 if(!fd.on)return;
 if((Sint32)(SDL_GetTicks()-fd.next)<0)return;
 fd.next=SDL_GetTicks()+20;
 fd.step++;
 unsigned tt=(fd.step*255)/fd.steps;if(tt>255)tt=255;
 SDL_Surface*dst=surfaces[0],*to=surfaces[1];
 if(!dst||!to){fd.on=false;return;}
 unsigned char*dp=(unsigned char*)dst->pixels,*tp=(unsigned char*)to->pixels;
 int dpitch=dst->pitch,tpitch=to->pitch;
 int w=dst->w<to->w?dst->w:to->w,h=dst->h<to->h?dst->h:to->h;
 for(int y=0;y<h;y++){
  unsigned char*drow=dp+y*dpitch,*trow=tp+y*tpitch;
  for(int x=0;x<w;x++){
   drow[x*4]=(unsigned char)((unsigned)trow[x*4]*tt>>8);
   drow[x*4+1]=(unsigned char)((unsigned)trow[x*4+1]*tt>>8);
   drow[x*4+2]=(unsigned char)((unsigned)trow[x*4+2]*tt>>8);
   drow[x*4+3]=255;
  }
 }
 gfx_dirty=true;
 if(fd.step>=fd.steps)fd.on=false;
}
void frontend_fadein_skip(void){
 if(fd.on){
  if(surfaces[0]&&surfaces[1]){SDL_Rect r={0,0,640,480};SDL_BlitSurface(surfaces[1],&r,surfaces[0],&r);}
  fd.on=false;gfx_dirty=true;
 }
}
bool frontend_fadein_active(void){return fd.on;}
void frontend_xfade_poll(void) {
 if(!xf.on||(Sint32)(SDL_GetTicks()-xf.next)<0)return;
 xf.next=SDL_GetTicks()+20;
 if(++xf.step==12){frontend_xfade_skip();return;}
 SDL_Surface *dst=surfaces[0],*to=surfaces[1];
 unsigned L=xf.step*32;
 for(int y=0;y<480;y++)for(int x=0;x<640;x++){
  unsigned m=msk_data[xf.idx][y*640+x];
  unsigned v=m<L?L-m+16:0;if(v>255)v=255;
  uint8_t *d=(uint8_t*)dst->pixels+y*dst->pitch+4*x;
  uint8_t *f=(uint8_t*)xf.from->pixels+y*xf.from->pitch+4*x;
  uint8_t *t=(uint8_t*)to->pixels+y*to->pitch+4*x;
  for(int c=0;c<3;c++)d[c]=(f[c]*(255-v)+t[c]*v)/255;
  d[3]=255;
 }
 gfx_dirty=true;
}
void frontend_xfade_skip(void) {
 if(!xf.on)return;
 copy_rect(0,0,639,479,1,0,0,0,false);
 xf.on=false;SDL_FreeSurface(xf.from);xf.from=NULL;
}
/* util8: preserve display in page 7, composite new region, then interlace rows.
 * One original iteration waits at most 1 ms; batch elapsed iterations per display.
 * Clicking removes the wait but still executes every row and the final commit. */
static struct {bool on; int x,y,w,h,i; Uint32 next;} ppf;
bool frontend_ppf_active(void){return ppf.on;}
static void ppf_finish(void){
 copy_rect(ppf.x,ppf.y,ppf.x+ppf.w-1,ppf.y+ppf.h-1,1,ppf.x,ppf.y,0,true);
 ppf.on=false;
}
bool frontend_ppf_start(int x,int y,int w,int h){
 if(ppf.on)ppf_finish();
 copy_rect(0,0,639,479,0,0,0,7,false);
 /* Zero-sized original calls have no scanlines; keep the existing no-op region. */
 if(w<=0||h<=0)return false;
 int right=x+w,bottom=y+h;if(x<0)x=0;if(y<0)y=0;
 if(right>640)right=640;
 if(bottom>480)bottom=480;
 w=right-x;h=bottom-y;if(w<=0||h<=0)return false;
 ppf.x=x;ppf.y=y;ppf.w=w;ppf.h=h;ppf.i=0;ppf.next=SDL_GetTicks();
 copy_rect(x,y,x+w-1,y+h-1,1,x,y,7,true);
 ppf.on=true;return true;
}
static void ppf_poll(bool fast){
 if(!ppf.on)return;
 Uint32 now=SDL_GetTicks();
 while(ppf.i<ppf.h/2&&(fast||smoke||(Sint32)(now-ppf.next)>=0)){
  int rows[2]={ppf.y+2*ppf.i,ppf.y+ppf.h-1-2*ppf.i};
  for(int j=0;j<2;j++)copy_rect(ppf.x,rows[j],ppf.x+ppf.w-1,rows[j],7,ppf.x,rows[j],0,false);
  ppf.i++;ppf.next++;
 }
 if(ppf.i==ppf.h/2)ppf_finish();
}
static void quake_poll(void){
 if(!quake_level)return;
 Uint32 now=SDL_GetTicks();if((Sint32)(now-quake_next)<0)return;
 quake_next=now+80;
 if(xf.on||fd.on||ppf.on)return;
 SDL_Surface *src=surfaces[1],*dst=surfaces[0];if(!src||!dst)return;
 /* Original DIB walks from its bottom storage row towards the top: screen y=0. */
 for(int y=0;y<479;y++){
  int dx=quake_wave[quake_phase]/6;quake_phase=(quake_phase+1)%480;
  uint8_t *d=(uint8_t*)dst->pixels+y*dst->pitch;
  uint8_t *t=(uint8_t*)src->pixels+y*src->pitch;
  for(int x=0;x<640;x++){
   int sx=x-dx;
   if(sx<0)sx+=640;
   /* Negative displacement reads the source extension when the page has one. */
   if(sx>=src->w)sx%=640;
   memcpy(d+4*x,t+4*sx,4);
  }
 }
 gfx_dirty=true;
}
static bool ensure_surface(unsigned i,int w,int h) {
 if(i>=NSURF||w>4096||h>4096||w<0||h<0){fail("Invalid surface %u %dx%d",i,w,h);return false;}
 if(w<640)w=640;if(h<480)h=480;
 if(surfaces[i]&&surfaces[i]->w>=w&&surfaces[i]->h>=h)return true;
 SDL_Surface *n=SDL_CreateRGBSurfaceWithFormat(0,w,h,32,SDL_PIXELFORMAT_RGBA32);
 if(!n){fail("Surface allocation failed");return false;}
 SDL_SetSurfaceBlendMode(n,SDL_BLENDMODE_NONE);
 SDL_FillRect(n,NULL,SDL_MapRGBA(n->format,0,0,0,255));
 if(surfaces[i]){SDL_BlitSurface(surfaces[i],NULL,n,NULL);SDL_FreeSurface(surfaces[i]);}surfaces[i]=n;return true;
}
static unsigned rgb(SDL_Surface *s,uint32_t c) {return SDL_MapRGBA(s->format,(c>>16)&255,(c>>8)&255,c&255,255);}
/* blit a region of a 32-bit surface, skipping classic AI5 chroma keys
 * (green 0,255,0 and magenta 255,0,255; red is real art in these parts).
 * Used for the UI part images (selparts highlight bar etc.) loaded from BMP. */
static void blit_keyed(SDL_Surface *src,SDL_Surface *dst,int sx,int sy,int sw,int sh,int dx,int dy){
 if(!src||!dst)return;
 if(src->format->BytesPerPixel!=4||dst->format->BytesPerPixel!=4)return;
 if(sx<0){sw+=sx;dx-=sx;sx=0;}if(sy<0){sh+=sy;dy-=sy;sy=0;}
 if(sx+sw>src->w)sw=src->w-sx;if(sy+sh>src->h)sh=src->h-sy;
 if(sw<=0||sh<=0)return;
 if(dx<0){sw+=dx;sx-=dx;dx=0;}if(dy<0){sh+=dy;sy-=dy;dy=0;}
 if(dx+sw>dst->w)sw=dst->w-dx;if(dy+sh>dst->h)sh=dst->h-dy;
 if(sw<=0||sh<=0)return;
 unsigned char*sp=(unsigned char*)src->pixels+sy*src->pitch+sx*4;
 unsigned char*dp=(unsigned char*)dst->pixels+dy*dst->pitch+dx*4;
 for(int y=0;y<sh;y++){
  unsigned char*sr=sp+(size_t)y*src->pitch,*dr=dp+(size_t)y*dst->pitch;
  for(int x=0;x<sw;x++){
   unsigned char*px=sr+x*4;
   unsigned char r=px[0],g=px[1],b=px[2];
   if(g>200&&r<80&&b<80)continue;      /* green chroma -> transparent */
   if(r>200&&b>200&&g<80)continue;     /* magenta chroma -> transparent */
   unsigned char*d=dr+x*4;d[0]=r;d[1]=g;d[2]=b;d[3]=255;
  }
 }
}
void animation_load(const char *name) {
 st.var[14]=1;
 struct archive_data *d=seq_arc?archive_get(seq_arc,name):NULL;
 if(!d){fail("Missing AX %s",name);return;}
 bool ok=ax_load(&animation,name,d->data,d->size);archive_data_release(d);
 if(!ok)fail("Invalid AX %s",name);else {st.var[14]=0;note("AX LOAD %s (%u bytes)",name,animation.size);}
}
static void animation_draw(const uint32_t d[7],void *context) {
 gfx_dirty=true;
 (void)context;
 if(!surfaces[8]){fail("AX %s has no source surface 8",animation.name);return;}
 if(!d[3]||!d[4])return;
 if(!ensure_surface(1,d[5]+d[3],d[6]+d[4])||!ensure_surface(0,640,480))return;
 ax_blit(surfaces[8],surfaces[1],d[1],d[2],d[3],d[4],d[5],d[6]);
 copy_rect(d[5],d[6],d[5]+d[3]-1,d[6]+d[4]-1,1,d[5],d[6],0,false);
 if(getenv("KAWA_AX_TRACE"))note("AX DRAW %s kind=%u src=%u,%u %ux%u dst=%u,%u",animation.name,d[0],d[1],d[2],d[3],d[4],d[5],d[6]);
}
void animation_update(unsigned elapsed_ms) {
 if(st.waiting==99)return;
 /* Bound catch-up after OS suspension; never spin through a long frame backlog. */
 if(elapsed_ms>200)elapsed_ms=200;
 animation.phase_ms+=elapsed_ms;
 while(animation.phase_ms>=AX_TICK_MS) {
  animation.phase_ms-=AX_TICK_MS;
  if(!ax_tick(&animation,animation_draw,NULL)){fail("Malformed AX program in %s",animation.name);break;}
 }
 if(st.waiting==4&&!ax_waiting(&animation))st.waiting=0;
}
void fill_rect(int x,int y,int ex,int ey,unsigned dst,uint32_t color) {
 gfx_dirty=true;
 if(ex<x||ey<y)return;
 if(!ensure_surface(dst,ex+1,ey+1))return;
 SDL_Rect r={x,y,ex-x+1,ey-y+1};SDL_FillRect(surfaces[dst],&r,rgb(surfaces[dst],color));
}
void copy_rect(int sx,int sy,int ex,int ey,unsigned src,int dx,int dy,unsigned dst,bool mask) {
 gfx_dirty=true;
 if(src>=NSURF||dst>=NSURF){fail("Invalid surface copy %u -> %u",src,dst);return;}
 int w=ex-sx+1,h=ey-sy+1;if(w<=0||h<=0)return;
 if(!ensure_surface(src,ex+1,ey+1)||!ensure_surface(dst,dx+w,dy+h))return;
 SDL_Rect from={sx,sy,w,h},to={dx,dy,w,h};
 SDL_Surface *s=surfaces[src],*temp=NULL;
 if(src==dst){temp=SDL_ConvertSurface(s,s->format,0);s=temp;if(!s){fail("Blit allocation");return;}}
 if(mask)ax_blit(s,surfaces[dst],sx,sy,w,h,dx,dy);
 else {SDL_SetSurfaceBlendMode(s,SDL_BLENDMODE_NONE);SDL_BlitSurface(s,&from,surfaces[dst],&to);}
 if(temp)SDL_FreeSurface(temp);
}
void draw_image(const char *name,unsigned dst,int x,int y) {
 gfx_dirty=true;
 char real[256];snprintf(real,sizeof(real),"%s",name);char *ext=strrchr(real,'.');
 if(ext&&!strcasecmp(ext,".bmp"))snprintf(ext,sizeof(real)-(ext-real),".gcc");
 struct archive_data *d=archive_get(cg_arc,real);
 if(!d){fail("Missing image %s",real);return;}
 struct cg *c=cg_load_arcdata(d);archive_data_release(d);if(!c){fail("Decode %s",real);return;}
 if(x<0)x=c->metrics.x;if(y<0)y=c->metrics.y;
 if(ensure_surface(dst,x+c->metrics.w,y+c->metrics.h)) {
  SDL_Surface *s=SDL_CreateRGBSurfaceWithFormatFrom(c->pixels,c->metrics.w,c->metrics.h,32,c->metrics.w*4,SDL_PIXELFORMAT_RGBA32);
  if(s){SDL_SetSurfaceBlendMode(s,SDL_BLENDMODE_NONE);SDL_Rect to={x,y,0,0};SDL_BlitSurface(s,NULL,surfaces[dst],&to);SDL_FreeSurface(s);}
  st.sys[8]=x;st.sys[9]=y;st.sys[10]=c->metrics.w;st.sys[11]=c->metrics.h;
  if(dst<NSURF){
   snprintf(surf_src[dst],sizeof(surf_src[dst]),"%s",real);surf_x[dst]=(short)x;surf_y[dst]=(short)y;
   if(img_hist_n<IMG_HIST_MAX){struct img_hist_ent *e=&img_hist[img_hist_n++];
    snprintf(e->name,sizeof(e->name),"%s",real);e->dst=(unsigned short)dst;e->x=(short)x;e->y=(short)y;}
  }
  note("IMAGE %s dst=%u %dx%d at %d,%d",real,dst,c->metrics.w,c->metrics.h,x,y);
 }
 cg_free(c);
}
void frontend_text(const char *text) {
 size_t used=strlen(st.text),n=strlen(text);
 if(used+n>=sizeof(st.text)){fail("Message buffer overflow");return;}
 memcpy(st.text+used,text,n+1);
}
/* story choices (kind2): nothing is pre-selected when the menu opens; the first
 * d-pad/keyboard move (or a touch) arms a selection, A only confirms after that,
 * so a stray A press cannot skip past the first option. */
static bool need_sel;
static bool title_seen;
static void menu_move(int dx,int dy){
 if(st.waiting!=2||!choice_open)return;
 if(vm_choice_kind()==37&&title_ui_active()){title_ui_skip();gfx_dirty=true;return;}
 if(need_sel&&(vm_choice_kind()==2||vm_choice_kind()==37)){
  need_sel=false;
  selected=(dy<0&&choice_count)?choice_count-1:0;
  gfx_dirty=true;return;
 }
 selected=vm_menu_nav(selected,dx,dy);gfx_dirty=true;
}
void set_choices(const char **items,unsigned count,int kind) {
 (void)kind;choice_count=count>100?100:count;selected=0;choice_open=true;
 need_sel=(vm_choice_kind()==2||vm_choice_kind()==37);
 if(vm_choice_kind()==37){
  title_ui_begin(surfaces[7],surfaces[1],msk_get(11),!title_seen);title_seen=true;
  if(smoke)title_ui_skip();
 }
 for(unsigned i=0;i<choice_count;i++)snprintf(labels[i],sizeof(labels[i]),"%s",items[i]);
}
/* 4391d0: window (0,376), text-local origin (32,24).
 * 443710: choice strips at local y=16+18*row; four rows per column.
 * DEFINE.MES text bounds 32..624 give 592px; original choice width subtracts 8. */
static SDL_Rect story_choice_rect(unsigned i){
 unsigned base=(selected/8)*8,n=choice_count-base;if(n>8)n=8;
 unsigned j=i-base;int width=n>4?288:584;
 return (SDL_Rect){32+(int)(j/4)*width,392+(int)(j%4)*18,width,18};
}
static SDL_Surface *msg_cache;
static char msg_key[sizeof(st.text)];
static uint32_t msg_col;
static SDL_Surface *msg_surface(int *w,int *h){
 if(msg_cache&&!strcmp(msg_key,st.text)&&msg_col==st.color){*w=592;*h=msg_cache->h;return msg_cache;}
 SDL_Surface*s0=kawa_text_render(font,st.text,592,0x000000);
 SDL_Surface*s1=kawa_text_render(font,st.text,592,st.color);
 int h1=s0?s0->h:0,h2=s1?s1->h:0;int hh=h1>h2?h1:h2;
 if(hh<=0){if(s0)SDL_FreeSurface(s0);if(s1)SDL_FreeSurface(s1);return NULL;}
 SDL_Surface*c=SDL_CreateRGBSurfaceWithFormat(0,592,hh+2,32,SDL_PIXELFORMAT_RGBA32);
 if(!c){if(s0)SDL_FreeSurface(s0);if(s1)SDL_FreeSurface(s1);return NULL;}
 SDL_SetSurfaceBlendMode(c,SDL_BLENDMODE_BLEND);
 if(s0){SDL_Rect d={1,1,0,0};SDL_BlitSurface(s0,NULL,c,&d);SDL_FreeSurface(s0);}
 if(s1){SDL_Rect d={0,0,0,0};SDL_BlitSurface(s1,NULL,c,&d);SDL_FreeSurface(s1);}
 if(msg_cache)SDL_FreeSurface(msg_cache);
 msg_cache=c;snprintf(msg_key,sizeof(msg_key),"%s",st.text);msg_col=st.color;
 *w=592;*h=hh+2;
 return msg_cache;
}
static void text_at(const char *text,int x,int y,uint32_t color,int width) {
 if(!font||!text[0])return;
 SDL_Surface *s=kawa_text_render(font,text,width,color);
 if(!s)return;
 SDL_Rect d={x,y,0,0};SDL_BlitSurface(s,NULL,canvas,&d);SDL_FreeSurface(s);
}
/* cached 640x104 message-window surface: mwaku bottom half rows104..207 with frame
 * opaque and interior as a dark-gray veil (alpha). Blended onto the canvas in ONE
 * SDL blit per frame instead of a per-pixel loop. */
static SDL_Surface *win_surf;
static SDL_Surface *win_src6;
static SDL_Surface *win_build(SDL_Surface*mw){
 SDL_Surface*t=SDL_CreateRGBSurfaceWithFormat(0,640,104,32,SDL_PIXELFORMAT_RGBA32);
 if(!t)return NULL;
 SDL_SetSurfaceBlendMode(t,SDL_BLENDMODE_BLEND);
 unsigned char*tp=(unsigned char*)t->pixels;
 unsigned char*sp=(unsigned char*)mw->pixels;
 for(int yy=104;yy<208;yy++){
  unsigned char*srow=sp+yy*mw->pitch;
  unsigned char*trow=tp+(yy-104)*t->pitch;
  for(int xx=0;xx<640;xx++){
   unsigned char*sx=srow+xx*4,*d=trow+xx*4;
   unsigned g=(unsigned char)((sx[0]*3+sx[1]*6+sx[2])/10);
   if(g>118||xx<8||xx>=632){d[0]=sx[0];d[1]=sx[1];d[2]=sx[2];d[3]=255;}
   else {d[0]=40;d[1]=40;d[2]=40;d[3]=194;}   /* dark-mid gray veil */
  }
 }
 return t;
}
static void win_ensure(void){
 SDL_Surface*mw=surfaces[6];
 if(!mw||mw->w<640||mw->h<208){if(win_surf){SDL_FreeSurface(win_surf);win_surf=NULL;}win_src6=NULL;return;}
 if(win_surf&&win_src6==mw)return;
 if(win_surf)SDL_FreeSurface(win_surf);
 win_surf=win_build(mw); win_src6=mw;
}
void frontend_present(void) {
 if(!canvas)return;
 /* state-driven dirtiness */
 if(strcmp(last_msg,st.text)){
   snprintf(last_msg,sizeof(last_msg),"%s",st.text);gfx_dirty=true;}
 if(last_wait!=st.waiting){last_wait=st.waiting;gfx_dirty=true;}
 if(last_sel!=selected){last_sel=selected;gfx_dirty=true;}
 if(last_cho!=choice_open){last_cho=choice_open;gfx_dirty=true;}
 if(last_hide!=hide_msg){last_hide=hide_msg;gfx_dirty=true;}
 if(last_ff!=ff_hold){last_ff=ff_hold;gfx_dirty=true;}
 {bool st_now=status_until>SDL_GetTicks(); if(st_now!=last_status){last_status=st_now;gfx_dirty=true;} if(st_now)gfx_dirty=true;}

 if(!gfx_dirty){ /* nothing changed: reuse last uploaded texture */
  SDL_RenderClear(renderer);SDL_RenderCopy(renderer,texture,NULL,NULL);SDL_RenderPresent(renderer);
  return;
 }
 gfx_dirty=false;
 SDL_FillRect(canvas,NULL,rgb(canvas,0)); if(surfaces[0]){SDL_Rect r={0,0,640,480};SDL_BlitSurface(surfaces[0],&r,canvas,NULL);}
 bool engine_menu=(st.waiting==2&&choice_open&&vm_choice_kind()!=2);
 if(!engine_menu&&((st.text[0]&&!hide_msg)||(st.waiting==2&&choice_open&&vm_choice_kind()==2))&&(!hide_msg)) {
  /* engine message window = the BOTTOM half (rows 104..207) of the boot-loaded
   * mwaku.gcc panel (640x208 = two stacked 104-high frames; AI.exe 0x4391d0 window
   * height is 0x68=104) drawn into the 104-high band at the bottom (y 376..479).
   * Frame pixels stay opaque; the gray interior is blended translucent over the CG.
   * Story choices share this same window and backdrop, listed top-down. */
  win_ensure();
  if(win_surf){
   SDL_Rect to={0,376,640,104};
   SDL_BlitSurface(win_surf,NULL,canvas,&to);   /* one alpha blend blit */
  } else {
   unsigned char*dp=(unsigned char*)canvas->pixels;
   for(int yy=376;yy<480;yy++){
    unsigned char*row=dp+yy*canvas->pitch;
    for(int xx=0;xx<640;xx++){
     unsigned char*r=row+xx*4;
     r[0]=(unsigned char)((r[0]*6+150)/7);
     r[1]=(unsigned char)((r[1]*6+160)/7);
     r[2]=(unsigned char)((r[2]*6+170)/7);
    }
   }
  }
  if(st.waiting==2&&choice_open&&vm_choice_kind()==2){
   /* story choices render as plain text inside the message window, exactly like
    * the dialogue line (no box/frame); the selected row is tinted. */
   unsigned start=(selected/8)*8,visible=choice_count-start;if(visible>8)visible=8;
   SDL_Rect clip;SDL_GetClipRect(canvas,&clip);
   for(unsigned j=0;j<visible;j++){
    SDL_Rect row=story_choice_rect(start+j);
    SDL_SetClipRect(canvas,&row);
    bool sel=(!need_sel&&(start+j==selected));
    text_at(labels[start+j],row.x,row.y,sel?0xffe9a0:0xffffff,row.w);
   }
   SDL_SetClipRect(canvas,&clip);
  } else {
   int tw=592,th=0;SDL_Surface*ms=msg_surface(&tw,&th);
   if(ms){
    SDL_Rect clip,area={32,400,592,72};SDL_GetClipRect(canvas,&clip);SDL_SetClipRect(canvas,&area);
    SDL_Rect to={32,400,0,0};SDL_BlitSurface(ms,NULL,canvas,&to);SDL_SetClipRect(canvas,&clip);
   }
  }
 }
 if(engine_menu) {
  if(vm_choice_kind()==37&&surfaces[7]&&surfaces[7]->h>=888) {
   unsigned results[6];unsigned count=choice_count<6?choice_count:6;
   for(unsigned i=0;i<count;i++)results[i]=vm_menu_value(i);
   title_ui_draw(surfaces[7],canvas,results,count,need_sel?~0u:selected);
  } else if(vm_choice_kind()==43){
   extern unsigned scene_page;
   scene_ui_draw(canvas,scene_page,vm_menu_value(selected));
  } else if(vm_choice_kind()==41||vm_choice_kind()==42||vm_choice_kind()==44){
   extras_draw(canvas,vm_choice_kind(),vm_menu_value(selected));
  } else if(save_ui_kind(vm_choice_kind())){
   save_ui_draw(canvas,vm_choice_kind(),vm_menu_value(selected));
  } else {
  /* Remaining engine menus use the text fallback. */
  unsigned start=selected>=8?selected-7:0,visible=choice_count-start;if(visible>8)visible=8;
  int lh=34; int y0=104;
  for(unsigned j=0;j<visible;j++){
   int y=y0+(int)j*lh;
   bool sel=(start+j==selected);
   text_at(labels[start+j],120,y,sel?0xffe9a0:0xffffff,440);
  }
  }
 }
 if(st.waiting==99) {
  SDL_Rect bg={0,0,640,160};SDL_FillRect(canvas,&bg,rgb(canvas,0x501010));
  text_at("Runtime stopped — unsupported operation",14,12,0xffffff,610);
  text_at(error_text,14,44,0xffffff,610);
 }
 if(status_until>SDL_GetTicks()){SDL_Rect r={0,0,640,34};SDL_FillRect(canvas,&r,rgb(canvas,0x12212d));text_at(status,12,4,0xffffff,610);}
SDL_UpdateTexture(texture,NULL,canvas->pixels,canvas->pitch);
 SDL_RenderClear(renderer);SDL_RenderCopy(renderer,texture,NULL,NULL);SDL_RenderPresent(renderer);
}
static void notify_status(const char *s) {snprintf(status,sizeof(status),"%s",s);status_until=SDL_GetTicks()+2500;}
static bool valid_loc(struct loc *p) {return memchr(p->script,0,32)&&p->addr<16*1024*1024;}
/* In-memory gzip (RFC1952) helpers.  Save payloads are compressed in RAM and
 * written with plain fopen/fwrite, which works on every layout including the
 * fsdev save: device (gzopen/gzclose file IO can fail there). */
static unsigned char *gz_mem_compress(const unsigned char *src, size_t n, size_t *outn) {
 uLongf cap = compressBound((uLong)n) + 64;
 unsigned char *out = malloc(cap); if (!out) return NULL;
 z_stream s; memset(&s,0,sizeof(s));
 if (deflateInit2(&s, Z_BEST_COMPRESSION, Z_DEFLATED, 15+16, 8, Z_DEFAULT_STRATEGY) != Z_OK) { free(out); return NULL; }
 s.next_in = (Bytef*)src; s.avail_in = (uInt)n;
 s.next_out = out; s.avail_out = (uInt)cap;
 int r = deflate(&s, Z_FINISH);
 deflateEnd(&s);
 if (r != Z_STREAM_END) { free(out); return NULL; }
 *outn = s.total_out;
 return out;
}
static unsigned char *gz_mem_decompress(const unsigned char *gz, size_t n, size_t *outn) {
 size_t cap = 1 << 20; unsigned char *out = malloc(cap); if (!out) return NULL;
 z_stream s; memset(&s,0,sizeof(s));
 if (inflateInit2(&s, 15+16) != Z_OK) { free(out); return NULL; }
 s.next_in = (Bytef*)gz; s.avail_in = (uInt)n;
 size_t done = 0; int r = Z_OK;
 while (r != Z_STREAM_END) {
  if (done == cap) { cap *= 2; unsigned char *o = realloc(out, cap); if (!o) { free(out); inflateEnd(&s); return NULL; } out = o; }
  s.next_out = out + done; s.avail_out = (uInt)(cap - done);
  r = inflate(&s, Z_NO_FLUSH);
  done = cap - s.avail_out;
  if (r != Z_OK && r != Z_STREAM_END) { free(out); inflateEnd(&s); return NULL; }
  if (done == cap && r != Z_STREAM_END) { cap *= 2; unsigned char *o = realloc(out, cap); if (!o) { free(out); inflateEnd(&s); return NULL; } out = o; }
 }
 inflateEnd(&s); *outn = done; return out;
}
int save_state(unsigned slot) {
 if(st.waiting!=1){notify_status("Save is available at a dialogue pause.");return 0;}
 char path[1200],tmp[1200];snprintf(path,sizeof(path),"%s/slot%u.kws",save_dir,slot);snprintf(tmp,sizeof(tmp),"%s/slot%u.tmp",save_dir,slot);
 /* v4: save the full pixel contents of every non-empty surface (like v2),
  * plus VM+AX state.  Loading restores every layer (background, characters,
  * AX source) exactly, so pictures and animations resume with no re-draw
  * needed.  gzip keeps it small (scenes are mostly flat CG). */
 size_t cap=(1<<22), n=0;
 unsigned char *mb=malloc(cap);
 if(!mb){notify_status("Save out of memory");return 0;}
 #define MB_NEED(x) do { if (n+(x)>cap){ while(cap<n+(x))cap*=2; unsigned char*np=realloc(mb,cap); if(!np){free(mb);notify_status("Save out of memory");return 0;} mb=np; } } while(0)
 #define MB_PUT(p,x) do { MB_NEED(x); memcpy(mb+n,(p),(x)); n+=(x); } while(0)
 const uint32_t hdr[]={0x3153574b,4,sizeof(st),NSURF};
 MB_PUT(hdr,sizeof(hdr)); MB_PUT(&st,sizeof(st));
 for(unsigned i=0;i<NSURF;i++) {
  uint32_t wh[]={surfaces[i]?surfaces[i]->w:0,surfaces[i]?surfaces[i]->h:0};
  MB_PUT(wh,sizeof(wh));
  if(surfaces[i])for(int y=0;y<surfaces[i]->h;y++)MB_PUT((uint8_t*)surfaces[i]->pixels+y*surfaces[i]->pitch,wh[0]*4);
 }
 MB_PUT(&animation,sizeof(animation));
#undef MB_NEED
 #undef MB_PUT
 size_t orign=n;
 size_t gzn=0; unsigned char *gz=gz_mem_compress(mb,orign,&gzn); free(mb);
 if(!gz){notify_status("Save compress failed");return 0;}
 bool ok=false;
#ifdef __SWITCH__
 {extern int switch_save_active;
  if(switch_save_active){
   /* HOS SaveData: write the gz file through the native FsFileSystem API
    * (fsdev stdio dies past ~1.5MB; native chunked write handles MBs). */
   extern int switch_hos_save_write(const char*,const void*,size_t);
   extern int switch_hos_save_remove(const char*);
   /* This HOS SaveData rejects any single file past ~1.5MB, so store the gz
    * as 1MB segments slot%u.kws.0, .1, ... and join them on load. */
   const size_t SEG=(1u<<20);
   ok=true;
   for(int sgi=0;sgi<8&&ok;sgi++){
    size_t off=(size_t)sgi*SEG;
    if(off>=gzn)break;
    size_t n=gzn-off; if(n>SEG)n=SEG;
    char rel[64];
    snprintf(rel,sizeof(rel),"slot%u.kws.%d",slot,sgi);
    switch_hos_save_remove(rel);
    int wr=switch_hos_save_write(rel,gz+off,n);
    if(wr){ok=false;break;}
   }
   /* wipe any stale higher segments from an earlier larger save */
   if(ok){for(int sgi=8;sgi<16;sgi++){char rel[64];snprintf(rel,sizeof(rel),"slot%u.kws.%d",slot,sgi);switch_hos_save_remove(rel);}}

  } else
#endif
  {
   FILE *rf=fopen(tmp,"wb");
   if(rf){
    ok=fwrite(gz,1,gzn,rf)==gzn&&fclose(rf)==0;
    if(ok&&rename(tmp,path)){remove(path);ok=rename(tmp,path)==0;}
   }
   if(!ok)remove(tmp);
  }
#ifdef __SWITCH__
 }
#endif
 if(ok)note("SAVE %zu bytes -> %zu compressed",orign,gzn);
 char msg[64];snprintf(msg,sizeof(msg),ok?"Saved slot %u":"Save failed (slot %u)",slot);
 notify_status(msg);note("SAVE %s %s",ok?"OK":"FAIL",path);return ok;
}
/* After any load, cancel running transitions/effects so the restored frame
 * stays on screen instead of being overwritten by a stale fade/wipe/quake. */
static void load_clear_fx(void){
 xf.on=false; if(xf.from){SDL_FreeSurface(xf.from);xf.from=NULL;}
 fd.on=false; ppf.on=false;
 quake_level=false; quake_phase=0;
 gfx_dirty=true;
}
int load_state(unsigned slot) {
 char path[1200];snprintf(path,sizeof(path),"%s/slot%u.kws",save_dir,slot);
 unsigned char *gz=NULL; size_t fsz=0; bool fok=false;
#ifdef __SWITCH__
 {extern int switch_save_active;
  if(switch_save_active){
   extern int switch_hos_save_read(const char*,void*,size_t,size_t*);
   /* Load the segmented save: join slot%u.kws.0/.1/... until a segment is
    * shorter than 1MB or missing.  (A legacy single-file slot%u.kws also
    * still works via the fopen fallback below when segments are absent.) */
   const size_t SEG=(1u<<20);
   size_t cap=1u<<25; gz=malloc(cap);
   if(gz){
    size_t total=0; bool cont=true;
    for(int sgi=0;sgi<16&&cont;sgi++){
     char rel[64]; snprintf(rel,sizeof(rel),"slot%u.kws.%d",slot,sgi);
     size_t got=0;
     if(switch_hos_save_read(rel,gz+total,cap-total,&got)!=0||got==0){cont=false;break;}
     total+=got;
     if(got<SEG)cont=false;
    }
    if(total>0){fsz=total;fok=true;} else {free(gz);gz=NULL;}
   }
  }
 }
#endif
 if(!fok){
  FILE *rf=fopen(path,"rb");if(!rf){char msg[64];snprintf(msg,sizeof(msg),"No saved game in slot %u",slot);notify_status(msg);free(gz);return 0;}
  fseek(rf,0,SEEK_END); fsz=(size_t)ftell(rf); fseek(rf,0,SEEK_SET);
  gz=malloc(fsz?fsz:1); fok=gz&&fread(gz,1,fsz,rf)==fsz; fclose(rf);
 }
 if(!fok){free(gz);char msg[64];snprintf(msg,sizeof(msg),"No saved game in slot %u",slot);notify_status(msg);return 0;}
 size_t raw_n=0; unsigned char *raw=gz_mem_decompress(gz,fsz,&raw_n); free(gz);
 if(!raw){char msg[64];snprintf(msg,sizeof(msg),"Save damaged or incompatible (slot %u)",slot);notify_status(msg);return 0;}
 size_t at=0;
 #define RD(p,n) do { if (at+(n)>raw_n) { ok=false; goto rd_done; } memcpy((p),raw+at,(n)); at+=(n); } while (0)
 struct state next;uint32_t hdr[4];SDL_Surface *nextsurf[NSURF]={0};
 struct ax_player nextanim;ax_reset(&nextanim);
 bool ok=true;
 RD(hdr,sizeof(hdr));
 unsigned ver=hdr[1];
 if(!(hdr[0]==0x3153574b&&(ver>=1&&ver<=4)&&hdr[2]==sizeof(st)))ok=false;
 if(ok){RD(&next,sizeof(next)); if(!(next.depth<=NFRAME&&next.waiting==1&&memchr(next.text,0,sizeof(next.text))&&valid_loc(&next.ip)))ok=false;}
 if(ok)for(unsigned i=0;i<NPROC;i++)if(!valid_loc(&next.proc[i]))ok=false;
 if(ok)for(unsigned i=0;i<next.depth;i++)if(!valid_loc(&next.frames[i]))ok=false;
 if(ok&&ver<=2){ /* v1/v2: every non-empty surface pixel, then AX */
  for(unsigned i=0;i<NSURF&&ok;i++) {
   uint32_t wh[2];RD(wh,sizeof(wh));
   if(!wh[0]&&!wh[1])continue;
   if(wh[0]<640||wh[1]<480||wh[0]>4096||wh[1]>4096){ok=false;break;}
   nextsurf[i]=SDL_CreateRGBSurfaceWithFormat(0,wh[0],wh[1],32,SDL_PIXELFORMAT_RGBA32);if(!nextsurf[i]){ok=false;break;}
   SDL_SetSurfaceBlendMode(nextsurf[i],SDL_BLENDMODE_NONE);
   for(unsigned y=0;y<wh[1];y++)RD((uint8_t*)nextsurf[i]->pixels+y*nextsurf[i]->pitch,wh[0]*4);
  }
 }
 if(ok&&ver==3){ /* v3 (interim dev build): draw history + display frame; read
                  * and discard so old saves stay loadable, then surfaces are
                  * rebuilt by replaying the history below. */
  uint32_t hn=0; RD(&hn,4);
  struct img_hist_ent *h3=calloc(hn?hn:1,sizeof(*h3));
  if(!h3){ok=false;} else {
   for(unsigned i=0;i<hn&&i<IMG_HIST_MAX;i++){
    RD(h3[i].name,sizeof(h3[i].name)); RD(&h3[i].dst,2); RD(&h3[i].x,2); RD(&h3[i].y,2);
   }
   uint32_t dflag=0; RD(&dflag,4);
   if(dflag)for(int y=0;y<480&&ok;y++){uint8_t px[640*4];RD(px,sizeof(px));}
   if(hn>IMG_HIST_MAX)hn=IMG_HIST_MAX;
   for(unsigned i=0;i<hn&&i<IMG_HIST_MAX;i++){
    draw_image(h3[i].name,h3[i].dst,h3[i].x,h3[i].y); /* rebuild layers */
   }
   free(h3);
  }
 }
 if(ok&&ver==4){ /* v4: same as v1/v2 pixel layout */
  for(unsigned i=0;i<NSURF&&ok;i++) {
   uint32_t wh[2];RD(wh,sizeof(wh));
   if(!wh[0]&&!wh[1])continue;
   if(wh[0]<640||wh[1]<480||wh[0]>4096||wh[1]>4096){ok=false;break;}
   nextsurf[i]=SDL_CreateRGBSurfaceWithFormat(0,wh[0],wh[1],32,SDL_PIXELFORMAT_RGBA32);if(!nextsurf[i]){ok=false;break;}
   SDL_SetSurfaceBlendMode(nextsurf[i],SDL_BLENDMODE_NONE);
   for(unsigned y=0;y<wh[1];y++)RD((uint8_t*)nextsurf[i]->pixels+y*nextsurf[i]->pitch,wh[0]*4);
  }
 }
 if(ok&&ver>=2){RD(&nextanim,sizeof(nextanim)); if(!ax_valid(&nextanim))ok=false;}
 if(ok&&at!=raw_n)ok=false;
rd_done:
 free(raw);
 if(ok) {
  load_clear_fx();
  st=next;animation=nextanim;
  if(ver==1||ver==2||ver==4){
   for(unsigned i=0;i<NSURF;i++){if(surfaces[i])SDL_FreeSurface(surfaces[i]);surfaces[i]=nextsurf[i];nextsurf[i]=NULL;}
  }
  for(int i=0;i<5;i++)audio_stop(i);
  choice_open=false;error_text[0]=0;
 }
 for(unsigned i=0;i<NSURF;i++)if(nextsurf[i])SDL_FreeSurface(nextsurf[i]);
 char msg[64];snprintf(msg,sizeof(msg),ok?"Loaded slot %u":"Save damaged or incompatible (slot %u)",slot);
 notify_status(msg);note("LOADSAVE %s %s",ok?"OK":"FAIL",path);return ok;
}
/* ---- engine-menu grid navigation (layout rows per kind) ----
 * 41 sound test : 8 rows x 2 (14 tracks + 停止/タイトルに戻る)
 * 42 endings    : rows {4,5,5,5,1} (19 cards + 戻る)
 * 43 scene      : rows {5,3} (5 part plates + 前/次/戻る)
 * 44 album      : 5 rows x 4 (16 cells + 4 controls)
 * 2/37/38/45/46: single column. dx in-row, dy between rows, clamped. */
static unsigned nav_step(unsigned s,int dx,int dy){
 unsigned n=choice_count; if(!n)return s;
 unsigned kind=vm_choice_kind();
 if(save_ui_kind(kind)){
  if(slot_pending>=0){int t=(int)s+dx+dy;return t<0?0:t>=(int)n?n-1:(unsigned)t;}
  if(dx)return s<10?10:s>=10?0:s;
  int t=(int)s+dy;return t<0?0:t>=(int)n?n-1:(unsigned)t;
 }
 if(kind==2){
  unsigned base=(s/8)*8,local=s-base,npage=n-base;if(npage>8)npage=8;
  if(dx){int t=(int)local+dx*4;return t>=0&&t<(int)npage?base+(unsigned)t:s;}
  int t=(int)s+dy;return t<0?0:t>=(int)n?n-1:(unsigned)t;
 }
 if(kind==37||kind==38||kind==45||kind==46){
  if(dx)return s;
  int t=(int)s+dy; if(t<0)t=0; if(t>=(int)n)t=(int)n-1; return (unsigned)t;
 }
 unsigned rows,lens[8],i;
 switch(kind){
  case 41: rows=8; for(i=0;i<8;i++)lens[i]=2; break;
  case 42: rows=5; lens[0]=4;lens[1]=5;lens[2]=5;lens[3]=5;lens[4]=1; break;
  case 43: rows=2; lens[0]=5;lens[1]=3; break;
  case 44: rows=5; for(i=0;i<5;i++)lens[i]=4; break;
  default: return s;
 }
 unsigned base[8];base[0]=0;for(i=1;i<rows;i++)base[i]=base[i-1]+lens[i-1];
 unsigned total=base[rows-1]+lens[rows-1];
 if(s>=total)s=total?total-1:0;
 unsigned r=0; while(r+1<rows&&s>=base[r+1])r++;
 unsigned c=s-base[r];
 if(dy){
  int rr=(int)r+dy; if(rr<0)rr=0; if(rr>=(int)rows)rr=(int)rows-1;
  unsigned cc=c<lens[rr]?c:lens[rr]-1;
  return base[rr]+cc;
 }
 if(dx){
  int cc=(int)c+dx; if(cc<0)cc=0; if(cc>=(int)lens[r])cc=(int)lens[r]-1;
  return base[r]+(unsigned)cc;
 }
 return s;
}
/* L/R shoulder: page flip in album (44) / scene (43); elsewhere keep the
 * save/load menu shortcuts. */
static void shoulder_page(bool next){
 if(choice_open&&st.waiting==2&&save_ui_kind(vm_choice_kind())){
  if(slot_pending>=0)return;
  unsigned p=slot_page;if(next&&p<3)p++;else if(!next&&p>0)p--;
  for(unsigned i=0;i<choice_count;i++)if(vm_menu_value(i)==SLOT_PAGE+p){vm_choose(i);return;}
  return;
 }
 if(choice_open&&st.waiting==2){
  unsigned kind=vm_choice_kind();
  if(kind==44||kind==43){
   unsigned want=next?EXTRA_NEXT:EXTRA_PREV;
   for(unsigned i=0;i<choice_count;i++)if(vm_menu_value(i)==want){vm_choose(i);return;}
   return;
  }
 }
 if(next)vm_slot_menu(false);else vm_slot_menu(true);
}
unsigned vm_menu_nav(unsigned sel,int dx,int dy){
 unsigned n=choice_count; if(!n)return sel;
 unsigned kind=vm_choice_kind();
 unsigned cur=sel>=n?n-1:sel;
 unsigned first=nav_step(cur,dx,dy);
 /* engine extra menus: never rest the cursor on unactivated items */
 if((kind==41||kind==42||kind==43||kind==44)&&(dx||dy)){
  unsigned gridn=0;
  switch(kind){case 41:gridn=14;break;case 42:gridn=19;break;case 43:gridn=5;break;case 44:gridn=16;break;}
  if(gridn>n)gridn=n;
  unsigned probe=first;
  for(unsigned steps=0;steps<=n;steps++){
   if(extras_enabled(kind,vm_menu_value(probe)))return probe;
   unsigned nx=nav_step(probe,dx,dy);
   if(nx==probe)break;      /* edge of this axis */
   probe=nx;
  }
  /* no enabled item along that axis: jump to the nearest enabled item in the
   * same section (grid vs control-button row), scanning forward */
  unsigned lo=0,hi=n;
  if(first<gridn)hi=gridn; else lo=gridn;
  if(hi>choice_count)hi=choice_count;
  if(lo>=hi)return sel;
  unsigned from=first<hi?first:lo;
  /* keep scanning in the SAME direction (wrap inside the section) so a left
   * press never jumps right and vice versa */
  int sgn=dy? (dy>0?1:-1) : (dx>0?1:-1);
  unsigned span=hi-lo;
  for(unsigned k=1;k<span;k++){
   int off=((int)(from-lo)+sgn*(int)k)%(int)span;
   if(off<0)off+=(int)span;
   unsigned idx=lo+(unsigned)off;
   if(extras_enabled(kind,vm_menu_value(idx)))return idx;
  }
  return sel;
 }
 return first;
}
/* B in an engine menu = its 戻る/タイトル item, else direct close. */
void vm_menu_cancel(void){
 unsigned kind=vm_choice_kind(),want;
 if(save_ui_kind(kind)&&slot_pending>=0){
  for(unsigned i=0;i<choice_count;i++)if(vm_menu_value(i)==SLOT_NO){vm_choose(i);return;}
 }
 switch(kind){
  case 41: want=15; break;              /* タイトルに戻る */
  case 42: case 43: case 44: want=0; break; /* 戻る / タイトルへ */
  case 38: case 45: case 46: want=~0u; break;
  default: return;                      /* 2 / 37 have no cancel */
 }
 for(unsigned i=0;i<choice_count;i++)if(vm_menu_value(i)==want){vm_choose(i);return;}
 if(choice_open){choice_open=false;st.waiting=1;}
}
/* B while a CG album photo is showing (ALLPIC.MES util46 hold): leave the
 * photo back to the album menu; never chain to the next unlocked group. */
static void album_back(void){
 if(st.waiting==1&&st.ip.script&&!strcasecmp(st.ip.script,"ALLPIC.MES")){
  /* leave the CG photo: reopen the album menu (ALLPIC.MES @41 Util44) */
  st.text[0]=0;st.waiting=0;
  vm_jump_at("ALLPIC.MES",0x41);
  gfx_dirty=true;
  return;
 }
 hide_msg=!hide_msg;gfx_dirty=true;
}
static void confirm(void) {if(st.waiting==2&&vm_choice_kind()==37&&title_ui_active()){title_ui_skip();gfx_dirty=true;return;}if(st.waiting==3){extern void frontend_xfade_skip(void);extern void frontend_fadein_skip(void);frontend_xfade_skip();frontend_fadein_skip();ppf_poll(true);st.waiting=0;}else if(st.waiting==2){if(need_sel)return;note("SRC confirm kind=%u sel=%u",vm_choice_kind(),selected);vm_choose(selected);if(st.waiting!=2)choice_open=false;}else if(!hide_msg)vm_advance(); /* story must not advance while the text box is hidden */}
/* Share the rendering geometry with pointer input; outside taps are not confirms. */
static int choice_at(int x,int y) {
 if(!choice_open||st.waiting!=2)return -1;
 unsigned kind=vm_choice_kind();
 if(save_ui_kind(kind)){
  for(unsigned i=0;i<choice_count;i++){
   SDL_Rect r;unsigned v=vm_menu_value(i);
   if(save_ui_enabled(kind,v)&&save_ui_rect(kind,v,&r)&&x>=r.x&&x<r.x+r.w&&y>=r.y&&y<r.y+r.h)return (int)i;
  }
  return -1;
 }
 if(kind==41||kind==42||kind==43||kind==44){
  for(unsigned i=0;i<choice_count;i++){
   SDL_Rect r;unsigned value=vm_menu_value(i);
   if(extras_enabled(kind,value)&&(kind==43?scene_ui_rect(value,&r):extras_rect(kind,value,&r))&&x>=r.x&&x<r.x+r.w&&y>=r.y&&y<r.y+r.h)return (int)i;
  }
  return -1;
 }
 if(kind==37&&surfaces[7]&&surfaces[7]->h>=888) {
  for(unsigned i=0;i<choice_count;i++) {
   SDL_Rect source,r;
   if(title_ui_button(vm_menu_value(i),i,false,&source,&r)&&
      x>=r.x&&x<r.x+r.w&&y>=r.y&&y<r.y+r.h)return (int)i;
  }
  return -1;
 }
 unsigned base=selected>=8?selected-7:0,vis=choice_count-base;
 if(vis>8)vis=8;
 if(!vis)return -1;
 int lh=34,y0=104,left=120,right=560;
 if(kind==2) {
  unsigned start=(selected/8)*8,end=start+8;if(end>choice_count)end=choice_count;
  for(unsigned i=start;i<end;i++){
   SDL_Rect r=story_choice_rect(i);
   if(x>=r.x&&x<r.x+r.w&&y>=r.y&&y<r.y+r.h)return (int)i;
  }
  return -1;
 }
 if(x<left||x>=right||y<y0||y>=y0+(int)vis*lh)return -1;
 return (int)base+(y-y0)/lh;
}
static void pointer_confirm(int x,int y) {
 if(st.waiting==2&&vm_choice_kind()==37&&title_ui_active()){title_ui_skip();gfx_dirty=true;return;}
 if(st.waiting==2){int i=choice_at(x,y);if(i<0)return;need_sel=false;selected=(unsigned)i;}
 confirm();
}
int main(int argc,char **argv) {
 const char *fontpath=NULL;const char *start="STARTUP.MES";
#ifdef __SWITCH__
 /* Full-NSP: game data in the title RomFS, saves in HOS SaveData.  Homebrew
  * NRO falls back to /switch/KAWAXP on the SD card.  Must run before any
  * game-data or save file is opened. */
 snprintf(data_dir,sizeof(data_dir),"sdmc:/switch/KAWAXP");
 {extern void switch_hos_init(char*,size_t,char*,size_t);
  switch_hos_init(data_dir,sizeof(data_dir),save_dir,sizeof(save_dir));}
#else
 snprintf(data_dir,sizeof(data_dir),".");
#endif
 for(int i=1;i<argc;i++) {
  if(!strcmp(argv[i],"--smoke")&&i+1<argc){smoke=true;smoke_messages=atoi(argv[++i]);}
  else if(!strcmp(argv[i],"--screenshot")&&i+1<argc)screenshot=argv[++i];
  else if(!strcmp(argv[i],"--font")&&i+1<argc)fontpath=argv[++i];
  else if(!strcmp(argv[i],"--save-dir")&&i+1<argc)snprintf(save_dir,sizeof(save_dir),"%s",argv[++i]);
  else if(!strcmp(argv[i],"--start")&&i+1<argc)start=argv[++i];
  else snprintf(data_dir,sizeof(data_dir),"%s",argv[i]);
 }
 if(!save_dir[0])snprintf(save_dir,sizeof(save_dir),"%s/kawaxp-saves",data_dir);
#ifndef __SWITCH__
 if(mkdir(save_dir,0755)&&errno!=EEXIST){fprintf(stderr,"Cannot create save directory %s\n",save_dir);return 1;}
#else
 /* HOS SaveData root exists once mounted; SD fallback needs the directory. */
 {extern int switch_save_active;
  if(!switch_save_active&&mkdir(save_dir,0755)&&errno!=EEXIST){
   fprintf(stderr,"Cannot create save directory %s\n",save_dir);return 1;}}
#endif
 char fontbuf[1200];if(!fontpath){snprintf(fontbuf,sizeof(fontbuf),"%s/Kosugi-Regular.ttf",data_dir);fontpath=fontbuf;}
if(SDL_Init(SDL_INIT_VIDEO|SDL_INIT_AUDIO|SDL_INIT_GAMECONTROLLER|SDL_INIT_TIMER)||kawa_text_init()){fprintf(stderr,"SDL: %s\n",SDL_GetError());return 1;}
#ifdef __SWITCH__
 /* Switch SDL2 runs fullscreen at the console resolution (handheld 1280x720 /
  * docked 1080p); letterbox the 4:3 canvas via logical size (no distortion). */
 {SDL_DisplayMode dm={0};SDL_GetCurrentDisplayMode(0,&dm);
  int ww=dm.w?dm.w:1280,wh=dm.h?dm.h:720;
  window=SDL_CreateWindow("KAWAXP",0,0,ww,wh,SDL_WINDOW_FULLSCREEN_DESKTOP);}
#else
 {int ww=960,wh=720;const char*wws=getenv("KAWA_W");if(wws)ww=atoi(wws);
  const char*whs=getenv("KAWA_H");if(whs)wh=atoi(whs);
  window=SDL_CreateWindow("KAWAXP — development build",SDL_WINDOWPOS_CENTERED,SDL_WINDOWPOS_CENTERED,ww,wh,SDL_WINDOW_SHOWN|SDL_WINDOW_RESIZABLE);}
#endif
 renderer=SDL_CreateRenderer(window,-1,SDL_RENDERER_ACCELERATED|SDL_RENDERER_PRESENTVSYNC);
 if(!renderer)renderer=SDL_CreateRenderer(window,-1,SDL_RENDERER_SOFTWARE);
 if(!window||!renderer){fprintf(stderr,"Window: %s\n",SDL_GetError());return 1;}
  {SDL_RendererInfo ri;if(SDL_GetRendererInfo(renderer,&ri)==0)
    fprintf(stderr,"RENDERER %s flags=0x%x accel=%d soft=%d vsync=%d\n",ri.name,ri.flags,
      (ri.flags&SDL_RENDERER_ACCELERATED)!=0,(ri.flags&SDL_RENDERER_SOFTWARE)!=0,
      (ri.flags&SDL_RENDERER_PRESENTVSYNC)!=0);}
 if(getenv("KAWA_STRETCH")){SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY,"linear");SDL_RenderSetLogicalSize(renderer,1280,720);}
 else SDL_RenderSetLogicalSize(renderer,640,480);
#ifdef __SWITCH__
 /* Pin the VM/present thread away from the core(s) HOS schedules on. The user
  * observed the game sharing core 3 with system processes while 1/2 sat idle.
  * Default: prefer core 1, allow any (KAWA_CORE_MAIN=<n> to force one core). */
 {const char *e=getenv("KAWA_CORE_MAIN"); s32 pref = e?atoi(e):1; u64 mask=0xF;
  if(e&&pref>=0&&pref<=3)mask=1u<<pref;
  Result rc=svcSetThreadCoreMask(threadGetCurHandle(),pref,mask);
  s32 gotp=-1;u64 gotm=0;svcGetThreadCoreMask(&gotp,&gotm,threadGetCurHandle());
  fprintf(stderr,"CORE main pref=%d mask=0x%llx set_rc=0x%x cur=%u\n",gotp,(unsigned long long)gotm,(unsigned)rc,svcGetCurrentProcessorNumber());}
#endif
 canvas=SDL_CreateRGBSurfaceWithFormat(0,640,480,32,SDL_PIXELFORMAT_RGBA32);
 texture=SDL_CreateTexture(renderer,SDL_PIXELFORMAT_RGBA32,SDL_TEXTUREACCESS_STREAMING,640,480);
 font=kawa_font_open(fontpath,18);if(!font||!canvas||!texture){fprintf(stderr,"Font/render init failed\n");return 1;}
 char path[1200];snprintf(path,sizeof(path),"%s/mes.ARC",data_dir);ai5_set_game("kawarazakike");mes_arc=archive_open(path,ARCHIVE_RAW);
 snprintf(path,sizeof(path),"%s/gcc.ARC",data_dir);cg_arc=archive_open(path,ARCHIVE_RAW);
 snprintf(path,sizeof(path),"%s/sequence.ARC",data_dir);seq_arc=archive_open(path,ARCHIVE_RAW);
 if(!mes_arc||!cg_arc||!seq_arc){fprintf(stderr,"Missing game archives\n");return 1;}
 SDL_GameController *controller=NULL;for(int i=0;i<SDL_NumJoysticks();i++)if(SDL_IsGameController(i)){controller=SDL_GameControllerOpen(i);break;}
 audio_init();vm_start(start);Uint32 last=SDL_GetTicks();unsigned frames=0;
 /* Isolated smoke fixtures; never unlock anything in normal play. */
 if(smoke&&getenv("KAWA_TEST_EXTRAS")){
  for(unsigned i=130;i<=152;i++)st.flag[i]=1;
  for(unsigned i=200;i<=322;i++)st.flag[i]=1;
  for(unsigned i=1510;i<1524;i++)st.flag[i]=1;
  for(unsigned i=401;i<=440;i++)st.flag[i]=1; /* scene replay parts */
 }
 frame_log = getenv("KAWA_FRAMELOG")!=NULL;
 const char *km=getenv("KAWA_MENU37");if(!km)km=getenv("KAWA_MENU");unsigned kawa37=km?strtoul(km,0,10):0;
 const char *km2=getenv("KAWA_MENU38");unsigned kawa38=km2?strtoul(km2,0,10):0;
 unsigned kawa41=0,kawa42=0,kawa43=0,kawa44=0;
 unsigned route[32]; unsigned rlen=0, rpos=0;
 unsigned kawa45=0,kawa46=0;
 /* KAWA_BRANCH="S7:1,S11:1,...": pick option i for the next kind2 menu in that script */
 struct { char script[32]; unsigned long addr; unsigned opt; int have_addr; } brk[64]; unsigned nbrk=0;
 {const char*bv=getenv("KAWA_BRANCH"); if(bv){const char*q=bv;
   while(nbrk<64&&*q){char sc[32]={0};unsigned o=0;unsigned long ad=0;int ha=0;
    int nn=sscanf(q,"%31[^:]:%lx:%u",sc,&ad,&o);
    if(nn==2){sscanf(q,"%31[^:]:%u",sc,&o);ha=0;}
    else if(nn==3){ha=1;}
    if(sc[0]){snprintf(brk[nbrk].script,32,"%s",sc);brk[nbrk].addr=ad;brk[nbrk].opt=o;brk[nbrk].have_addr=ha;nbrk++;}
    q=strchr(q,','); if(!q)break; q++;}}}
 static unsigned brk_used[64];
 {const char *e=getenv("KAWA_MENU45");if(e)kawa45=strtoul(e,0,10);
  e=getenv("KAWA_MENU46");if(e)kawa46=strtoul(e,0,10);}
 {const char *rt=getenv("KAWA_ROUTE");
  if(rt){const char *q=rt; while(rlen<32&&*q){while(*q==','||*q==' ')q++;if(!*q)break;route[rlen++]=strtoul(q,(char**)&q,10);}}}
 {const char *e;unsigned *t[]={&kawa41,&kawa42,&kawa43,&kawa44};const char *kn[]={"KAWA_MENU41","KAWA_MENU42","KAWA_MENU43","KAWA_MENU44"};
  for(int z=0;z<4;z++){e=getenv(kn[z]);if(e)*t[z]=strtoul(e,0,10);}}
 const char *kf=getenv("KAWA_FRAMES");unsigned frames_cap=kf?strtoul(kf,0,10):50000;
 unsigned autosave_at=0;
 {const char *asv=getenv("KAWA_AUTOSAVE"); if(asv){unsigned long a=0,b=3;sscanf(asv,"%lu:%lu",&a,&b);autosave_at=(unsigned)a;kawa45=(unsigned)b;}}
 unsigned hold_ticks=0;const char *hold=getenv("KAWA_SMOKE_HOLD");
 if(hold)hold_ticks=strtoul(hold,NULL,10);
 ax_reset(&animation);Uint32 anim_last=SDL_GetTicks();
 const char *scene_keys=smoke?getenv("KAWA_SCENE_KEYS"):NULL;
 unsigned scene_key_pos=0;
 while(running) {
#ifdef __SWITCH__
  if(!appletMainLoop())break;
#endif
  if(scene_keys&&st.waiting==2&&vm_choice_kind()==43&&scene_keys[scene_key_pos]){
   char key=scene_keys[scene_key_pos++];SDL_Event injected={0};
   if(key=='L'||key=='R'){
    injected.type=SDL_CONTROLLERBUTTONDOWN;injected.cbutton.state=SDL_PRESSED;
    injected.cbutton.button=key=='L'?SDL_CONTROLLER_BUTTON_LEFTSHOULDER:SDL_CONTROLLER_BUTTON_RIGHTSHOULDER;
    SDL_PushEvent(&injected);
   } /* '.' holds one frame, allowing the original hover animation to run */
  }
  SDL_Event e;while(SDL_PollEvent(&e)) {
   if(save_ui_event(&e))continue;
   if(e.type==SDL_QUIT)running=false;
   if(e.type==SDL_KEYDOWN&&!e.key.repeat) {
    switch(e.key.keysym.sym) {
    case SDLK_ESCAPE:running=false;break;
    case SDLK_RETURN:case SDLK_SPACE:
     confirm();break;
    case SDLK_UP:menu_move(0,-1);break;
    case SDLK_DOWN:menu_move(0,1);break;
    case SDLK_LEFT:menu_move(-1,0);break;
    case SDLK_RIGHT:menu_move(1,0);break;
    case SDLK_F5:vm_slot_menu(true);break;case SDLK_F9:vm_slot_menu(false);break;
    case SDLK_x:ff_hold=true;gfx_dirty=true;break;
    case SDLK_b:
     if(choice_open&&st.waiting==2&&vm_choice_kind()!=2&&vm_choice_kind()!=37)vm_menu_cancel();
     else album_back();
     break;
    }
   }
   if(e.type==SDL_KEYUP&&(e.key.keysym.sym==SDLK_x)){ff_hold=false;gfx_dirty=true;}
   if(e.type==SDL_CONTROLLERBUTTONDOWN){
    /* SDL reports face buttons in Xbox positions (A=bottom,B=right,X=left,Y=top);
     * Switch labels rotate them (A=right,B=bottom,X=top,Y=left), so swap there so
     * the printed A confirms, printed B hides text, printed X fast-forwards. */
    unsigned fb=e.cbutton.button;
#ifdef __SWITCH__
    switch(fb){
     case SDL_CONTROLLER_BUTTON_B:confirm();break;      /* printed A */
     case SDL_CONTROLLER_BUTTON_A: /* printed B */
      if(choice_open&&st.waiting==2&&vm_choice_kind()!=2&&vm_choice_kind()!=37)vm_menu_cancel();
      else album_back();
      break;
     case SDL_CONTROLLER_BUTTON_Y:ff_hold=true;break;   /* printed X */
    }
#else
    switch(fb){
     case SDL_CONTROLLER_BUTTON_A:confirm();break;
     case SDL_CONTROLLER_BUTTON_B:
      if(choice_open&&st.waiting==2&&vm_choice_kind()!=2&&vm_choice_kind()!=37)vm_menu_cancel();
      else album_back();
      break;
     case SDL_CONTROLLER_BUTTON_X:ff_hold=true;gfx_dirty=true;break;
    }
#endif
    switch(fb){
     case SDL_CONTROLLER_BUTTON_START:running=false;break;
     case SDL_CONTROLLER_BUTTON_DPAD_UP:menu_move(0,-1);break;
     case SDL_CONTROLLER_BUTTON_DPAD_DOWN:menu_move(0,1);break;
     case SDL_CONTROLLER_BUTTON_DPAD_LEFT:menu_move(-1,0);break;
     case SDL_CONTROLLER_BUTTON_DPAD_RIGHT:menu_move(1,0);break;
     case SDL_CONTROLLER_BUTTON_LEFTSHOULDER:shoulder_page(false);break;
     case SDL_CONTROLLER_BUTTON_RIGHTSHOULDER:shoulder_page(true);break;
    }
   }
   if(e.type==SDL_CONTROLLERBUTTONUP){
    unsigned fb=e.cbutton.button;
#ifdef __SWITCH__
    if(fb==SDL_CONTROLLER_BUTTON_Y){ff_hold=false;gfx_dirty=true;}
#else
    if(fb==SDL_CONTROLLER_BUTTON_X){ff_hold=false;gfx_dirty=true;}
#endif
   }
   if(e.type==SDL_MOUSEMOTION&&e.motion.which!=SDL_TOUCH_MOUSEID&&vm_choice_kind()!=2) {
    int i=choice_at(e.motion.x,e.motion.y);if(i>=0){selected=(unsigned)i;if(vm_choice_kind()==37&&!title_ui_active())need_sel=false;}
   }
   if(e.type==SDL_MOUSEBUTTONDOWN&&e.button.button==SDL_BUTTON_LEFT&&e.button.which!=SDL_TOUCH_MOUSEID) {
    pointer_confirm(e.button.x,e.button.y);
   }
   if(e.type==SDL_FINGERDOWN) { /* Switch touch: normalized 0..1 -> 640x480 logical */
    int tx=(int)(e.tfinger.x*640.0f), ty=(int)(e.tfinger.y*480.0f);
    if(tx>=0&&tx<640&&ty>=0&&ty<480)pointer_confirm(tx,ty);
    gfx_dirty=true;
   }
  }
  Uint32 anim_now=SDL_GetTicks();
  if(st.waiting==2&&vm_choice_kind()==37&&title_ui_update(anim_now-anim_last))gfx_dirty=true;
  if(st.waiting==2&&vm_choice_kind()==43){
   extern unsigned scene_page;
   if(scene_ui_update(scene_page,vm_menu_value(selected),smoke?AX_TICK_MS:anim_now-anim_last))gfx_dirty=true;
  }else animation_update(smoke?AX_TICK_MS:anim_now-anim_last);
  anim_last=anim_now;
  {extern void frontend_xfade_poll(void);frontend_xfade_poll();}
  {extern void frontend_fadein_poll(void);frontend_fadein_poll();}
  ppf_poll(ff_hold||getenv("KAWA_FF"));quake_poll();
  /* timed waits end as usual; a util35 wipe keeps waiting until its frames ran */
  if(st.waiting==3&&!frontend_xfade_active()&&!frontend_fadein_active()&&!frontend_ppf_active()&&!hide_msg&&(smoke||(Sint32)(SDL_GetTicks()-st.sys[255])>=0))st.waiting=0;
  if((ff_hold||getenv("KAWA_FF"))&&!smoke&&!hide_msg) { /* fast-forward paused while text box is hidden */
   if(st.waiting==3){if(!getenv("KAWA_FFNOSKIP")){extern void frontend_xfade_skip(void);frontend_xfade_skip();frontend_fadein_skip();ppf_poll(true);}if(!frontend_xfade_active()&&!frontend_fadein_active()&&!frontend_ppf_active())st.waiting=0;}
   else if(st.waiting==1)vm_advance();
  }
  if(smoke&&st.waiting==2){
   if(scene_keys&&vm_choice_kind()==43&&scene_keys[scene_key_pos]){frontend_present();continue;}
   {const char *kind=getenv("KAWA_ENGINESHOT");
    if(kind&&vm_choice_kind()==strtoul(kind,NULL,10)){
     static bool acted=false;
     const char *actions=getenv("KAWA_EXTRA_ACTIONS");
     if(actions&&!acted){
      acted=true;const char *p=actions;
      while(*p&&st.waiting==2){char *end;unsigned value=strtoul(p,&end,10);if(end==p)break;
       for(unsigned i=0;i<choice_count;i++)if(vm_menu_value(i)==value){selected=i;vm_choose(i);break;}
       p=*end==','?end+1:end;
      }
      if(st.waiting!=2)continue;
     }
     frontend_present();if(screenshot){SDL_SaveBMP(canvas,screenshot);screenshot=NULL;}
     break;}}
   {static unsigned msn=0;const char*ms=getenv("KAWA_MENUSHOT");
    if(ms&&vm_choice_kind()==2&&++msn>=strtoul(ms,0,10)){
     frontend_present();if(screenshot){SDL_SaveBMP(canvas,screenshot);screenshot=NULL;}
     break;}}
   {note("SRC autopick kind=%u",vm_choice_kind());unsigned kk=vm_choice_kind();unsigned want=0;bool matched=false;
 if(kk==2){for(unsigned bi=0;bi<nbrk;bi++){if(!brk_used[bi]){const char*a=st.ip.script,*b=brk[bi].script;
   size_t la=strcspn(a,"."),lb=strcspn(b,".");
   if(la==lb&&!strncasecmp(a,b,la)&&(!brk[bi].have_addr||brk[bi].addr==(unsigned long)vm_last_menu_addr())){brk_used[bi]=1;want=brk[bi].opt;matched=true;break;}}}
  if(!matched&&rpos<rlen)want=route[rpos++];}
 else want=(kk==37)?kawa37:(kk==38)?kawa38:(kk==41)?kawa41:(kk==42)?kawa42:(kk==43)?kawa43:(kk==44)?kawa44:(kk==45)?kawa45:(kk==46)?kawa46:0;unsigned pick=choice_count?(want<choice_count?want:choice_count-1):0;vm_choose(pick);if(st.waiting!=2)choice_open=false;}
  }
  if(smoke&&st.waiting==1) {
   if(st.messages>=(unsigned)smoke_messages) {if(!hold_ticks)break;hold_ticks--;}
   else {
    {static bool as_done=false; if(!as_done&&autosave_at&&st.messages>=autosave_at){as_done=true;note("AUTOSAVE open slot menu");vm_slot_menu(true);}}
    vm_advance();
   }
  }
  for(unsigned i=0;i<300&&!st.waiting;i++)vm_step();
  {Uint32 t0=SDL_GetTicks();frontend_present();Uint32 dt=SDL_GetTicks()-t0;
   frames++;fr_cnt++;fr_acc+=dt;if(dt>fr_max)fr_max=dt;
   if(frame_log) { /* periodic FRAME stats; off by default, KAWA_FRAMELOG=1 re-enables */
    if(fr_logt==0)fr_logt=SDL_GetTicks();
    if(SDL_GetTicks()-fr_logt>=2000&&fr_cnt){
     fprintf(stderr,"FRAME avg=%.2fms max=%ums over %u frames\n",(double)fr_acc/fr_cnt,(unsigned)fr_max,fr_cnt);
     fr_cnt=0;fr_acc=0;fr_max=0;fr_logt=SDL_GetTicks();}
   }
  }
  if((smoke&&(st.waiting==99||st.waiting==98||frames>frames_cap))||(!smoke&&getenv("KAWA_FRAMES")&&frames>frames_cap))break;
  if(smoke)SDL_Delay(1);else if(SDL_GetTicks()-last<16)SDL_Delay(16-(SDL_GetTicks()-last));last=SDL_GetTicks();
 }
 frontend_present();if(screenshot)SDL_SaveBMP(canvas,screenshot);
 if(smoke&&st.waiting==1) {
  struct state old=st;struct ax_player oldanim=animation;
  if(!save_state(99)){fail("Smoke save failed");}
  else {st.var[1]^=123;ax_reset(&animation);if(!load_state(99)||memcmp(&st,&old,sizeof(st))||memcmp(&animation,&oldanim,sizeof(animation)))fail("Smoke save round trip mismatch");else note("SAVE ROUNDTRIP PASS (VM + AX)");}
 }
 note("RESULT messages=%u unsupported=%u waiting=%u script=%s addr=%x",st.messages,unsupported,st.waiting,st.ip.script,st.ip.addr);
 title_ui_close();save_ui_close();scene_ui_close();extras_close();audio_fini();if(controller)SDL_GameControllerClose(controller);kawa_font_close(font);SDL_DestroyTexture(texture);SDL_FreeSurface(canvas);
 for(unsigned i=0;i<NSURF;i++)if(surfaces[i])SDL_FreeSurface(surfaces[i]);
 SDL_DestroyRenderer(renderer);SDL_DestroyWindow(window);kawa_text_fini();SDL_Quit();return st.waiting==99?1:0;
}
