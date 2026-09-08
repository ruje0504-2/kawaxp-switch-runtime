/* GPL-2.0-or-later. Optional original-asset corpus, no save writes. */
#include "../runtime/kawa.h"
#include "../runtime/scene_ui.h"
#include "../runtime/extras_ui.h"
#include "ai5/game.h"
#include "ai5/cg.h"
#include <assert.h>
#include <stdarg.h>
struct state st;
struct archive *cg_arc,*seq_arc;
static unsigned errors;
void fail(const char *fmt,...){va_list a;va_start(a,fmt);vfprintf(stderr,fmt,a);va_end(a);errors++;}
static void check_column(SDL_Surface *s,struct cg *c,int sx,int sy,int dx){
 for(int y=0;y<384;y++)assert(!memcmp((Uint8*)s->pixels+(24+y)*s->pitch+dx*4,c->pixels+((sy+y)*c->metrics.w+sx)*4,112*4));
}
int main(int argc,char **argv){
 SDL_Rect r;assert(!scene_ui_rect(6,&r));assert(!scene_ui_rect(999,&r));
 if(argc<2)return 0;
 ai5_set_game("kawarazakike");char path[1200];
 snprintf(path,sizeof(path),"%s/gcc.ARC",argv[1]);cg_arc=archive_open(path,ARCHIVE_RAW);
 snprintf(path,sizeof(path),"%s/sequence.ARC",argv[1]);seq_arc=archive_open(path,ARCHIVE_RAW);
 assert(cg_arc&&seq_arc);
 SDL_Surface *canvas=SDL_CreateRGBSurfaceWithFormat(0,640,480,32,SDL_PIXELFORMAT_RGBA32);assert(canvas);
 for(unsigned p=0;p<8;p++){
  char name[32];snprintf(name,sizeof(name),"sc_pt%02u.gcc",p+1);
  struct archive_data *d=archive_get(cg_arc,name);assert(d);struct cg *c=cg_load_arcdata(d);archive_data_release(d);assert(c);
  for(unsigned i=0;i<5;i++)st.flag[401+p*5+i]=1;
  scene_ui_draw(canvas,p,EXTRA_NEXT);
  check_column(canvas,c,896,0,24);check_column(canvas,c,336,384,144);
  check_column(canvas,c,896,384,264);check_column(canvas,c,336,768,384);check_column(canvas,c,896,768,504);
  for(unsigned i=1;i<=5;i++)for(unsigned t=0;t<160;t++)scene_ui_update(p,i,20);
  scene_ui_close();st.flag[401+p*5]=0;scene_ui_draw(canvas,p,0);
  check_column(canvas,c,448,0,24);cg_free(c);scene_ui_close();assert(!errors);
 }
 SDL_FreeSurface(canvas);archive_close(cg_arc);archive_close(seq_arc);
 puts("SCENE_UI_OK 8 pages, 40 strips, locked art, hover AX");return 0;
}
