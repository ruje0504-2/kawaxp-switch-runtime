/* GPL-2.0-or-later. Slot hot areas: AI.exe 430b80, globals 4a6028/2c. */
#include "kawa.h"
#include "save_ui.h"
#include "ax_render.h"
#include "ai5/cg.h"
#include <zlib.h>
#include <sys/stat.h>
#include <time.h>
#ifdef __SWITCH__
#include <switch.h>
#endif
unsigned slot_page;
int slot_pending=-1;
static SDL_Surface *background[2],*parts;
static bool occupied[40],valid[40],editing;
static char descriptions[40][512],memo[256];
static void path_for(char *p,size_t n,unsigned slot,const char *ext){snprintf(p,n,"%s/slot%u.%s",save_dir,slot,ext);}
/* Inflate the head of a gzip KWS file into buf (at most buflen bytes); the
 * file is opened by the caller.  Returns bytes produced (may be < buflen at
 * EOF), or 0 on failure. */
static size_t kws_peek(FILE *f, unsigned char *buf, size_t buflen){
 fseek(f,0,SEEK_END); long fsz=ftell(f); fseek(f,0,SEEK_SET);
 if(fsz<=0)return 0;
 unsigned char *gz=malloc((size_t)fsz); if(!gz)return 0;
 bool ok=fread(gz,1,(size_t)fsz,f)==(size_t)fsz;
 size_t outn=0;
 if(ok){
  z_stream s; memset(&s,0,sizeof(s));
  if(inflateInit2(&s,15+16)==Z_OK){
   s.next_in=gz; s.avail_in=(uInt)fsz;
   s.next_out=buf; s.avail_out=(uInt)buflen;
   int r=inflate(&s,Z_NO_FLUSH);
   if(r==Z_OK||r==Z_STREAM_END)outn=buflen-s.avail_out;
   inflateEnd(&s);
  }
 }
 free(gz);
 return outn;
}
bool save_ui_kind(unsigned kind){return kind==38||kind==45||kind==46;}
void save_ui_scan(void){
 for(unsigned i=0;i<40;i++){
  /* v4 segmented saves live in slot%u.kws.0; legacy single-file slot%u.kws
   * still supported. */
  char path[1200];struct stat sb;
  snprintf(path,sizeof(path),"%s/slot%u.kws.0",save_dir,i);
  occupied[i]=stat(path,&sb)==0;valid[i]=false;descriptions[i][0]=0;
  if(!occupied[i]){path_for(path,sizeof(path),i,"kws");occupied[i]=stat(path,&sb)==0;}
  if(!occupied[i])continue;
  FILE *rf=fopen(path,"rb");if(!rf){continue;}
  unsigned char *head=malloc(sizeof(uint32_t)*4+sizeof(struct state));
  if(!head){fclose(rf);continue;}
  size_t hn=kws_peek(rf,head,sizeof(uint32_t)*4+sizeof(struct state)); fclose(rf);
  struct state saved; memset(&saved,0,sizeof(saved));
  if(hn>=sizeof(uint32_t)*4+sizeof(struct state)){
   uint32_t h[4]; memcpy(h,head,sizeof(h));
   memcpy(&saved,head+sizeof(h),sizeof(saved));
   valid[i]=h[0]==0x3153574b&&h[1]>=1&&h[1]<=4&&h[2]==sizeof(saved)&&
     (h[1]<=2||h[1]==4?h[3]==NSURF:1)&&
     saved.waiting==1&&memchr(saved.text,0,sizeof(saved.text));
  }
  free(head);
  if(!valid[i]){snprintf(descriptions[i],512,"无法读取存档");continue;}
  char date[32]="",text[256];struct tm t;if(localtime_r(&sb.st_mtime,&t))strftime(date,sizeof(date),"%m/%d %H:%M",&t);
  snprintf(text,sizeof(text),"%.240s",saved.text);
  path_for(path,sizeof(path),i,"memo");FILE *m=fopen(path,"rb");if(m){size_t n=fread(text,1,sizeof(text)-1,m);text[n]=0;fclose(m);}
  for(char *p=text;*p;p++)if(*p=='\n'||*p=='\r')*p=' ';
  snprintf(descriptions[i],512,"%s  %s",date,text);
 }
}
bool save_ui_enabled(unsigned kind,unsigned v){
 if(v<40)return kind==45||valid[v];
 return v==~0u||(v>=SLOT_PAGE&&v<SLOT_PAGE+4)||v==SLOT_YES||v==SLOT_NO||(v==SLOT_MEMO&&kind==45);
}
bool save_ui_rect(unsigned kind,unsigned v,SDL_Rect *r){
 if(slot_pending>=0){
  if(kind==45){
   if(v==SLOT_MEMO)*r=(SDL_Rect){152,236,104,24};
   else if(v==SLOT_YES)*r=(SDL_Rect){268,236,104,24};
   else if(v==SLOT_NO)*r=(SDL_Rect){384,236,104,24};else return false;
  }else{
   if(v==SLOT_YES)*r=(SDL_Rect){208,240,104,24};
   else if(v==SLOT_NO)*r=(SDL_Rect){324,240,104,24};else return false;
  }
  return true;
 }
 if(v<40&&v/10==slot_page)*r=(SDL_Rect){40,52+40*(v%10),516,32};
 else if(v>=SLOT_PAGE&&v<SLOT_PAGE+4)*r=(SDL_Rect){572,56+72*(v-SLOT_PAGE),28,64};
 else if(v==~0u)*r=(SDL_Rect){572,352,28,88};else return false;
 return true;
}
static SDL_Surface *asset(const char *name){
 struct archive_data *d=archive_get(cg_arc,name);if(!d){fail("Missing save UI %s",name);return NULL;}
 struct cg *c=cg_load_arcdata(d);archive_data_release(d);if(!c){fail("Decode save UI");return NULL;}
 SDL_Surface *s=SDL_CreateRGBSurfaceWithFormat(0,c->metrics.w,c->metrics.h,32,SDL_PIXELFORMAT_RGBA32);
 if(s){for(unsigned y=0;y<c->metrics.h;y++)memcpy((Uint8*)s->pixels+y*s->pitch,c->pixels+y*c->metrics.w*4,c->metrics.w*4);SDL_SetSurfaceBlendMode(s,SDL_BLENDMODE_NONE);}
 cg_free(c);return s;
}
static void put(SDL_Surface *dst,int sx,int sy,int w,int h,int dx,int dy){SDL_Rect s={sx,sy,w,h},d={dx,dy,w,h};SDL_BlitSurface(parts,&s,dst,&d);}
static void text(SDL_Surface *dst,const char *str,int x,int y,int width,Uint32 color){
 SDL_Surface *s=kawa_text_render(font,str,width,color);if(!s)return;
 SDL_Rect crop={0,0,width,s->h<24?s->h:24},d={x,y,width,crop.h};SDL_BlitSurface(s,&crop,dst,&d);SDL_FreeSurface(s);
}
void save_ui_draw(SDL_Surface *canvas,unsigned kind,unsigned selected){
 unsigned mode=kind==45?0:1;
 if(!parts)parts=asset("sl_pt.gcc");if(!background[mode])background[mode]=asset(mode?"sl_load.gcc":"sl_save.gcc");
 if(!parts||!background[mode])return;
 SDL_Rect dst={8,8,624,464};SDL_BlitSurface(background[mode],NULL,canvas,&dst);
 for(unsigned j=0;j<10;j++){
  unsigned slot=slot_page*10+j;int y=52+40*j;
  if(selected==slot&&save_ui_enabled(kind,slot))ax_blit(parts,canvas,0,88,516,32,40,y);
  char number[16];snprintf(number,sizeof(number),"%02u",slot+1);
  text(canvas,number,48,y+5,36,0xffffff);
  text(canvas,occupied[slot]?descriptions[slot]:"未使用",92,y+5,452,save_ui_enabled(kind,slot)?0xffffff:0x888888);
 }
 for(unsigned p=0;p<4;p++)put(canvas,112*p+28*(slot_page==p?2:selected==SLOT_PAGE+p?1:0),0,28,64,572,56+72*p);
 put(canvas,448+(selected==~0u?28:0),0,28,88,572,352);
 if(slot_pending>=0){
  if(kind==45){
   put(canvas,0,152,384,104,128,188);
   if(selected==SLOT_MEMO)put(canvas,0,416,104,24,152,236);
   if(selected==SLOT_YES)put(canvas,208,416,104,24,268,236);
   if(selected==SLOT_NO)put(canvas,312,416,104,24,384,236);
  }else{
   put(canvas,384,152,272,104,184,184);
   if(selected==SLOT_YES)put(canvas,208,416,104,24,208,240);
   if(selected==SLOT_NO)put(canvas,312,416,104,24,324,240);
  }
  if(editing){put(canvas,0,256,384,136,128,172);text(canvas,memo,152,222,336,0xffffff);}
 }
}
void save_ui_prepare(unsigned slot){slot_pending=(int)slot;snprintf(memo,sizeof(memo),"%.240s",st.text);}
void save_ui_edit(void){
#ifdef __SWITCH__
 SwkbdConfig k;if(R_FAILED(swkbdCreate(&k,0)))return;
 swkbdConfigMakePresetDefault(&k);swkbdConfigSetInitialText(&k,memo);swkbdConfigSetStringLenMax(&k,120);
 char result[256];if(R_SUCCEEDED(swkbdShow(&k,result,sizeof(result))))snprintf(memo,sizeof(memo),"%s",result);
 swkbdClose(&k);
#else
 editing=true;SDL_StartTextInput();
#endif
 frontend_refresh();
}
bool save_ui_event(SDL_Event *e){
 if(!editing)return false;
 if(e->type==SDL_TEXTINPUT){size_t n=strlen(memo),m=strlen(e->text.text);if(n+m<sizeof(memo))memcpy(memo+n,e->text.text,m+1);}
 if(e->type==SDL_KEYDOWN){
  if(e->key.keysym.sym==SDLK_BACKSPACE){size_t n=strlen(memo);if(n){do{n--;}while(n&&((unsigned char)memo[n]&0xc0)==0x80);memo[n]=0;}}
  if(e->key.keysym.sym==SDLK_RETURN||e->key.keysym.sym==SDLK_ESCAPE){editing=false;SDL_StopTextInput();}
 }
 frontend_refresh();return e->type!=SDL_QUIT;
}
void save_ui_write_memo(unsigned slot){
 char path[1200],temp[1200];path_for(path,sizeof(path),slot,"memo");path_for(temp,sizeof(temp),slot,"memo.tmp");
 FILE *f=fopen(temp,"wb");if(!f)return;size_t n=strlen(memo);bool ok=fwrite(memo,1,n,f)==n;if(fclose(f))ok=false;
 if(ok)rename(temp,path);else remove(temp);
#ifdef __SWITCH__
 {extern void switch_hos_commit(void);switch_hos_commit();}
#endif
}
void save_ui_close(void){for(unsigned i=0;i<2;i++)if(background[i]){SDL_FreeSurface(background[i]);background[i]=NULL;}if(parts)SDL_FreeSurface(parts);parts=NULL;editing=false;slot_pending=-1;}
