/* Real CREDITS.MES integration: original assets, virtual 20ms animation clock.
 * No smoke-mode timer shortcuts, no translated PNG overrides, no user saves. */
#define main kawa_runtime_main
#include "../runtime/frontend.c"
#undef main
#include <assert.h>
static struct mes_statement *statement(mes_statement_list list,unsigned addr){
 struct mes_statement *q;vector_foreach(q,list)if(q->address==addr)return q;return NULL;
}
static unsigned number(mes_parameter_list p,unsigned i){
 assert(i<vector_length(p)&&vector_A(p,i).type==MES_PARAM_EXPRESSION);
 return ev(vector_A(p,i).expr);
}
int main(int argc,char **argv){
 if(argc!=3){fprintf(stderr,"usage: credits-test GAME_DIR EMPTY_REPORT_DIR\n");return 2;}
 assert(SDL_Init(SDL_INIT_TIMER)==0);ai5_set_game("kawarazakike");
 char path[1400];
 snprintf(path,sizeof(path),"%s/mes.ARC",argv[1]);mes_arc=archive_open(path,ARCHIVE_RAW);assert(mes_arc);
 snprintf(path,sizeof(path),"%s/gcc.ARC",argv[1]);cg_arc=archive_open(path,ARCHIVE_RAW);assert(cg_arc);
 snprintf(path,sizeof(path),"%s/sequence.ARC",argv[1]);seq_arc=archive_open(path,ARCHIVE_RAW);assert(seq_arc);
 snprintf(save_dir,sizeof(save_dir),"%s",argv[2]);data_dir[0]=0;
 for(unsigned i=0;i<NSURF;i++)assert(ensure_surface(i,640,480));
 struct archive_data *d=archive_get(mes_arc,"credits.mes");assert(d);
 mes_statement_list script=vector_initializer;assert(mes_parse_statements(d->data,d->size,&script));archive_data_release(d);
 ax_reset(&animation);vm_start("credits.mes");
 unsigned calls=0,waits=0,iterations=0,elapsed=0;uint8_t before[640*480*4];
 while(strcasecmp(st.ip.script,"start.mes")){
  assert(++iterations<10000&&st.waiting!=99&&st.waiting!=98);
  if(st.waiting==3){
   if(fd.on){frontend_fadein_skip();st.waiting=0;continue;}
   unsigned duration=st.sys[255]-SDL_GetTicks();
   unsigned expected=waits==0?2000:waits==1?4000:waits==24?10000:(waits%2?3000:4000);
   assert(waits<25&&duration<=expected&&duration+50>=expected);
   unsigned changes=0;
   memcpy(before,surfaces[0]->pixels,sizeof(before));
   for(unsigned t=0;t<expected/AX_TICK_MS;t++){
    animation_update(AX_TICK_MS);
    if(memcmp(before,surfaces[0]->pixels,sizeof(before))){
     changes++;memcpy(before,surfaces[0]->pixels,sizeof(before));
     if(changes==4&&waits<24){
      snprintf(path,sizeof(path),"%s/end_staff%02u-%s.bmp",argv[2],waits/2,waits%2?"swirl":"reveal");
      assert(SDL_SaveBMP(surfaces[0],path)==0);
     }
    }
   }
   assert(changes>=5); /* reveal, swirl and logo actually animate on screen */
   assert(st.waiting!=99);elapsed+=expected;
   unsigned cell=waits==24?2:waits%2;
   assert(animation.cells[cell].state==AX_STOPPED);
   if(cell==0||cell==2){
    unsigned lit=0;for(int y=0;y<480;y++)for(int x=0;x<640;x++){
     const uint8_t *px=(const uint8_t*)surfaces[0]->pixels+y*surfaces[0]->pitch+4*x;
     if(px[0]||px[1]||px[2])lit++;
    }
    assert(lit>100); /* actual pixels reached the screen, not just VM progress */
    snprintf(path,sizeof(path),"%s/end_staff%02u-clear.bmp",argv[2],waits/2);
    assert(SDL_SaveBMP(surfaces[0],path)==0);
   }
   waits++;st.waiting=0;continue;
  }
  assert(st.waiting==0);
  struct mes_statement *q=!strcasecmp(st.ip.script,"credits.mes")?statement(script,st.ip.addr):NULL;
  bool staged=q&&q->aiw_op==2&&number(q->CALL.params,0)==24&&number(q->CALL.params,1)==4&&number(q->CALL.params,2)==10;
  unsigned cell=0;
  if(staged){
   cell=number(q->CALL.params,4);assert(cell==(calls==24?2:calls%2));
   assert(!fd.on);assert(surfaces[0]->pitch==640*4);
   memcpy(before,surfaces[0]->pixels,sizeof(before));
  }
  vm_step();
  if(staged){
   assert(st.waiting==0&&!fd.on&&!xf.on); /* must not fade or block */
   assert(!memcmp(before,surfaces[0]->pixels,sizeof(before))); /* no stale-page blit */
   assert(animation.cells[cell].state==0);calls++;
  }
 }
 assert(calls==25&&waits==25&&elapsed==93000&&unsupported==0);
 for(unsigned i=0;i<AX_CELLS;i++)assert(animation.cells[i].state==AX_STOPPED);
 printf("PASS: 13 original staff sheets, 25 AX stages, 93s scripted holds, visible clear frames, no spurious fades/blits, return to START, unsupported=0\n");
 SDL_Quit();return 0;
}
