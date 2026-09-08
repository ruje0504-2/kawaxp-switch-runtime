/* GPL-2.0-or-later. PC 44b0e0 / 44af90, source tables 4971b8 and 4971e0. */
#include "kawa.h"
#include "scene_ui.h"
#include "extras_ui.h"
#include "ax_render.h"
#include "ai5/cg.h"
static SDL_Surface *parts,*backdrop,*frame;
static struct ax_player hover;
static unsigned current_page=~0u,current_selection=~0u,phase;
static const int source[5][2]={{896,0},{336,384},{896,384},{336,768},{896,768}};
bool scene_ui_rect(unsigned v,SDL_Rect *r){
 if(v>=1&&v<=5)*r=(SDL_Rect){24+120*(v-1),24,112,384};
 else if(v==EXTRA_PREV)*r=(SDL_Rect){64,432,80,24};
 else if(v==EXTRA_NEXT)*r=(SDL_Rect){228,432,80,24};
 else if(!v)*r=(SDL_Rect){452,432,124,24};else return false;
 return true;
}
static SDL_Surface *load(const char *name){
 struct archive_data *d=archive_get(cg_arc,name);
 if(!d){fail("Missing scene UI %s",name);return NULL;}
 struct cg *c=cg_load_arcdata(d);archive_data_release(d);
 if(!c){fail("Decode scene UI %s",name);return NULL;}
 SDL_Surface *s=SDL_CreateRGBSurfaceWithFormat(0,c->metrics.w,c->metrics.h,32,SDL_PIXELFORMAT_RGBA32);
 if(s){for(unsigned y=0;y<c->metrics.h;y++)memcpy((Uint8*)s->pixels+y*s->pitch,c->pixels+y*c->metrics.w*4,c->metrics.w*4);SDL_SetSurfaceBlendMode(s,SDL_BLENDMODE_NONE);}
 cg_free(c);if(!s)fail("Allocate scene UI");return s;
}
static void put(int sx,int sy,int w,int h,int dx,int dy){
 SDL_Rect s={sx,sy,w,h},d={dx,dy,w,h};SDL_BlitSurface(parts,&s,frame,&d);
}
static void draw_ax(const uint32_t d[7],void *context){
 (void)context;ax_blit(parts,frame,d[1],d[2],d[3],d[4],d[5],d[6]);
}
void scene_ui_close(void){
 if(parts)SDL_FreeSurface(parts);if(backdrop)SDL_FreeSurface(backdrop);if(frame)SDL_FreeSurface(frame);
 parts=backdrop=frame=NULL;current_page=current_selection=~0u;phase=0;ax_reset(&hover);
}
bool scene_ui_update(unsigned page,unsigned selected,unsigned elapsed){
 if(page>7)return false;
 bool changed=false;
 if(current_page!=page){
  scene_ui_close();char name[32];snprintf(name,sizeof(name),"sc_pt%02u.gcc",page+1);
  parts=load(name);backdrop=load("sc_bg.gcc");
  if(!parts||!backdrop)return false;
  frame=SDL_ConvertSurface(backdrop,backdrop->format,0);if(!frame){fail("Allocate scene frame");return false;}
  struct archive_data *d=archive_get(seq_arc,"sc_ani.ax");
  if(!d||!ax_load(&hover,"sc_ani.ax",d->data,d->size))fail("Load scene hover AX");
  if(d)archive_data_release(d);
  current_page=page;changed=true;
 }
 if(changed||current_selection!=selected){
  SDL_BlitSurface(backdrop,NULL,frame,NULL);
  ax_control(&hover,10,0,0);phase=0;
  for(unsigned i=0;i<5;i++){
   bool enabled=st.flag[401+page*5+i]==1;
   put(enabled?source[i][0]:448,enabled?source[i][1]:0,112,384,24+120*i,24);
  }
  put(page*16,96,16,24,164,432);
  const unsigned values[3]={EXTRA_PREV,EXTRA_NEXT,0};const int xs[3]={0,80,160};
  for(unsigned i=0;i<3;i++){
   SDL_Rect r;scene_ui_rect(values[i],&r);
   bool enabled=i==0?page>0:i==1?page<7:true;
   put(xs[i],!enabled?72:selected==values[i]?24:0,r.w,r.h,r.x,r.y);
  }
  if(selected>=1&&selected<=5&&st.flag[400+page*5+selected]==1){
   ax_control(&hover,1,0,selected-1);
   if(!ax_tick(&hover,draw_ax,NULL))fail("Scene hover AX");
  }
  current_selection=selected;changed=true;
 }
 phase+=elapsed;
 while(phase>=AX_TICK_MS){
  phase-=AX_TICK_MS;
  if(selected>=1&&selected<=5&&hover.cells[selected-1].state!=AX_STOPPED){
   if(!ax_tick(&hover,draw_ax,NULL))fail("Scene hover AX");changed=true;
  }
 }
 return changed;
}
void scene_ui_draw(SDL_Surface *canvas,unsigned page,unsigned selected){
 scene_ui_update(page,selected,0);if(frame)SDL_BlitSurface(frame,NULL,canvas,NULL);
}
