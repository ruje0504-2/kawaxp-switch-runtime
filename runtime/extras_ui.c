/* GPL-2.0-or-later */
#include "kawa.h"
#include "extras_ui.h"
#include "ax_render.h"
#include "ai5/cg.h"
const char *const music_files[14]={"yokan.wav","yokan2.wav","yokan3.wav","hallf.wav",
 "h1.wav","h2.wav","inbi1.wav","kyofu.wav","niwa2.wav","misako.wav",
 "last.wav","dead.wav","gemend.wav","or.wav"};
int music_playing=-1;
static SDL_Surface *page_bg,*page_parts;
static unsigned cached_kind,cached_page=~0u;
unsigned album_flag(unsigned id){
 if(!id||id>123)return 0;
 if(id==8)return 321;if(id==24)return 322;
 return 199+id-(id>8)-(id>24);
}
bool extras_enabled(unsigned kind,unsigned value){
 if(kind==41)return value==15||(value==14?music_playing>=0:(value<14&&st.flag[1510+value]==1));
 if(kind==42)return !value||(value<=19&&st.flag[129+value]==1);
 if(kind==43){
  extern unsigned scene_page;
  if(!value)return true; /* 戻る */
  if(value==EXTRA_PREV)return scene_page>0;
  if(value==EXTRA_NEXT)return scene_page<7;
  if(value>=1&&value<=5)return st.flag[401+scene_page*5+(value-1)]==1;
  return false;
 }
 if(kind==44){
  if(value==EXTRA_PREV)return st.var[20]>0;
  if(value==EXTRA_NEXT)return st.var[20]<7;
  if(value==EXTRA_PLAY||!value)return true;
  return album_flag(value)&&st.flag[album_flag(value)]==1;
 }
 return true;
}
bool extras_rect(unsigned kind,unsigned v,SDL_Rect *r){
 if(kind==41&&v<16){
  *r=v<14?(SDL_Rect){64+268*(v%2),36+52*(v/2),244,40}:
              (SDL_Rect){v==14?62:450,418,128,28};return true;
 }
 if(kind==42&&v<=19){
  unsigned i=v-1;
  *r=!v?(SDL_Rect){256,416,128,32}:i<4?(SDL_Rect){80+120*i,24,112,92}:
       (SDL_Rect){24+120*((i-4)%5),116+92*((i-4)/5),112,92};return true;
 }
 if(kind==43){
  if(v>=1&&v<=5){*r=(SDL_Rect){24+120*(v-1),112,112,196};return true;}
  if(v==EXTRA_PREV)*r=(SDL_Rect){64,432,80,24};
  else if(v==EXTRA_NEXT)*r=(SDL_Rect){228,432,80,24};
  else if(!v)*r=(SDL_Rect){452,432,124,24}; /* 戻る */
  else return false;
  return true;
 }
 if(kind==44){
  if(v>=1&&v<=123){unsigned i=(v-1)%16;*r=(SDL_Rect){66+132*(i%4),24+100*(i/4),112,84};return true;}
  if(v==EXTRA_PREV)*r=(SDL_Rect){64,432,80,24};
  else if(v==EXTRA_NEXT)*r=(SDL_Rect){228,432,80,24};
  else if(!v)*r=(SDL_Rect){328,432,100,24};
  else if(v==EXTRA_PLAY)*r=(SDL_Rect){452,432,124,24};else return false;
  return true;
 }
 return false;
}
static SDL_Surface *asset(const char *name){
 struct archive_data *d=archive_get(cg_arc,name);if(!d){fail("Missing menu asset %s",name);return NULL;}
 struct cg *c=cg_load_arcdata(d);archive_data_release(d);if(!c){fail("Decode menu asset %s",name);return NULL;}
 SDL_Surface *s=SDL_CreateRGBSurfaceWithFormat(0,c->metrics.w,c->metrics.h,32,SDL_PIXELFORMAT_RGBA32);
 if(s){for(unsigned y=0;y<c->metrics.h;y++)memcpy((Uint8*)s->pixels+y*s->pitch,c->pixels+y*c->metrics.w*4,c->metrics.w*4);SDL_SetSurfaceBlendMode(s,SDL_BLENDMODE_NONE);}
 cg_free(c);return s;
}
void extras_close(void){if(page_bg)SDL_FreeSurface(page_bg);if(page_parts)SDL_FreeSurface(page_parts);page_bg=page_parts=NULL;cached_kind=0;cached_page=~0u;}
static void put(SDL_Surface *src,SDL_Surface *dst,int x,int y,int w,int h,int dx,int dy){
 if(!src)return;SDL_Rect a={x,y,w,h},b={dx,dy,w,h};SDL_BlitSurface(src,&a,dst,&b);
}
void extras_draw(SDL_Surface *canvas,unsigned kind,unsigned selected){
 if(kind==41){
  put(surfaces[1],canvas,0,0,640,480,0,0);
  for(unsigned v=0;v<16;v++){
   SDL_Rect r;extras_rect(kind,v,&r);
   bool enabled=extras_enabled(kind,v);
   int state=!enabled?3:(v<14&&(int)v==music_playing)?2:v==selected?1:0;
   int sx=v<14?244*(v%2):128*state;
   int sy=v<14?160*(v/2)+40*state:v==14?1120:1148;
   ax_blit(surfaces[7],canvas,sx,sy,r.w,r.h,r.x,r.y);
  }
 }else if(kind==42){
  put(surfaces[1],canvas,0,0,640,480,0,0);
  SDL_Rect r;extras_rect(kind,selected,&r);
  if(!selected)ax_blit(surfaces[8],canvas,128,0,128,32,256,416);
  else if(extras_enabled(kind,selected)){
   unsigned i=selected-1;int sx=i<4?96*i:96*((i-4)%5),sy=i<4?56:152+96*((i-4)/5);
   ax_blit(surfaces[8],canvas,sx,sy,96,24,r.x+8,r.y+68);
  }
 }else if(kind==44){
  unsigned page=st.var[20]<8?st.var[20]:0;
  if(cached_kind!=kind||cached_page!=page){
   extras_close();char name[32];snprintf(name,sizeof(name),"cg_tm%02u.gcc",page+1);
   page_parts=asset(name);page_bg=asset(page==7?"cg_bg2.gcc":"cg_bg1.gcc");cached_kind=kind;cached_page=page;
  }
  put(page_bg,canvas,0,0,640,480,0,0);
  for(unsigned i=0;i<(page==7?11:16);i++){
   unsigned v=page*16+i+1;SDL_Rect r;extras_rect(kind,v,&r);
   if(extras_enabled(kind,v))put(page_parts,canvas,112*(i%4),84*(i/4),112,84,r.x,r.y);
   /* Original selection frame from cg_pt: alpha/keyed, no new artwork. */
   if(v==selected&&extras_enabled(kind,v))ax_blit(surfaces[8],canvas,0,120,112,84,r.x,r.y);
  }
  put(surfaces[8],canvas,16*page,96,16,24,164,432);
  const unsigned controls[4]={EXTRA_PREV,EXTRA_NEXT,0,EXTRA_PLAY};
  const int sx[4]={0,80,160,284};
  for(unsigned i=0;i<4;i++){
   SDL_Rect r;extras_rect(kind,controls[i],&r);
   int state=!extras_enabled(kind,controls[i])?3:selected==controls[i]?1:0;
   put(surfaces[8],canvas,sx[i],24*state,r.w,r.h,r.x,r.y);
  }
 }
}
