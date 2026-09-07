/* GPL-2.0-or-later */
#include "title_ui.h"
#include "ax_render.h"

bool title_ui_button(unsigned result, unsigned row, bool selected,
                     SDL_Rect *source, SDL_Rect *destination) {
 if(result<1||result>6||row>=6)return false;
 *source=(SDL_Rect){0,(int)(result-1)*108+(selected?36:0),408,36};
 *destination=(SDL_Rect){116,244+(int)row*36,408,36};
 return true;
}

void title_ui_draw(SDL_Surface *parts, SDL_Surface *canvas,
                   const unsigned *results, unsigned count, unsigned selected) {
 if(!parts||!canvas||parts->w<584||parts->h<888)return;
 /* Settled logo. The PC intro additionally animates the grey frame at y=648. */
 SDL_Rect logo={0,768,584,120}, dest={28,108,584,120};
 SDL_BlitSurface(parts,&logo,canvas,&dest);
 for(unsigned i=0;i<count;i++) {
  SDL_Rect source,destination;
  if(title_ui_button(results[i],i,i==selected,&source,&destination))
   ax_blit(parts,canvas,source.x,source.y,source.w,source.h,destination.x,destination.y);
 }
}
