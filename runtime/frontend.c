/* GPL-2.0-or-later */
#include "kawa.h"
#include "ai5/cg.h"
#include "ai5/game.h"
#include "text.h"
#include "ax_render.h"
#include "title_ui.h"
#include "extras_ui.h"
#include <unistd.h>
#include <sys/stat.h>
#include <zlib.h>
#include <errno.h>
#include <stdlib.h>
#include <limits.h>
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
static bool flag_on(const char*n){char p[1200];snprintf(p,sizeof(p),"%s/%s",data_dir,n);FILE*f=fopen(p,"rb");if(!f)return false;fclose(f);return true;}
static int dbg_noax,dbg_notext;
int dbg_noaudio,dbg_audiosync; /* bisect: see audio.c */
static bool hide_msg;
static bool gfx_dirty=true;
void frontend_refresh(void){gfx_dirty=true;}
static unsigned fr_cnt;static Uint32 fr_acc,fr_max,fr_logt; /* rebuild/upload canvas only when the frame actually changed */
static bool frame_log; /* KAWA_FRAMELOG=1: periodic FRAME stats to the log (off by default) */
static char last_msg[1024];static int last_wait=-1,last_sel=-1;static bool last_cho=false,last_hide=false,last_ff=false,last_status=false; /* hide message window/text: B (keyboard b) toggles */
struct ax_player animation;
/* Util 34 screen quake: level>0 enables a subtle tremor until quake(0). */
static int quake_level;static Uint32 quake_start;
static SDL_Surface *quake_tile; /* 1280x960 gray tile built once per quake start */
static SDL_Surface *quake_build_tile(SDL_Surface *src){
 if(!src)return NULL;
 SDL_Surface*t=SDL_CreateRGBSurfaceWithFormat(0,1280,960,32,SDL_PIXELFORMAT_RGBA32);
 if(!t)return NULL;
 SDL_SetSurfaceBlendMode(t,SDL_BLENDMODE_NONE);
 /* grayscale the source into the top-left 640x480 */
 {unsigned char*sp=(unsigned char*)src->pixels; unsigned char*tp=(unsigned char*)t->pixels;
  int sw=src->w<640?src->w:640, sh=src->h<480?src->h:480;
  for(int y=0;y<sh;y++){unsigned char*sr=sp+y*src->pitch; unsigned char*tr=tp+y*t->pitch;
   for(int x=0;x<sw;x++){unsigned char*px=sr+x*4; unsigned g=(unsigned char)((px[0]*3+px[1]*6+px[2])/10);
    unsigned char*d=tr+x*4; d[0]=g;d[1]=g;d[2]=g;d[3]=255;}}}
 /* tile it 2x2 for seamless slow scroll */
 SDL_Rect d1={0,480,640,480},d2={640,0,640,480},d3={640,480,640,480};
 SDL_Rect srcr={0,0,640,480};
 SDL_BlitSurface(t,&srcr,t,&d1); SDL_BlitSurface(t,&srcr,t,&d2); SDL_BlitSurface(t,&srcr,t,&d3);
 return t;
}
void frontend_quake(unsigned level) {
 if(level&&!quake_tile){SDL_Surface*src=surfaces[1]?surfaces[1]:(surfaces[0]?surfaces[0]:NULL);
  if(quake_tile)SDL_FreeSurface(quake_tile); quake_tile=quake_build_tile(src);}
 if(!level&&quake_tile){SDL_FreeSurface(quake_tile);quake_tile=NULL;}
 quake_level=(int)level; if(level) quake_start=SDL_GetTicks(); if(getenv("KAWA_QUAKE")&&!level)quake_level=1;
}
/* Util 35 transition masks (%02u.msk, 640x480 raw 8-bit): validated here; the real
 * timed wipe (old page -> staged surface 1 with per-pixel mask reveal) is pending a
 * page-model cross-check against the original, see work/msk-note.md. */
/* Util35 transition masks (%02u.msk = 640x480 raw 8-bit per-pixel "reveal time"). */
static uint8_t *msk_data[16];
static const uint8_t *msk_get(unsigned idx) {
 if(idx>=16)return NULL;
 if(!msk_data[idx]) {
  char name[16];snprintf(name,sizeof(name),"%02u.msk",idx);
  struct archive_data *d=seq_arc?archive_get(seq_arc,name):NULL;
  if(d&&d->size>=307200){msk_data[idx]=(uint8_t*)malloc(307200);memcpy(msk_data[idx],d->data,307200);}
  if(d)archive_data_release(d);
  if(getenv("KAWA_MSKTRACE"))note("MSK %s %s",name,msk_data[idx]?"loaded":"missing");
 }
 return msk_data[idx];
}
void frontend_msk_note(unsigned idx){(void)msk_get(idx);}

/* util35 = masked scene transition (AI.exe 0x43a0b5): reveal the new frame staged on
 * surface 1 over the CURRENT display (surface 0) using the mask's per-pixel reveal
 * thresholds, phased over ~12 steps so the old picture is still visible while it is
 * being covered (not an instant black-out / opaque replace). In smoke it completes
 * instantly. */
static struct { bool on; unsigned idx; SDL_Surface *from; unsigned step; unsigned steps; Uint32 next; } xf;
bool frontend_xfade_start(unsigned idx) {
 if(smoke||!surfaces[0]||!surfaces[1]){
  if(surfaces[0]&&surfaces[1]){SDL_Rect r={0,0,640,480};SDL_BlitSurface(surfaces[1],&r,surfaces[0],&r);}
  return false;
 }
 if(!msk_get(idx)){SDL_Rect r={0,0,640,480};SDL_BlitSurface(surfaces[1],&r,surfaces[0],&r);return false;}
 /* If the staged frame is pure black (kuro fade templates), do NOT black out the
  * display: keep the previous picture on screen (quake then trembles that picture).
  * AI.exe page semantics are still unverified; Codex to confirm on device. */
 if(surfaces[1]&&surfaces[0]){
  unsigned char*b=(unsigned char*)surfaces[1]->pixels;unsigned long k=0;
  for(int yy=0;yy<480;yy+=4){unsigned char*r=b+yy*surfaces[1]->pitch;for(int xx=0;xx<640;xx+=4)k+=r[xx*4];}
  if(k<2000)return false; /* staged frame is ~pure black: keep old display */
 }
 if(!xf.on){
  xf.from=SDL_CreateRGBSurfaceWithFormat(0,640,480,32,SDL_PIXELFORMAT_RGBA32);
  if(!xf.from){SDL_Rect r={0,0,640,480};SDL_BlitSurface(surfaces[1],&r,surfaces[0],&r);return false;}
  SDL_SetSurfaceBlendMode(xf.from,SDL_BLENDMODE_NONE);
  {SDL_Rect r={0,0,640,480};SDL_BlitSurface(surfaces[0],&r,xf.from,&r);}
  xf.on=true;xf.idx=idx;xf.step=0;xf.steps=12;xf.next=SDL_GetTicks()+20;
 }
 return true;
}
void frontend_xfade_poll(void) {
 if(!xf.on)return;
 if((Sint32)(SDL_GetTicks()-xf.next)<0)return;
 xf.next=SDL_GetTicks()+20;
 xf.step++;
 SDL_Surface*dst=surfaces[0],*to=surfaces[1];
 const uint8_t*m=xf.idx<16?msk_data[xf.idx]:NULL;
 if(!m||!dst||!to||!xf.from){xf.on=false;if(xf.from)SDL_FreeSurface(xf.from);xf.from=NULL;return;}
 unsigned th=(xf.step*255)/xf.steps;
 unsigned char*dp=(unsigned char*)dst->pixels,*fp=(unsigned char*)xf.from->pixels,*tp=(unsigned char*)to->pixels;
 int dpitch=dst->pitch,fpitch=xf.from->pitch,tpitch=to->pitch;
 for(int y=0;y<480;y++){
  const unsigned char*mrow=m+y*640;
  unsigned char*drow=dp+y*dpitch,*frow=fp+y*fpitch,*trow=tp+y*tpitch;
  for(int x=0;x<640;x++){
   if(mrow[x]<th)memcpy(drow+x*4,trow+x*4,4);
   else memcpy(drow+x*4,frow+x*4,4);
  }
 }
 if(xf.step>=xf.steps){xf.on=false;SDL_FreeSurface(xf.from);xf.from=NULL;}
}
void frontend_xfade_skip(void) { /* finish instantly on click/ff */
 if(xf.on){
  if(surfaces[0]&&surfaces[1]){SDL_Rect r={0,0,640,480};SDL_BlitSurface(surfaces[1],&r,surfaces[0],&r);}
  xf.on=false;if(xf.from)SDL_FreeSurface(xf.from);xf.from=NULL;
 }
}

/* util34 = quake: per the user's PC observation these moments show a full gray
 * textured frame (the grayscale test@ pattern scripts stage on surface 1) drifting
 * slowly -- 0% of the previous picture. Render that: sample surface 1 (fall back to
 * surface 0) as a slowly scrolling texture across the whole canvas. */
static void quake_render(void) {
 SDL_Surface *src=surfaces[1]?surfaces[1]:(surfaces[0]?surfaces[0]:NULL);
 if(!src)return;
 if(!quake_tile)quake_tile=quake_build_tile(src);
 if(!quake_tile)return;
 Uint32 t=SDL_GetTicks()-quake_start;
 int ox=(int)(t/90)%640;   /* slow horizontal drift */
 int oy=(int)(t/260)%480;  /* slower vertical drift */
 /* single blit from the cached 2x2 gray tile (offset stays small vs 1280x960) */
 SDL_Rect fr={ox,oy,640,480};
 SDL_BlitSurface(quake_tile,&fr,canvas,NULL);
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
 /* Bound catch-up after OS suspension; never spin through a long backlog. */
 if(elapsed_ms>200)elapsed_ms=200;
 animation.phase_ms+=elapsed_ms;
 while(animation.phase_ms>=AX_TICK_MS) {
  animation.phase_ms-=AX_TICK_MS;
  if(!dbg_noax)if(!ax_tick(&animation,animation_draw,NULL)){fail("Malformed AX program in %s",animation.name);break;}
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
  note("IMAGE %s dst=%u %dx%d at %d,%d",real,dst,c->metrics.w,c->metrics.h,x,y);
 }
 cg_free(c);
}
void frontend_text(const char *text) {
 size_t used=strlen(st.text),n=strlen(text);
 if(used+n>=sizeof(st.text)){fail("Message buffer overflow");return;}
 memcpy(st.text+used,text,n+1);
}
void set_choices(const char **items,unsigned count,int kind) {
 (void)kind;choice_count=count>100?100:count;selected=0;choice_open=true;
 for(unsigned i=0;i<choice_count;i++)snprintf(labels[i],sizeof(labels[i]),"%s",items[i]);
}
static SDL_Surface *msg_cache;
static char msg_key[1024];
static uint32_t msg_col;
/* ---- text backlog (message history) ---- */
#define HIST_MAX 256
#define HIST_LEN 1024
static char history[HIST_MAX][HIST_LEN];
static unsigned hist_count, hist_head; /* count written; head = next write slot */
static bool hist_mode;                 /* full-screen history view */
static int hist_scroll;                /* 0 = newest at bottom */
static bool hist_avail(void){return hist_count>0;}
static void hist_add(const char *s){
 if(!s||!s[0])return;
 /* skip pure repeats of the last line (engine re-commits same text) */
 if(hist_count && !strcmp(history[(hist_head+HIST_MAX-1)%HIST_MAX], s))return;
 snprintf(history[hist_head],HIST_LEN,"%s",s);
 hist_head=(hist_head+1)%HIST_MAX;
 if(hist_count<HIST_MAX)hist_count++;
 if(hist_scroll)hist_scroll++; /* keep view pinned to bottom while open */
}
static const char *hist_line(int idx){ /* 0 = oldest .. count-1 = newest */
 return history[(hist_head+HIST_MAX-hist_count+idx)%HIST_MAX];
}
static void hist_enter(void){if(hist_avail()){hist_mode=true;hist_scroll=0;gfx_dirty=true;}}
static void hist_leave(void){hist_mode=false;gfx_dirty=true;}
void frontend_hist_note(void){ if(st.text[0])hist_add(st.text); } /* commit-time hook */
static SDL_Surface *msg_surface(int *w,int *h){
 if(msg_cache&&!strcmp(msg_key,st.text)&&msg_col==st.color){*w=600;*h=msg_cache->h;return msg_cache;}
 SDL_Surface*s0=kawa_text_render(font,st.text,600,0x000000);
 SDL_Surface*s1=kawa_text_render(font,st.text,600,st.color);
 int h1=s0?s0->h:0,h2=s1?s1->h:0;int hh=h1>h2?h1:h2;
 if(hh<=0){if(s0)SDL_FreeSurface(s0);if(s1)SDL_FreeSurface(s1);return NULL;}
 SDL_Surface*c=SDL_CreateRGBSurfaceWithFormat(0,600,hh+2,32,SDL_PIXELFORMAT_RGBA32);
 if(!c){if(s0)SDL_FreeSurface(s0);if(s1)SDL_FreeSurface(s1);return NULL;}
 SDL_SetSurfaceBlendMode(c,SDL_BLENDMODE_BLEND);
 if(s0){SDL_Rect d={1,1,0,0};SDL_BlitSurface(s0,NULL,c,&d);SDL_FreeSurface(s0);}
 if(s1){SDL_Rect d={0,0,0,0};SDL_BlitSurface(s1,NULL,c,&d);SDL_FreeSurface(s1);}
 if(msg_cache)SDL_FreeSurface(msg_cache);
 msg_cache=c;snprintf(msg_key,sizeof(msg_key),"%s",st.text);msg_col=st.color;
 *w=600;*h=hh+2;
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
  if(last_msg[0]&&!choice_open)hist_add(last_msg); /* the line we just finished showing */
  snprintf(last_msg,sizeof(last_msg),"%s",st.text);gfx_dirty=true;}
 if(last_wait!=st.waiting){last_wait=st.waiting;gfx_dirty=true;}
 if(last_sel!=selected){last_sel=selected;gfx_dirty=true;}
 if(last_cho!=choice_open){last_cho=choice_open;gfx_dirty=true;}
 if(last_hide!=hide_msg){last_hide=hide_msg;gfx_dirty=true;}
 if(last_ff!=ff_hold){last_ff=ff_hold;gfx_dirty=true;}
 {bool st_now=status_until>SDL_GetTicks(); if(st_now!=last_status){last_status=st_now;gfx_dirty=true;} if(st_now)gfx_dirty=true;}
 if(quake_level&&(SDL_GetTicks()-quake_start)<3000)gfx_dirty=true; /* moving */
 if(!gfx_dirty){ /* nothing changed: reuse last uploaded texture */
  SDL_RenderClear(renderer);SDL_RenderCopy(renderer,texture,NULL,NULL);SDL_RenderPresent(renderer);
  return;
 }
 gfx_dirty=false;
 SDL_FillRect(canvas,NULL,rgb(canvas,0)); if(quake_level&&(unsigned)(SDL_GetTicks()-quake_start)<3000)quake_render();
 else if(surfaces[0]){SDL_Rect r={0,0,640,480};SDL_BlitSurface(surfaces[0],&r,canvas,NULL);}
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
   unsigned start=selected>=8?selected-7:0,visible=choice_count-start;if(visible>8)visible=8;
   int lh=visible? (104-8)/ (int)visible : 26; if(lh>26)lh=26; if(lh<14)lh=14;
   int y0=376+(104-(int)visible*lh)/2;
   for(unsigned j=0;j<visible;j++){
    int y=y0+(int)j*lh;
    bool sel=(start+j==selected);
    text_at(labels[start+j],28,y+2,sel?0xffe9a0:0xffffff,584);
   }
  } else {
   int tw=600,th=0;SDL_Surface*ms=dbg_notext?NULL:msg_surface(&tw,&th);
   if(ms){SDL_Rect to={28,392,0,0};SDL_BlitSurface(ms,NULL,canvas,&to);}
  }
 }
 if(engine_menu) {
  if(vm_choice_kind()==37&&surfaces[7]&&surfaces[7]->h>=888) {
   unsigned results[6];unsigned count=choice_count<6?choice_count:6;
   for(unsigned i=0;i<count;i++)results[i]=vm_menu_value(i);
   title_ui_draw(surfaces[7],canvas,results,count,selected);
  } else if(vm_choice_kind()==41||vm_choice_kind()==42||vm_choice_kind()==44){
   extras_draw(canvas,vm_choice_kind(),vm_menu_value(selected));
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
 if(ff_hold&&!smoke&&(st.waiting==1||st.waiting==3)){SDL_Rect r={572,4,64,20};SDL_FillRect(canvas,&r,rgb(canvas,0x10241c));text_at(">>",578,5,0x7dffa0,50);}
 if(hist_mode) { /* ---- full-screen text history ---- */
  SDL_FillRect(canvas,NULL,rgb(canvas,0x000000));
  SDL_Rect tbar={0,0,640,28};SDL_FillRect(canvas,&tbar,rgb(canvas,0x1a2033));
  text_at("文字履歴 — 戻る:B / ↓:新しい  ↑:古い",10,5,0x9fc4ff,620);
  unsigned total=hist_count;
  int newest=(int)total-1-hist_scroll;      /* index of the newest visible line */
  if(newest<0)newest=0;
  /* each entry may wrap to several rendered rows, so page by actual measured
   * pixel height instead of a fixed stride (long lines were overlapping) */
  int avail=480-28-22; /* under the title bar, above the bottom bar */
  int first=newest, used=0;
  while(first>0){
   SDL_Surface*m=kawa_text_render(font,hist_line(first-1),616,0xdddddd);
   int h=m?m->h:24; if(m)SDL_FreeSurface(m);
   if(used+h>avail)break;
   used+=h+4; first--;
  }
  int y=30;
  for(int idx=first;idx<=newest;idx++){
   SDL_Surface*m=kawa_text_render(font,hist_line(idx),616,0xdddddd);
   if(m){SDL_Rect d={12,y,0,0};SDL_BlitSurface(m,NULL,canvas,&d);
    y+=m->h+4; SDL_FreeSurface(m);}
  }
  SDL_Rect hbar={0,478-14,640,14};SDL_FillRect(canvas,&hbar,rgb(canvas,0x1a2033));
  {char b[64];snprintf(b,sizeof(b),"%d/%u",newest+1,total);
   text_at(b,12,479-12,0x88aadd,600);}
 }
SDL_UpdateTexture(texture,NULL,canvas->pixels,canvas->pitch);
 SDL_RenderClear(renderer);SDL_RenderCopy(renderer,texture,NULL,NULL);SDL_RenderPresent(renderer);
}
static void notify_status(const char *s) {snprintf(status,sizeof(status),"%s",s);status_until=SDL_GetTicks()+2500;}
static bool valid_loc(struct loc *p) {return memchr(p->script,0,32)&&p->addr<16*1024*1024;}
int save_state(unsigned slot) {
 if(st.waiting!=1){notify_status("Save is available at a dialogue pause.");return 0;}
 char path[1200],tmp[1200];snprintf(path,sizeof(path),"%s/slot%u.kws",save_dir,slot);snprintf(tmp,sizeof(tmp),"%s/slot%u.tmp",save_dir,slot);
 gzFile f=gzopen(tmp,"wb3");if(!f){notify_status("Cannot open save file");return 0;}
 const uint32_t hdr[]={0x3153574b,2,sizeof(st),NSURF};bool ok=gzwrite(f,hdr,sizeof(hdr))==sizeof(hdr)&&gzwrite(f,&st,sizeof(st))==sizeof(st);
 for(unsigned i=0;i<NSURF&&ok;i++) {
  uint32_t wh[]={surfaces[i]?surfaces[i]->w:0,surfaces[i]?surfaces[i]->h:0};
  ok=gzwrite(f,wh,sizeof(wh))==sizeof(wh);
  if(surfaces[i])for(int y=0;y<surfaces[i]->h&&ok;y++)ok=gzwrite(f,(uint8_t*)surfaces[i]->pixels+y*surfaces[i]->pitch,wh[0]*4)==(int)wh[0]*4;
 }
 if(ok)ok=gzwrite(f,&animation,sizeof(animation))==sizeof(animation);
 if(gzclose(f)!=Z_OK)ok=false;
 /* rename() refuses to replace an existing file on Switch fsdev/FAT and
   * Windows, so overwriting an older save failed: delete the old save and
   * retry once when the plain rename is rejected. */
  if(ok&&rename(tmp,path)){remove(path);ok=rename(tmp,path)==0;}
 if(!ok)remove(tmp);
 char msg[64];snprintf(msg,sizeof(msg),ok?"Saved slot %u":"Save failed (slot %u)",slot);
 notify_status(msg);note("SAVE %s %s",ok?"OK":"FAIL",path);return ok;
}
int load_state(unsigned slot) {
 char path[1200];snprintf(path,sizeof(path),"%s/slot%u.kws",save_dir,slot);
 gzFile f=gzopen(path,"rb");if(!f){char msg[64];snprintf(msg,sizeof(msg),"No saved game in slot %u",slot);notify_status(msg);return 0;}
 struct state next;uint32_t hdr[4];SDL_Surface *nextsurf[NSURF]={0};
 struct ax_player nextanim;ax_reset(&nextanim);
 bool ok=gzread(f,hdr,sizeof(hdr))==sizeof(hdr)&&hdr[0]==0x3153574b&&(hdr[1]==1||hdr[1]==2)&&hdr[2]==sizeof(st)&&hdr[3]==NSURF;
 if(ok)ok=gzread(f,&next,sizeof(next))==sizeof(next)&&next.depth<=NFRAME&&next.waiting==1&&memchr(next.text,0,sizeof(next.text))&&valid_loc(&next.ip);
 if(ok)for(unsigned i=0;i<NPROC;i++)if(!valid_loc(&next.proc[i]))ok=false;
 if(ok)for(unsigned i=0;i<next.depth;i++)if(!valid_loc(&next.frames[i]))ok=false;
 for(unsigned i=0;i<NSURF&&ok;i++) {
  uint32_t wh[2];ok=gzread(f,wh,sizeof(wh))==sizeof(wh);if(!ok)break;
  if(!wh[0]&&!wh[1])continue;
  if(wh[0]<640||wh[1]<480||wh[0]>4096||wh[1]>4096){ok=false;break;}
  nextsurf[i]=SDL_CreateRGBSurfaceWithFormat(0,wh[0],wh[1],32,SDL_PIXELFORMAT_RGBA32);if(!nextsurf[i]){ok=false;break;}
  SDL_SetSurfaceBlendMode(nextsurf[i],SDL_BLENDMODE_NONE);
  for(unsigned y=0;y<wh[1]&&ok;y++)ok=gzread(f,(uint8_t*)nextsurf[i]->pixels+y*nextsurf[i]->pitch,wh[0]*4)==(int)wh[0]*4;
 }
 if(ok&&hdr[1]==2)ok=gzread(f,&nextanim,sizeof(nextanim))==sizeof(nextanim)&&ax_valid(&nextanim);
 if(ok){unsigned char extra;ok=gzread(f,&extra,1)==0&&gzeof(f);}
 if(gzclose(f)!=Z_OK)ok=false;
 if(ok) {
  st=next;animation=nextanim;for(unsigned i=0;i<NSURF;i++){if(surfaces[i])SDL_FreeSurface(surfaces[i]);surfaces[i]=nextsurf[i];nextsurf[i]=NULL;}
  for(int i=0;i<5;i++)audio_stop(i);
  choice_open=false;error_text[0]=0;
 }
 for(unsigned i=0;i<NSURF;i++)if(nextsurf[i])SDL_FreeSurface(nextsurf[i]);
 char msg[64];snprintf(msg,sizeof(msg),ok?"Loaded slot %u":"Save damaged or incompatible (slot %u)",slot);
 notify_status(msg);note("LOADSAVE %s %s",ok?"OK":"FAIL",path);return ok;
}
static void confirm(void) {if(hist_mode){hist_leave();return;}if(st.waiting==3){extern void frontend_xfade_skip(void);frontend_xfade_skip();st.waiting=0;}else if(st.waiting==2){vm_choose(selected);if(st.waiting!=2)choice_open=false;}else vm_advance();}
/* Share the rendering geometry with pointer input; outside taps are not confirms. */
static int choice_at(int x,int y) {
 if(!choice_open||st.waiting!=2)return -1;
 unsigned kind=vm_choice_kind();
 if(kind==41||kind==42||kind==44){
  for(unsigned i=0;i<choice_count;i++){
   SDL_Rect r;unsigned value=vm_menu_value(i);
   if(extras_enabled(kind,value)&&extras_rect(kind,value,&r)&&x>=r.x&&x<r.x+r.w&&y>=r.y&&y<r.y+r.h)return (int)i;
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
  lh=96/(int)vis;if(lh>26)lh=26;if(lh<14)lh=14;
  y0=376+(104-(int)vis*lh)/2;left=28;right=612;
 }
 if(x<left||x>=right||y<y0||y>=y0+(int)vis*lh)return -1;
 return (int)base+(y-y0)/lh;
}
static void pointer_confirm(int x,int y) {
 if(hist_mode){hist_leave();return;}
 if(st.waiting==2){int i=choice_at(x,y);if(i<0)return;selected=(unsigned)i;}
 confirm();
}
int main(int argc,char **argv) {
 const char *fontpath=NULL;const char *start="STARTUP.MES";
#ifdef __SWITCH__
 snprintf(data_dir,sizeof(data_dir),"sdmc:/switch/KAWAXP");
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
 if(mkdir(save_dir,0755)&&errno!=EEXIST){fprintf(stderr,"Cannot create save directory %s\n",save_dir);return 1;}
 char fontbuf[1200];if(!fontpath){snprintf(fontbuf,sizeof(fontbuf),"%s/Kosugi-Regular.ttf",data_dir);fontpath=fontbuf;}
 dbg_noax=flag_on("noax.flag");dbg_notext=flag_on("notext.flag");
 dbg_noaudio=flag_on("noaudio.flag");dbg_audiosync=flag_on("audiosync.flag");
 #ifdef __SWITCH__
  /* redirect diagnostics to a log file on the SD card (read after run) */
  {char lp[1200];snprintf(lp,sizeof(lp),"%s/kawaxp.log",save_dir);
   FILE*lf=fopen(lp,"w");if(lf){dup2(fileno(lf),2);setvbuf(stderr,NULL,_IOLBF,0);}
   fprintf(stderr,"KAWAXP log start %s\n",lp);
   fprintf(stderr,"DBG noax=%d notext=%d noaudio=%d audiosync=%d\n",dbg_noax,dbg_notext,dbg_noaudio,dbg_audiosync);}
#endif
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
 while(running) {
#ifdef __SWITCH__
  if(!appletMainLoop())break;
#endif
  SDL_Event e;while(SDL_PollEvent(&e)) {
   if(e.type==SDL_QUIT)running=false;
   if(e.type==SDL_KEYDOWN&&!e.key.repeat) {
    switch(e.key.keysym.sym) {
    case SDLK_ESCAPE:running=false;break;
    case SDLK_RETURN:case SDLK_SPACE:
     if(hist_mode){hist_leave();}else confirm();break;
    case SDLK_UP:
     if(hist_mode){if(hist_scroll< (int)hist_count-1)hist_scroll++;gfx_dirty=true;}
     else if(selected&&choice_count){selected--;gfx_dirty=true;}break;
    case SDLK_DOWN:
     if(hist_mode){if(hist_scroll>0)hist_scroll--;gfx_dirty=true;}
     else if(selected+1<choice_count){selected++;gfx_dirty=true;}break;
    case SDLK_h:case SDLK_y:
     if(hist_mode)hist_leave();else if(st.waiting==1||st.waiting==3)hist_enter();break;
    case SDLK_F5:vm_slot_menu(true);break;case SDLK_F9:vm_slot_menu(false);break;
    case SDLK_x:ff_hold=true;gfx_dirty=true;break;
    case SDLK_b:if(hist_mode)hist_leave();else {hide_msg=!hide_msg;gfx_dirty=true;}break;
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
     case SDL_CONTROLLER_BUTTON_A:if(hist_mode)hist_leave();else hide_msg=!hide_msg;gfx_dirty=true;break;  /* printed B */
     case SDL_CONTROLLER_BUTTON_Y:ff_hold=true;break;   /* printed X */
     case SDL_CONTROLLER_BUTTON_BACK:
      if(hist_mode)hist_leave();else if(st.waiting==1||st.waiting==3)hist_enter();break; /* printed - */
    }
#else
    switch(fb){
     case SDL_CONTROLLER_BUTTON_A:confirm();break;
     case SDL_CONTROLLER_BUTTON_B:if(hist_mode)hist_leave();else hide_msg=!hide_msg;gfx_dirty=true;break;
     case SDL_CONTROLLER_BUTTON_X:ff_hold=true;gfx_dirty=true;break;
     case SDL_CONTROLLER_BUTTON_BACK:
     case SDL_CONTROLLER_BUTTON_Y:
      if(hist_mode)hist_leave();else if(st.waiting==1||st.waiting==3)hist_enter();break;
    }
#endif
    switch(fb){
     case SDL_CONTROLLER_BUTTON_START:running=false;break;
     case SDL_CONTROLLER_BUTTON_DPAD_UP:
      if(hist_mode){ /* scroll toward older lines; at the oldest line, tap Up again to close */
       if(hist_scroll < (int)hist_count-1)hist_scroll++;
       else hist_leave();
       gfx_dirty=true;
      } else if(!choice_open&&(st.waiting==1||st.waiting==3))hist_enter(); /* short tap Up opens history */
      else if(selected&&choice_count)selected--;
      break;
     case SDL_CONTROLLER_BUTTON_DPAD_DOWN:
      if(hist_mode){if(hist_scroll>0)hist_scroll--;gfx_dirty=true;} /* scroll toward newest */
      else if(selected+1<choice_count)selected++;
      break;
     case SDL_CONTROLLER_BUTTON_LEFTSHOULDER:vm_slot_menu(true);break;
     case SDL_CONTROLLER_BUTTON_RIGHTSHOULDER:vm_slot_menu(false);break;
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
    int i=choice_at(e.motion.x,e.motion.y);if(i>=0)selected=(unsigned)i;
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
  Uint32 anim_now=SDL_GetTicks();animation_update(smoke?AX_TICK_MS:anim_now-anim_last);anim_last=anim_now;
  {extern void frontend_xfade_poll(void);frontend_xfade_poll();}
  if(st.waiting==3&&(smoke||(Sint32)(SDL_GetTicks()-st.sys[255])>=0))st.waiting=0;
  if((ff_hold||getenv("KAWA_FF"))&&!smoke&&!hist_mode) { /* fast-forward: skip timed waits, auto-advance dialogue; stop at choices/menus */
   if(st.waiting==3){if(!getenv("KAWA_FFNOSKIP")){extern void frontend_xfade_skip(void);frontend_xfade_skip();}st.waiting=0;}
   else if(st.waiting==1)vm_advance();
  }
  if(smoke&&st.waiting==2){
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
   {unsigned kk=vm_choice_kind();unsigned want=0;bool matched=false;
 if(kk==2){for(unsigned bi=0;bi<nbrk;bi++){if(!brk_used[bi]){const char*a=st.ip.script,*b=brk[bi].script;
   size_t la=strcspn(a,"."),lb=strcspn(b,".");
   if(la==lb&&!strncasecmp(a,b,la)&&(!brk[bi].have_addr||brk[bi].addr==(unsigned long)vm_last_menu_addr())){brk_used[bi]=1;want=brk[bi].opt;matched=true;break;}}}
  if(!matched&&rpos<rlen)want=route[rpos++];}
 else want=(kk==37)?kawa37:(kk==38)?kawa38:(kk==41)?kawa41:(kk==42)?kawa42:(kk==43)?kawa43:(kk==44)?kawa44:(kk==45)?kawa45:(kk==46)?kawa46:0;unsigned pick=choice_count?(want<choice_count?want:choice_count-1):0;vm_choose(pick);if(st.waiting!=2)choice_open=false;}
  }
  if(smoke&&st.waiting==1) {
   if(getenv("KAWA_HISTTEST")&&st.messages>=strtoul(getenv("KAWA_HISTTEST"),0,10)){
    note("HISTTEST msg=%u count=%u avail=%d",st.messages,hist_count,hist_avail());
    hist_enter();frontend_present();
    if(screenshot){SDL_SaveBMP(canvas,screenshot);screenshot=NULL;}
    hist_leave();break;
   }
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
 extras_close();audio_fini();if(controller)SDL_GameControllerClose(controller);kawa_font_close(font);SDL_DestroyTexture(texture);SDL_FreeSurface(canvas);
 for(unsigned i=0;i<NSURF;i++)if(surfaces[i])SDL_FreeSurface(surfaces[i]);
 SDL_DestroyRenderer(renderer);SDL_DestroyWindow(window);kawa_text_fini();SDL_Quit();return st.waiting==99?1:0;
}
