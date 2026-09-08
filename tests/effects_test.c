/* Exercise production effect state machines against independent pixel fixtures. */
#define main kawa_runtime_main
#include "../runtime/frontend.c"
#undef main
#include <assert.h>
static uint8_t *pixel(unsigned s,int x,int y){return (uint8_t*)surfaces[s]->pixels+y*surfaces[s]->pitch+x*4;}
static void fill(unsigned s,unsigned r,unsigned g,unsigned b){SDL_FillRect(surfaces[s],NULL,SDL_MapRGBA(surfaces[s]->format,r,g,b,255));}
int main(int argc,char **argv){
 assert(SDL_Init(SDL_INIT_TIMER)==0);
 for(unsigned i=0;i<8;i++)assert(ensure_surface(i,640,480));
 /* All 256 mask values at every phase, including opaque black target. */
 msk_data[0]=malloc(640*480);
 for(int i=0;i<640*480;i++)msk_data[0][i]=i%256;
 fill(0,255,127,61);fill(1,0,0,0);
 assert(frontend_xfade_start(0));
 for(unsigned step=1;step<=11;step++){
  xf.next=SDL_GetTicks();frontend_xfade_poll();assert(xf.on);
  for(unsigned m=0;m<256;m++){
   unsigned a=m<step*32?step*32-m+16:0;if(a>255)a=255;
   assert(pixel(0,m,0)[0]==255-a);
   assert(pixel(0,m,0)[1]==127*(255-a)/255);
  }
 }
 xf.next=SDL_GetTicks();frontend_xfade_poll();assert(!xf.on);
 assert(pixel(0,255,0)[0]==0);
 fill(0,0,0,0);fill(1,233,89,5);assert(frontend_xfade_start(0));
 frontend_xfade_skip();assert(!xf.on&&pixel(0,400,300)[0]==233);
 /* Partial region: intermediate rows, odd height, empty size, final bounds. */
 fill(0,9,17,31);fill(1,201,42,19);
 assert(frontend_ppf_start(11,13,30,8));
 ppf.next=SDL_GetTicks()+10000;ppf_poll(false);assert(ppf.i==0);
 ppf.next=SDL_GetTicks();ppf_poll(false);
 assert(pixel(0,11,13)[0]==201&&pixel(0,11,20)[0]==201);
 assert(pixel(0,11,14)[0]==9&&pixel(0,10,13)[0]==9);
 ppf_poll(true);assert(!ppf.on);
 for(int y=0;y<480;y++)for(int x=0;x<640;x++)assert(pixel(0,x,y)[0]==((x>=11&&x<41&&y>=13&&y<21)?201:9));
 assert(frontend_ppf_start(11,13,30,7));ppf_poll(true);assert(!ppf.on);
 assert(!frontend_ppf_start(0,0,0,0));
 /* Quake preserves color, wraps positive shifts, leaves final row untouched,
    advances phase by 479 and stops only on script command. */
 for(int y=0;y<480;y++)for(int x=0;x<640;x++){
  uint8_t *p=pixel(1,x,y);p[0]=x%251;p[1]=y%253;p[2]=77;p[3]=255;
 }
 fill(0,1,2,3);frontend_quake(1);quake_next=SDL_GetTicks();quake_poll();
 assert(quake_phase==479);assert(pixel(0,0,0)[0]==624%251);
 assert(pixel(0,200,100)[2]==77&&pixel(0,0,479)[0]==1);
 unsigned old=quake_phase;fd.on=true;quake_next=SDL_GetTicks();quake_poll();assert(quake_phase==old);fd.on=false;
 for(int i=0;i<50;i++){quake_next=SDL_GetTicks();quake_poll();}assert(quake_level);
 frontend_quake(0);old=quake_phase;quake_next=SDL_GetTicks();quake_poll();assert(quake_phase==old&&!quake_level);
 /* Title intro is separate from util35: grey fade, 256 exact mask levels,
    then menu-only fade. Synthetic assets distinguish every stage. */
 SDL_Surface *parts=SDL_CreateRGBSurfaceWithFormat(0,584,888,32,SDL_PIXELFORMAT_RGBA32);
 SDL_Surface *out=SDL_CreateRGBSurfaceWithFormat(0,640,480,32,SDL_PIXELFORMAT_RGBA32);
 SDL_FillRect(parts,NULL,SDL_MapRGBA(parts->format,220,10,20,255));
 SDL_Rect gr={0,648,584,120},rr={0,768,584,120};
 SDL_FillRect(parts,&gr,SDL_MapRGBA(parts->format,80,80,80,255));
 SDL_FillRect(parts,&rr,SDL_MapRGBA(parts->format,180,0,0,255));
 fill(1,20,30,40);memset(msk_data[0],128,640*480);
 unsigned results[1]={1};
 title_ui_begin(parts,surfaces[1],msk_data[0],true);
 title_ui_draw(parts,out,results,1,~0u);
 assert(((Uint8*)out->pixels)[0]==0);
 for(int i=0;i<17;i++)title_ui_update(20);
 title_ui_draw(parts,out,results,1,~0u);
 assert(*((Uint8*)out->pixels+108*out->pitch+28*4)==80);
 for(int i=0;i<128;i++)title_ui_update(20);
 title_ui_draw(parts,out,results,1,~0u);
 assert(*((Uint8*)out->pixels+108*out->pitch+28*4)==180);
 assert(*((Uint8*)out->pixels+244*out->pitch+116*4)==20);
 for(int i=0;i<128;i++)title_ui_update(20);
 title_ui_draw(parts,out,results,1,~0u);
 /* Phase 2 first frame: buttons alpha 0 (fade, not region brightness),
    so the menu spot still shows the bright backdrop colour 20. */
 assert(*((Uint8*)out->pixels+244*out->pitch+116*4)==20);
 for(int i=0;i<8;i++)title_ui_update(20);
 title_ui_draw(parts,out,results,1,~0u);
 {unsigned v=*((Uint8*)out->pixels+244*out->pitch+116*4);assert(v>20&&v<220);}
 for(int i=0;i<9;i++)title_ui_update(20);
 assert(!title_ui_active());
 title_ui_draw(parts,out,results,1,~0u);
 assert(*((Uint8*)out->pixels+244*out->pitch+116*4)==220);
 title_ui_begin(parts,surfaces[1],msk_data[0],false);title_ui_skip();assert(!title_ui_active());
 title_ui_close();SDL_FreeSurface(parts);SDL_FreeSurface(out);
 if(argc>1){
  free(msk_data[0]);msk_data[0]=NULL;
  ai5_set_game("kawarazakike");seq_arc=archive_open(argv[1],ARCHIVE_RAW);assert(seq_arc);
  for(unsigned mask=0;mask<16;mask++){
   fill(0,0,0,0);fill(1,255,71,189);assert(frontend_xfade_start(mask));
   for(unsigned step=1;step<=12;step++){
    xf.next=SDL_GetTicks();frontend_xfade_poll();
    for(int y=0;y<480;y++)for(int x=0;x<640;x++){
     unsigned m=msk_data[mask][y*640+x];
     unsigned a=step==12?255:m<32*step?32*step-m+16:0;if(a>255)a=255;
     assert(pixel(0,x,y)[0]==a);
    }
   }
  }
  puts("16 original masks: all 12 phases / 58,982,400 pixels passed");
 }
 for(int i=0;i<8;i++)SDL_FreeSurface(surfaces[i]);
 for(int i=0;i<16;i++)free(msk_data[i]);SDL_Quit();
 puts("effects: mask phases/black/skip, region sweep/bounds, quake color/phase/gating/stop passed");return 0;
}
