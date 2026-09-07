/* GPL-2.0-or-later. Independent timing/pixel expectations + optional game corpus. */
#include "../runtime/ax.h"
#include "../runtime/ax_render.h"
#include "ai5/game.h"
#include "ai5/arc.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static unsigned drawn;
static void count_draw(const uint32_t d[7],void *p){(void)p;assert(d[0]<3);drawn++;}
static void put32(uint8_t *p,uint32_t n){for(unsigned i=0;i<4;i++)p[i]=n>>(i*8);}
static struct ax_player a;
static uint8_t data[0x600];
static unsigned program(const uint8_t *p,unsigned n) {
 memset(data,0,sizeof(data));
 for(unsigned i=0;i<AX_CELLS;i++)put32(data+i*4,0x5ff);
 data[0x5ff]=255;put32(data,0x540);memcpy(data+0x540,p,n);
 put32(data+0x500,1);put32(data+0x50c,2);put32(data+0x510,1);
 assert(ax_load(&a,"test.ax",data,sizeof(data)));drawn=0;
 assert(ax_control(&a,1,0,0));return 0;
}
static void tick(unsigned n){while(n--)assert(ax_tick(&a,count_draw,NULL));}
static void unit(void) {
 /* Draw, delay=2 (two complete idle ticks), draw, boundary=3, rewind. */
 const uint8_t p[]={9,0,0,0,0,2,2,0,0,0,9,0,0,0,0,1,3,0,0,0,3};
 program(p,sizeof(p));tick(1);assert(drawn==1);tick(3);assert(drawn==1);
 tick(1);assert(drawn==2);assert(ax_control(&a,2,0,0));tick(1);
 assert(a.cells[0].state==255&&a.cells[0].boundary_delay==3);
 /* Resume must consume the outstanding delay, not start at byte zero. */
 assert(ax_control(&a,1,0,0));tick(3);assert(drawn==2);tick(2);assert(drawn==3);
 assert(ax_valid(&a));struct ax_player saved=a;
 tick(25);unsigned result=drawn;drawn=3;a=saved;tick(25);assert(drawn==result);
 const uint8_t finite[]={5,2,0,0,0,9,0,0,0,0,6,255};
 program(finite,sizeof(finite));assert(ax_control(&a,3,0,0));tick(6);
 assert(drawn==2&&!ax_waiting(&a));
 const uint8_t forever[]={5,0,0,0,0,9,0,0,0,0,6};
 program(forever,sizeof(forever));tick(9);assert(drawn==4);assert(ax_control(&a,10,0,0));tick(10);assert(drawn==4);
 const uint8_t nested[]={5,2,0,0,0,7,3,0,0,0,9,0,0,0,0,8,6,255};
 program(nested,sizeof(nested));tick(30);assert(drawn==6&&a.cells[0].state==255);
 const uint8_t bad[]={9,255,255,255,255};program(bad,sizeof(bad));assert(!ax_tick(&a,count_draw,NULL));assert(a.cells[0].state==255);
 const uint8_t single[]={9,0,0,0,0,255};
 for(unsigned kind=0;kind<5;kind++) {
  program(single,sizeof(single));put32(a.data+0x500,kind);tick(2);
  assert(drawn==(kind<3?1:0));
 }
 program(single,sizeof(single));a.phase_ms=20;assert(!ax_valid(&a));
 a.phase_ms=0;a.cells[0].ip=UINT32_MAX;assert(!ax_valid(&a));
 assert(!ax_load(&a,"short",data,10));assert(!ax_control(&a,1,10,0));
 SDL_Surface *s=SDL_CreateRGBSurfaceWithFormat(0,3,1,32,SDL_PIXELFORMAT_RGBA32);
 SDL_Surface *d=SDL_CreateRGBSurfaceWithFormat(0,3,1,32,SDL_PIXELFORMAT_RGBA32);
 assert(s&&d);
 const uint8_t src[]={0,255,0,255,200,100,50,128,7,8,9,255};
 const uint8_t bg[]={10,20,30,255,10,20,30,255,10,20,30,255};
 memcpy(s->pixels,src,12);memcpy(d->pixels,bg,12);ax_blit(s,d,0,0,3,1,0,0);
 const uint8_t expected[]={10,20,30,255,104,59,39,255,7,8,9,255};
 assert(!memcmp(d->pixels,expected,12));
 memcpy(d->pixels,bg,12);ax_blit(s,d,0,0,3,1,-2,0);
 assert(!memcmp(d->pixels,src+8,4)&&!memcmp((uint8_t*)d->pixels+4,bg+4,8));
 SDL_FreeSurface(s);SDL_FreeSurface(d);puts("AX timing, resume, waits, loops, snapshot, malformed input, green key, alpha and clipping: PASS");
}
int main(int argc,char **argv) {
 unit();if(argc<2)return 0;
 ai5_set_game("kawarazakike");struct archive *arc=archive_open(argv[1],ARCHIVE_RAW);assert(arc);
 struct archive_data *d;unsigned files=0,bad=0,total_draws=0;
 archive_foreach(d,arc) {
  const char *ext=strrchr(d->name,'.');if(!ext||strcasecmp(ext,".ax"))continue;
  assert(archive_data_load(d));assert(ax_load(&a,d->name,d->data,d->size));drawn=0;
  for(unsigned i=0;i<AX_CELLS;i++)if(!ax_control(&a,1,i/32,i%32)){fprintf(stderr,"Bad start %s cell %u\n",d->name,i);bad++;}
  for(unsigned t=0;t<2000;t++)if(!ax_tick(&a,count_draw,NULL)){fprintf(stderr,"Bad program %s tick %u\n",d->name,t);bad++;}
  if(!ax_valid(&a)){fprintf(stderr,"Bad snapshot %s\n",d->name);bad++;}
  total_draws+=drawn;files++;archive_data_release(d);
 }
 archive_close(arc);printf("AX corpus: files=%u draws=%u errors=%u (2000 ticks, all 320 cells/file)\n",files,total_draws,bad);
 return bad||!files;
}
