/* GPL-2.0-or-later */
#include "ax_render.h"
void ax_blit(SDL_Surface *src,SDL_Surface *dst,int sx,int sy,int w,int h,int dx,int dy) {
 if(!src||!dst||src->format->format!=SDL_PIXELFORMAT_RGBA32||dst->format->format!=SDL_PIXELFORMAT_RGBA32)return;
 /* Clip both ends together, preserving the source/destination translation. */
 if(sx<0){w+=sx;dx-=sx;sx=0;}if(sy<0){h+=sy;dy-=sy;sy=0;}
 if(dx<0){w+=dx;sx-=dx;dx=0;}if(dy<0){h+=dy;sy-=dy;dy=0;}
 if(w>src->w-sx)w=src->w-sx;
 if(w>dst->w-dx)w=dst->w-dx;
 if(h>src->h-sy)h=src->h-sy;
 if(h>dst->h-dy)h=dst->h-dy;
 if(w<=0||h<=0)return;
 for(int y=0;y<h;y++) {
  const Uint8 *s=(const Uint8 *)src->pixels+(sy+y)*src->pitch+sx*4;
  Uint8 *d=(Uint8 *)dst->pixels+(dy+y)*dst->pitch+dx*4;
  for(int x=0;x<w;x++,s+=4,d+=4) {
   if(s[0]==0&&s[1]==255&&s[2]==0)continue;
   unsigned a=s[3];
   for(unsigned k=0;k<3;k++)d[k]=s[k]*a/255+d[k]*(255-a)/255;
   d[3]=255;
  }
 }
}
