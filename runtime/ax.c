/* GPL-2.0-or-later. AI.exe 409510..409b11, concrete vtable 493358.
 * The offset table contains programs, not frames. Start resumes the current
 * instruction; only loading a file resets instruction/loop/delay state. */
#include "ax.h"
#include <string.h>
#include <stdio.h>
static uint32_t u32(const uint8_t *p) {
 return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;
}
void ax_reset(struct ax_player *a) {
 memset(a,0,sizeof(*a));
 for(unsigned i=0;i<AX_CELLS;i++)a->cells[i].state=AX_STOPPED;
}
bool ax_load(struct ax_player *a,const char *name,const void *data,size_t size) {
 if(size<0x500||size>AX_CAPACITY||!data||!name||strlen(name)>=sizeof(a->name))return false;
 ax_reset(a);snprintf(a->name,sizeof(a->name),"%s",name);
 a->size=(uint32_t)size;memcpy(a->data,data,size);
 for(unsigned i=0;i<AX_CELLS;i++)a->cells[i].start=u32(a->data+i*4);
 return true;
}
bool ax_valid(const struct ax_player *a) {
 if(!memchr(a->name,0,sizeof(a->name))||a->size>AX_CAPACITY||
    (a->size&&a->size<0x500)||a->phase_ms>=AX_TICK_MS||a->wait_cell>AX_CELLS)return false;
 for(unsigned i=0;i<AX_CELLS;i++) {
  const struct ax_cell *c=&a->cells[i];
  if(c->state!=0&&c->state!=1&&c->state!=3&&c->state!=4&&c->state!=AX_STOPPED)return false;
  if(a->size&&c->start!=u32(a->data+i*4))return false;
  if(c->state!=AX_STOPPED&&(c->start<0x500||c->start>=a->size||c->ip>a->size-c->start))return false;
  for(unsigned j=0;j<2;j++)if(c->loop_ip[j]>a->size||c->remaining[j]>c->total[j])return false;
 }
 return true;
}
bool ax_control(struct ax_player *a,unsigned command,unsigned bank,unsigned cell) {
 /* 410570 uses the low byte of command and low words of both operands. */
 command&=255;bank&=65535;cell&=65535;
 if(command==7||command==10||command==11) {
  for(unsigned i=0;i<AX_CELLS;i++)if(command!=7||a->cells[i].state!=AX_STOPPED)
   a->cells[i].state=command==7?1:AX_STOPPED;
  return true;
 }
 if(command<1||command>4)return true; /* Original dispatcher has no-op holes, including 26. */
 if(command==4)cell=bank; /* PC passes operand 1 twice (4107c3). */
 if(bank>=10||cell>=32||!a->size)return false;
 struct ax_cell *c=&a->cells[bank*32+cell];
 if(c->start<0x500||c->start>=a->size||c->ip>=a->size-c->start)return false;
 c->state=command==2?1:command==4?AX_STOPPED:0;
 if(command==3)a->wait_cell=bank*32+cell+1;
 return true;
}
bool ax_waiting(const struct ax_player *a) {
 return a->wait_cell&&a->cells[a->wait_cell-1].state!=AX_STOPPED;
}
static bool step(struct ax_player *a,struct ax_cell *c,ax_draw_fn draw,void *context) {
 if(c->delay){c->delay--;return true;}
 if(c->boundary_delay) {
  if(c->state==1)c->state=AX_STOPPED;else if(c->state==3)c->state=4;
  c->boundary_delay--;return true;
 }
 if(c->start<0x500||c->start>=a->size||c->ip>=a->size-c->start)return false;
 unsigned op=a->data[c->start+c->ip++];uint32_t arg=0;
 if(op==1||op==2||op==5||op==7||(op>8&&op!=255)) {
  if(a->size-c->start-c->ip<4)return false;
  arg=u32(a->data+c->start+c->ip);c->ip+=4;
 }
 switch(op) {
 case 0:break;
 case 1:
  if(c->state==1)c->state=AX_STOPPED;else if(c->state==3)c->state=4;
  c->boundary_delay=arg;break;
 case 2:c->delay=arg;break;
 case 3:c->ip=0;break;
 case 4:case 255:c->state=AX_STOPPED;break;
 case 5:case 7:{unsigned j=op==7;c->loop_ip[j]=c->ip;c->remaining[j]=c->total[j]=arg;break;}
 case 6:case 8:{unsigned j=op==8;
  if(!c->loop_ip[j]||(c->total[j]&&!c->remaining[j]))return false;
  if(!c->total[j]||--c->remaining[j])c->ip=c->loop_ip[j];
  break;}
 default:{
  if(arg>(a->size-0x500)/28||a->size-0x500-arg*28<28)return false;
  uint32_t d[7];for(unsigned k=0;k<7;k++)d[k]=u32(a->data+0x500+arg*28+k*4);
  /* Concrete kind 0,1,2 implementations are identical. >=3 is a no-op. */
  if(d[0]<3) {
   for(unsigned k=1;k<7;k++)if(d[k]>4096)return false;
   if(draw)draw(d,context);
  }
  break;
 }
 }
 return true;
}
bool ax_tick(struct ax_player *a,ax_draw_fn draw,void *context) {
 bool ok=true;
 for(unsigned i=0;i<AX_CELLS;i++) {
  struct ax_cell *c=&a->cells[i];
  if(c->state==AX_STOPPED||c->state==4)continue;
  if(!step(a,c,draw,context)){c->state=AX_STOPPED;ok=false;}
 }
 if(!ax_waiting(a))a->wait_cell=0;
 return ok;
}
