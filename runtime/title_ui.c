/* GPL-2.0-or-later */
#include "title_ui.h"
#include "ax_render.h"
static SDL_Surface *grey,*red,*fadebuf;
static const Uint8 *intro_mask;
/* 44b3a0: brightness 0,8,...120,255 on the full screen (logo intro),
 * mask 11 thresholds 0..255 (logo colour swap); the menu buttons then
 * alpha-fade in over the already-bright title: no brightness() on the
 * region, so the backdrop never dims and no black rectangle appears. */
static unsigned phase=3,step,accum;
void title_ui_close(void){
 if(grey)SDL_FreeSurface(grey);
 if(red)SDL_FreeSurface(red);
 if(fadebuf)SDL_FreeSurface(fadebuf);
 grey=red=fadebuf=NULL;intro_mask=NULL;phase=3;
}
void title_ui_begin(SDL_Surface *parts,SDL_Surface *background,const Uint8 *mask,bool intro){
 title_ui_close();
 if(!parts||!background||parts->w<584||parts->h<888)return;
 grey=SDL_ConvertSurface(background,background->format,0);
 red=SDL_ConvertSurface(background,background->format,0);
 if(!grey||!red){title_ui_close();return;}
 SDL_Rect src={0,648,584,120},dst={28,108,584,120};
 SDL_BlitSurface(parts,&src,grey,&dst);
 src.y=768;dst=(SDL_Rect){28,108,584,120};SDL_BlitSurface(parts,&src,red,&dst);
 intro_mask=mask;phase=intro?0:2;step=accum=0;
}
bool title_ui_active(void){return phase<3;}
void title_ui_skip(void){phase=3;}
bool title_ui_update(unsigned elapsed){
 if(!title_ui_active())return false;
 accum+=elapsed;
 if(accum<20)return false;
 /* Never skip a presented phase when rendering takes longer than one tick. */
 accum-=20;
 if(++step>=(phase==1?256:17)){phase++;step=0;}
 return true;
}
static void brightness(SDL_Surface *dst,SDL_Rect r,unsigned value){
 for(int y=r.y;y<r.y+r.h;y++)for(int x=r.x;x<r.x+r.w;x++){
  Uint8 *p=(Uint8*)dst->pixels+y*dst->pitch+x*4;
  for(int c=0;c<3;c++)p[c]=(unsigned)p[c]*value/255;
 }
}

bool title_ui_button(unsigned result, unsigned row, bool selected,
                     SDL_Rect *source, SDL_Rect *destination) {
 if(result<1||result>6||row>=6)return false;
 *source=(SDL_Rect){0,(int)(result-1)*108+(selected?36:0),408,36};
 *destination=(SDL_Rect){116,244+(int)row*36,408,36};
 return true;
}

/* Blit one 408x36 button with its own alpha scaled by f (0..255): the pixels
 * already under it (bright title backdrop) are never dimmed, so the menu
 * fades in instead of reading as a black rectangle. */
static void ax_blit_faded(SDL_Surface *parts,SDL_Surface *canvas,SDL_Rect s,SDL_Rect d,unsigned f){
 if(!parts||!canvas||parts->format->format!=SDL_PIXELFORMAT_RGBA32||canvas->format->format!=SDL_PIXELFORMAT_RGBA32)return;
 if(f>=255){ax_blit(parts,canvas,s.x,s.y,s.w,s.h,d.x,d.y);return;}
 if(!fadebuf||fadebuf->w!=s.w||fadebuf->h!=s.h){
  if(fadebuf)SDL_FreeSurface(fadebuf);
  fadebuf=SDL_CreateRGBSurfaceWithFormat(0,s.w,s.h,32,SDL_PIXELFORMAT_RGBA32);
 }
 if(!fadebuf)return;
 for(int y=0;y<s.h;y++)
  memcpy((Uint8*)fadebuf->pixels+y*fadebuf->pitch,
         (Uint8*)parts->pixels+(s.y+y)*parts->pitch+s.x*4,s.w*4);
 if(f<255){
  for(int y=0;y<s.h;y++){
   Uint8 *p=(Uint8*)fadebuf->pixels+y*fadebuf->pitch;
   for(int x=0;x<s.w;x++)p[x*4+3]=(unsigned)p[x*4+3]*f/255;
  }
 }
 ax_blit(fadebuf,canvas,0,0,s.w,s.h,d.x,d.y);
}

void title_ui_draw(SDL_Surface *parts, SDL_Surface *canvas,
                   const unsigned *results, unsigned count, unsigned selected) {
 if(!parts||!canvas||parts->w<584||parts->h<888)return;
 SDL_Rect screen={0,0,640,480};
 if(grey&&red&&phase<3){
  SDL_BlitSurface(phase<2?grey:red,&screen,canvas,&screen);
  if(phase==0){brightness(canvas,screen,step<16?step*8:255);return;}
  if(phase==1){
   /* 4374d0 copies pixels whose mask == threshold; cumulative result <= step. */
   for(int y=108;y<228;y++)for(int x=28;x<612;x++){
    if(!intro_mask||intro_mask[y*640+x]<=step)
     memcpy((Uint8*)canvas->pixels+y*canvas->pitch+4*x,
            (Uint8*)red->pixels+y*red->pitch+4*x,4);
   }
   return;
  }
 }
 /* phase==2: `red` (bright title) is already on canvas above. Settled logo,
  * kept while the buttons fade in on top of it. */
 SDL_Rect logo={0,768,584,120}, dest={28,108,584,120};
 SDL_BlitSurface(parts,&logo,canvas,&dest);
 for(unsigned i=0;i<count;i++) {
  SDL_Rect source,destination;
  if(!title_ui_button(results[i],i,i==selected,&source,&destination))continue;
  if(phase==2){
   unsigned f=step<16?step*16:255;if(f>255)f=255;
   ax_blit_faded(parts,canvas,source,destination,f);
  } else ax_blit(parts,canvas,source.x,source.y,source.w,source.h,destination.x,destination.y);
 }
}
