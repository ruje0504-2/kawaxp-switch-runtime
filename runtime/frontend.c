/* GPL-2.0-or-later */
#include "kawa.h"
#include "ai5/cg.h"
#include "ai5/game.h"
#include "text.h"
#include "ax_render.h"
#include <unistd.h>
#include <sys/stat.h>
#include <zlib.h>
#include <errno.h>
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
struct ax_player animation;
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
void animation_load(const char *name) {
 st.var[14]=1;
 struct archive_data *d=seq_arc?archive_get(seq_arc,name):NULL;
 if(!d){fail("Missing AX %s",name);return;}
 bool ok=ax_load(&animation,name,d->data,d->size);archive_data_release(d);
 if(!ok)fail("Invalid AX %s",name);else {st.var[14]=0;note("AX LOAD %s (%u bytes)",name,animation.size);}
}
static void animation_draw(const uint32_t d[7],void *context) {
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
  if(!ax_tick(&animation,animation_draw,NULL)){fail("Malformed AX program in %s",animation.name);break;}
 }
 if(st.waiting==4&&!ax_waiting(&animation))st.waiting=0;
}
void fill_rect(int x,int y,int ex,int ey,unsigned dst,uint32_t color) {
 if(ex<x||ey<y)return;
 if(!ensure_surface(dst,ex+1,ey+1))return;
 SDL_Rect r={x,y,ex-x+1,ey-y+1};SDL_FillRect(surfaces[dst],&r,rgb(surfaces[dst],color));
}
void copy_rect(int sx,int sy,int ex,int ey,unsigned src,int dx,int dy,unsigned dst,bool mask) {
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
static void text_at(const char *text,int x,int y,uint32_t color,int width) {
 if(!font||!text[0])return;
 SDL_Surface *s=kawa_text_render(font,text,width,color);
 if(!s)return;
 SDL_Rect d={x,y,0,0};SDL_BlitSurface(s,NULL,canvas,&d);SDL_FreeSurface(s);
}
void frontend_present(void) {
 if(!canvas)return;
 SDL_FillRect(canvas,NULL,rgb(canvas,0));
 if(surfaces[0]){SDL_Rect r={0,0,640,480};SDL_BlitSurface(surfaces[0],&r,canvas,NULL);}
 if(st.text[0]) {
  SDL_Rect bg={12,370,616,106};SDL_FillRect(canvas,&bg,rgb(canvas,0x101923));
  text_at(st.text,28,380,st.color,588);
 }
 if(st.waiting==2&&choice_open) {
  unsigned start=selected>=8?selected-7:0,visible=choice_count-start;if(visible>8)visible=8;
  SDL_Rect box={100,100,440,(int)visible*36+16};SDL_FillRect(canvas,&box,rgb(canvas,0x12212d));
  for(unsigned j=0;j<visible;j++)text_at(labels[start+j],120,108+j*36,start+j==selected?0xffdd88:0xffffff,410);
 }
 if(st.waiting==99) {
  SDL_Rect bg={0,0,640,160};SDL_FillRect(canvas,&bg,rgb(canvas,0x501010));
  text_at("Runtime stopped — unsupported operation",14,12,0xffffff,610);
  text_at(error_text,14,44,0xffffff,610);
 }
 if(status_until>SDL_GetTicks()){SDL_Rect r={0,0,640,34};SDL_FillRect(canvas,&r,rgb(canvas,0x12212d));text_at(status,12,4,0xffffff,610);}
 SDL_UpdateTexture(texture,NULL,canvas->pixels,canvas->pitch);SDL_RenderClear(renderer);SDL_RenderCopy(renderer,texture,NULL,NULL);SDL_RenderPresent(renderer);
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
 if(ok&&rename(tmp,path))ok=false;
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
static void confirm(void) {if(st.waiting==2){vm_choose(selected);if(st.waiting!=2)choice_open=false;}else vm_advance();}
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
 if(getenv("KAWA_STRETCH")){SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY,"linear");SDL_RenderSetLogicalSize(renderer,1280,720);}
 else SDL_RenderSetLogicalSize(renderer,640,480);
 canvas=SDL_CreateRGBSurfaceWithFormat(0,640,480,32,SDL_PIXELFORMAT_RGBA32);
 texture=SDL_CreateTexture(renderer,SDL_PIXELFORMAT_RGBA32,SDL_TEXTUREACCESS_STREAMING,640,480);
 font=kawa_font_open(fontpath,18);if(!font||!canvas||!texture){fprintf(stderr,"Font/render init failed\n");return 1;}
 char path[1200];snprintf(path,sizeof(path),"%s/mes.ARC",data_dir);ai5_set_game("kawarazakike");mes_arc=archive_open(path,ARCHIVE_RAW);
 snprintf(path,sizeof(path),"%s/gcc.ARC",data_dir);cg_arc=archive_open(path,ARCHIVE_RAW);
 snprintf(path,sizeof(path),"%s/sequence.ARC",data_dir);seq_arc=archive_open(path,ARCHIVE_RAW);
 if(!mes_arc||!cg_arc||!seq_arc){fprintf(stderr,"Missing game archives\n");return 1;}
 SDL_GameController *controller=NULL;for(int i=0;i<SDL_NumJoysticks();i++)if(SDL_IsGameController(i)){controller=SDL_GameControllerOpen(i);break;}
 audio_init();vm_start(start);Uint32 last=SDL_GetTicks();unsigned frames=0;
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
    case SDLK_RETURN:case SDLK_SPACE:confirm();break;
    case SDLK_UP:if(selected)selected--;break;
    case SDLK_DOWN:if(selected+1<choice_count)selected++;break;
    case SDLK_F5:vm_slot_menu(true);break;case SDLK_F9:vm_slot_menu(false);break;
    }
   }
   if(e.type==SDL_CONTROLLERBUTTONDOWN){switch(e.cbutton.button){
    case SDL_CONTROLLER_BUTTON_A:confirm();break;
    case SDL_CONTROLLER_BUTTON_START:running=false;break;
    case SDL_CONTROLLER_BUTTON_DPAD_UP:if(selected)selected--;break;
    case SDL_CONTROLLER_BUTTON_DPAD_DOWN:if(selected+1<choice_count)selected++;break;
    case SDL_CONTROLLER_BUTTON_LEFTSHOULDER:vm_slot_menu(true);break;
    case SDL_CONTROLLER_BUTTON_RIGHTSHOULDER:vm_slot_menu(false);break;
   }}
   if(e.type==SDL_MOUSEBUTTONDOWN&&e.button.button==SDL_BUTTON_LEFT) {
    if(st.waiting==2){int y=e.button.y;unsigned base=selected>=8?selected-7:0;if(y>=108){unsigned i=base+(y-108)/36;if(i<choice_count)selected=i;}}
    confirm();
   }
  }
  Uint32 anim_now=SDL_GetTicks();animation_update(smoke?AX_TICK_MS:anim_now-anim_last);anim_last=anim_now;
  if(st.waiting==3&&(smoke||(Sint32)(SDL_GetTicks()-st.sys[255])>=0))st.waiting=0;
  if(smoke&&st.waiting==2){unsigned kk=vm_choice_kind();unsigned want=0;bool matched=false;
 if(kk==2){for(unsigned bi=0;bi<nbrk;bi++){if(!brk_used[bi]){const char*a=st.ip.script,*b=brk[bi].script;
   size_t la=strcspn(a,"."),lb=strcspn(b,".");
   if(la==lb&&!strncasecmp(a,b,la)&&(!brk[bi].have_addr||brk[bi].addr==(unsigned long)vm_last_menu_addr())){brk_used[bi]=1;want=brk[bi].opt;matched=true;break;}}}
  if(!matched&&rpos<rlen)want=route[rpos++];}
 else want=(kk==37)?kawa37:(kk==38)?kawa38:(kk==41)?kawa41:(kk==42)?kawa42:(kk==43)?kawa43:(kk==44)?kawa44:(kk==45)?kawa45:(kk==46)?kawa46:0;unsigned pick=choice_count?(want<choice_count?want:choice_count-1):0;vm_choose(pick);if(st.waiting!=2)choice_open=false;}
  if(smoke&&st.waiting==1) {
   if(st.messages>=(unsigned)smoke_messages) {if(!hold_ticks)break;hold_ticks--;}
   else {
    {static bool as_done=false; if(!as_done&&autosave_at&&st.messages>=autosave_at){as_done=true;note("AUTOSAVE open slot menu");vm_slot_menu(true);}}
    vm_advance();
   }
  }
  for(unsigned i=0;i<300&&!st.waiting;i++)vm_step();
  frontend_present();frames++;
  if(smoke&&(st.waiting==99||st.waiting==98||frames>frames_cap))break;
  if(smoke)SDL_Delay(1);else if(SDL_GetTicks()-last<10)SDL_Delay(10-(SDL_GetTicks()-last));last=SDL_GetTicks();
 }
 frontend_present();if(screenshot)SDL_SaveBMP(canvas,screenshot);
 if(smoke&&st.waiting==1) {
  struct state old=st;struct ax_player oldanim=animation;
  if(!save_state(99)){fail("Smoke save failed");}
  else {st.var[1]^=123;ax_reset(&animation);if(!load_state(99)||memcmp(&st,&old,sizeof(st))||memcmp(&animation,&oldanim,sizeof(animation)))fail("Smoke save round trip mismatch");else note("SAVE ROUNDTRIP PASS (VM + AX)");}
 }
 note("RESULT messages=%u unsupported=%u waiting=%u script=%s addr=%x",st.messages,unsupported,st.waiting,st.ip.script,st.ip.addr);
 audio_fini();if(controller)SDL_GameControllerClose(controller);kawa_font_close(font);SDL_DestroyTexture(texture);SDL_FreeSurface(canvas);
 for(unsigned i=0;i<NSURF;i++)if(surfaces[i])SDL_FreeSurface(surfaces[i]);
 SDL_DestroyRenderer(renderer);SDL_DestroyWindow(window);kawa_text_fini();SDL_Quit();return st.waiting==99?1:0;
}
