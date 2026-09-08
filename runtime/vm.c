/* GPL-2.0-or-later. KAWAXP AST interpreter; original program addresses in RE.md. */
#include "kawa.h"
#include "zh.h"
#include "extras_ui.h"
#include "scene_ui.h"
#include "save_ui.h"
static void storage_menu(unsigned kind);
#include <stdarg.h>
#include <string.h>
#include <strings.h>
#include <stdlib.h>
struct state st;
struct archive *mes_arc,*cg_arc,*seq_arc;
unsigned unsupported;
char error_text[512];
struct script { char name[32]; mes_statement_list ast; struct mes_statement **map; unsigned size; };
static struct script scripts[128];
static unsigned nscripts;
static struct mes_statement *menus[16];
static struct loc menu_locs[16];
static struct aiw_mes_menu_case *choices[100];
static struct loc choice_locs[100];
static unsigned choice_ord[100]; /* declared case ordinal (1-based, counts condition-hidden cases): what scripts test in var32[18] */
static unsigned nchoices;
static unsigned choice_kind;
static unsigned menu_nums[100]; /* engine menus (title/load) return these in var[18] */
static unsigned last_menu_addr;  /* address of the currently open script menu */
unsigned vm_last_menu_addr(void){return last_menu_addr;}
static uint64_t steps;
void fail(const char *fmt,...) { va_list ap;va_start(ap,fmt);vsnprintf(error_text,sizeof(error_text),fmt,ap);va_end(ap);fprintf(stderr,"STOP %s:%x %s\n",st.ip.script,st.ip.addr,error_text);st.waiting=99; }
void note(const char *fmt,...) { va_list ap;va_start(ap,fmt);vfprintf(stderr,fmt,ap);va_end(ap);fputc('\n',stderr); }
static unsigned bound(unsigned i,unsigned n) { if(i>=n) {fail("Variable index %u >= %u",i,n);return 0;}return i; }
uint32_t ev(struct mes_expression *e) {
 if(!e) return 0;
 uint32_t a=e->sub_a?ev(e->sub_a):0,b=e->sub_b?ev(e->sub_b):0;
 switch(e->aiw_op) {
 case 0:return e->arg8;
 case 0x80:return st.var[bound(e->arg8,32)];
 case 0xf1:return e->arg16;case 0xf2:return e->arg32;
 case 0xf3:return st.flag[bound(e->arg16,NFLAG)];case 0xf4:return st.flag[bound(a,NFLAG)];
 case 0xf6:return st.word[bound(e->arg16,NVAR)];case 0xf7:return st.word[bound(a,NVAR)];
 case 0xf8:return st.sys[bound(e->arg16,NSYS)];case 0xf9:return st.sys[bound(a,NSYS)];
 case 0xe0:return b+a;case 0xe1:return b-a;case 0xe2:return b*a;
 case 0xe3:if(!a){fail("Division by zero");return 0;}return b/a;
 case 0xe4:if(!a){fail("Modulo by zero");return 0;}return b%a;
 case 0xe5:return e->arg16?(unsigned)rand()%e->arg16:0;
 case 0xe6:return !!b&&!!a;case 0xe7:return !!b||!!a;
 case 0xe8:return b&a;case 0xe9:return b|a;case 0xea:return b^a;
 case 0xeb:return b<a;case 0xec:return b>a;case 0xed:return b<=a;case 0xee:return b>=a;
 case 0xef:return b==a;case 0xf0:return b!=a;
 default:fail("Unimplemented expression %02x",e->aiw_op);return 0;
 }
}
static uint32_t par(mes_parameter_list p,unsigned i) { if(i>=vector_length(p))return 0;struct mes_parameter *q=&vector_A(p,i);if(q->type!=MES_PARAM_EXPRESSION){fail("Expected numeric parameter %u",i);return 0;}return ev(q->expr); }
static const char *str(mes_parameter_list p,unsigned i) { if(i>=vector_length(p)||vector_A(p,i).type!=MES_PARAM_STRING){fail("Expected string parameter %u",i);return "";}return vector_A(p,i).str; }
static void index_list(struct script *s,mes_statement_list list) {
 struct mes_statement *q;vector_foreach(q,list) {
  if(q->address>=s->size){fail("Bad script statement address");return;}
  s->map[q->address]=q;
  if(q->aiw_op==0x13) {struct aiw_mes_menu_case *c;vector_foreach_p(c,q->AIW_DEF_MENU.cases)index_list(s,c->body);}
 }
}
static struct script *get_script(const char *name) {
 for(unsigned i=0;i<nscripts;i++)if(!strcasecmp(scripts[i].name,name))return &scripts[i];
 if(nscripts==128||strlen(name)>=32){fail("Invalid script name/cache limit");return NULL;}
 struct archive_data *d=archive_get(mes_arc,name);
 if(!d){fail("Missing script %s",name);return NULL;}
 struct script *s=&scripts[nscripts];memset(s,0,sizeof(*s));snprintf(s->name,32,"%s",name);
 if(!mes_parse_statements(d->data,d->size,&s->ast)){archive_data_release(d);fail("Script parse failed %s",name);return NULL;}
 s->size=d->size;s->map=calloc(s->size,sizeof(*s->map));archive_data_release(d);
 if(!s->map){fail("Out of memory indexing %s",name);return NULL;}
 index_list(s,s->ast);nscripts++;note("LOAD %s",name);return s;
}
static bool is_over_script(const char *s){return s&&!strncasecmp(s,"OVER",4);}
/* replay/view scripts that load their own voice/BGM: leaving them (back to a
 * menu/title/other flow) must cut their audio immediately. */
static bool is_replay_script(const char*s){
 if(!s||!s[0])return false;
 if(!strncasecmp(s,"ALLPIC",6))return true;
 if(!strncasecmp(s,"EVENT",5)&&s[5]>='0'&&s[5]<='9')return true;
 if(is_over_script(s))return true;
 if(!strncasecmp(s,"CREDITS",7))return true;
 return false;
}
static void jump(const char *name,uint32_t addr) {
 struct script *s=get_script(name);if(!s)return;
 if(is_replay_script(st.ip.script)&&!is_replay_script(s->name)){
  if(audio_bgm_dirty){audio_stop_all();audio_bgm_dirty=0;audio_bgm_restore();} /* replay loaded its own BGM: stop, then resume menu music */
  else audio_stop_voice_se(); /* keep title/menu music on ch0 */
 }else if(!is_replay_script(st.ip.script)&&is_replay_script(s->name)){
  audio_bgm_snapshot(); /* remember the menu/title track playing before the replay */
 }
 snprintf(st.ip.script,32,"%s",s->name);st.ip.addr=addr;
 }
void vm_jump_at(const char *name,uint32_t addr){jump(name,addr);}
static void push(struct loc to) {
 if(st.depth==NFRAME){fail("Call stack overflow");return;}
 if(!is_replay_script(st.ip.script)&&is_replay_script(to.script))audio_bgm_snapshot();
 st.frames[st.depth++]=st.ip;st.ip=to;
 }
static void ret(void) {
 if(st.depth){
  if(is_replay_script(st.ip.script)&&!is_replay_script(st.frames[st.depth-1].script)){
   if(audio_bgm_dirty){audio_stop_all();audio_bgm_dirty=0;audio_bgm_restore();}
   else audio_stop_voice_se();
  }
  st.ip=st.frames[--st.depth];
 }else st.waiting=98;
 }
/* Pre-register DEFINE.MES procedures so any script can be started standalone
 * (normal boot goes STARTUP->define.mes->start.mes and registers them while
 * executing). defproc body statements sit between next_address and skip_addr. */
static void vm_preload_define(void) {
 struct script *d = get_script("define.mes");
 if (!d) return;
 struct mes_statement *q;
 vector_foreach(q, d->ast) {
  if (q->aiw_op == 0x11) { /* DEF_PROC */
   unsigned n = q->DEF_PROC.no_expr ? ev(q->DEF_PROC.no_expr) : 0;
   if (n < NPROC && !st.proc[n].script[0]) {
    st.proc[n].addr = q->next_address;
    snprintf(st.proc[n].script, sizeof(st.proc[n].script), "%s", "define.mes");
   }
  }
 }
}
void vm_start(const char *name) { memset(&st,0,sizeof(st));memset(menus,0,sizeof(menus));st.color=0xffffff;st.sys[2]=32;st.sys[3]=414;st.sys[4]=624;st.sys[5]=479;st.sys[13]=24;
 st.var[14]=0; /* resource-load result: AI.exe 4106b6 writes 0 on AX success. */
 vm_preload_define();
 /* START.MES boot guard: "jz var4[154] != 1 L_1" falls through (running
  * Load.load(100)) only while var4[154]!=1. Cold boot flags are zero, so the
  * flag bank is restored on the first START.MES entry; ENDING/SCENE/SOUND.MES
  * set var4[154]=1 immediately before jumping back to START.MES to skip that
  * redundant reload, since they already persisted via Save.update_var4(100). */
 jump(name,0); }
/* AIW LOAD/SAVE statements: the first parameter selects a child of the
 * Load[]/Save[] namespaces (decompiler prints e.g. Load.load / Save.update_var4),
 * the rest are that child's arguments. KAWAXP scripts only ever use:
 *   Load.load(100)        -- LOAD idx 1, slot 100: fetch the shared flag bank
 *   Save.update_var4(100) -- SAVE idx 3, slot 100: persist the flag array back
 * (grep of all 90 MES scripts; per-slot SaveData / var16-var32 areas are
 * engine-menu territory and never appear as script statements).
 * Slot 100 is the game's global/common slot = original file savedata\flag%04d
 * with %04d==0100. RE of AI.exe (flag accessors 0x40d9b0/0x40d9f0, file
 * serializers 0x428870/0x4288e0, name template "flag0000" @0x494900):
 *   - file payload is always 0x10f0 (4336) bytes;
 *   - the script flag array lives 0x540 bytes into the file, 2500 bytes long,
 *     5000 flags x 4-bit nibbles: flag n at byte [0x540 + n/2], EVEN n in the
 *     HIGH nibble, ODD n in the LOW nibble (runtime st.flag is one byte per
 *     flag, values clamped to 15, so pack/unpack converts byte<->nibble);
 *   - bytes outside the flag region (save header, other arrays) are left as
 *     zero for the global bank, matching the shipped all-zero FLAG0100. */
#define FLAG_FILE_BASE 0x540
#define FLAG_FILE_SIZE 0x10f0
_Static_assert(NFLAG == 5000, "flag bank format assumes 5000 nibble flags");
static void flag_pack(unsigned char *f) {
 for (unsigned n = 0; n < NFLAG; n++) {
  unsigned v = st.flag[n]; if (v > 15) v = 15;
  unsigned p = FLAG_FILE_BASE + n / 2;
  if (n & 1) f[p] = (f[p] & 0xf0) | v;
  else f[p] = (f[p] & 0x0f) | (v << 4);
 }
}
static void flag_unpack(const unsigned char *f) {
 for (unsigned n = 0; n < NFLAG; n++) {
  unsigned char b = f[FLAG_FILE_BASE + n / 2];
  st.flag[n] = (n & 1) ? (b & 0x0f) : (b >> 4);
 }
}
static void flag_bank(bool write) {
 char path[1200];snprintf(path,sizeof(path),"%s/FLAG0100",save_dir);
 FILE *f=fopen(path,write?"wb":"rb");
 if(!f&&!write){ /* migrate/accept the legacy lowercase name (PC 全图存档 etc.) */
  snprintf(path,sizeof(path),"%s/flag0100",save_dir);f=fopen(path,"rb");}
 if(!f){if(write)fail("Cannot write global flag bank");return;}
 if(write) {
  unsigned char file[FLAG_FILE_SIZE] = {0};
  flag_pack(file);
  if(fwrite(file,1,sizeof(file),f)!=sizeof(file))fail("Global flag bank write failed");
  if(getenv("KAWA_FLAGDUMP"))note("FLAGS %d %d %d %d %d %d %d %d %d %d %d",
   st.flag[106],st.flag[110],st.flag[111],st.flag[113],st.flag[116],st.flag[147],st.flag[148],st.flag[152],st.flag[161],st.flag[127],st.flag[126]);
 } else {
  unsigned char file[FLAG_FILE_SIZE];
  size_t n=fread(file,1,sizeof(file),f);
  if(n==sizeof(file))flag_unpack(file);else if(n)fail("Bad global flag bank file");
  if(getenv("KAWA_FLAGDUMP"))note("LOADFLAG 152=%d 199=%d 200=%d 201=%d 306=%d 320=%d 321=%d 322=%d 385=%d 400=%d 401=%d 424=%d 440=%d 985=%d 1494=%d",
   st.flag[152],st.flag[199],st.flag[200],st.flag[201],st.flag[306],st.flag[320],st.flag[321],st.flag[322],
   st.flag[385],st.flag[400],st.flag[401],st.flag[424],st.flag[440],st.flag[985],st.flag[1494]);
 }
 fclose(f);note("FLAGBANK %s %s",write?"SAVE":"LOAD",path);
#ifdef __SWITCH__
 if(write){extern void switch_hos_commit(void);switch_hos_commit();}
#endif
}
static void savedata_op(bool write,mes_parameter_list p) {
 unsigned idx=par(p,0),slot=par(p,1);
 switch(idx) {
 case 1: // Load.load / Save.save (full slot state)
  if(slot==100){flag_bank(write);break;}
  fail("%s slot %u not implemented",write?"Save.save":"Load.load",slot);break;
 case 3: // Save.update_var4 / Load.update_var4 (flag array only)
  if(!write){fail("Load.update_var4 not used by this game's scripts");break;}
  if(slot!=100){fail("Save.update_var4 slot %u not implemented",slot);break;}
  flag_bank(true);break;
 default:
  fail("Unimplemented %s child %u",write?"Save":"Load",idx);break;
 }
}
static void assign_values(unsigned op,unsigned index,mes_expression_list vals) {
 struct mes_expression *e;vector_foreach(e,vals) {
  uint32_t v=ev(e);
  switch(op){case 5:case 6:st.flag[bound(index,NFLAG)]=v>15?15:v;break;
  case 10:case 11:st.word[bound(index,NVAR)]=v>65535?65535:v;break;
  case 12:case 13:st.sys[bound(index,NSYS)]=v;break;
  default:fail("Assignment %u",op);return;}
  index++;
 }
}
unsigned scene_page=0; /* util43 scene replay: current event page 0..7 (AI.exe util43) */
/* set when an ending replay is launched from the エンディング (util42) menu: only
 * those OVER plays are views; endings reached by the real story are live gameplay
 * and may open the save/load menu. Cleared once execution leaves OVER/CREDITS. */
static bool ending_replay_flag;
static void scene_menu_open(void);
static void util(mes_parameter_list p) {
 unsigned n=par(p,0),a=par(p,1);
 if(getenv("KAWA_UTILTRACE")){
  char line[256];size_t o=0;o+=snprintf(line+o,sizeof(line)-o,"UTIL %u args=%zu",n,vector_length(p));
  for(unsigned i=1;i<vector_length(p)&&i<7;i++){
   struct mes_parameter *q=&vector_A(p,i);
   if(q->type==MES_PARAM_STRING)o+=snprintf(line+o,sizeof(line)-o," \"%s\"",q->str);
   else if(q->expr)o+=snprintf(line+o,sizeof(line)-o," %u",ev(q->expr));
   else o+=snprintf(line+o,sizeof(line)-o," ?");
  }
  note("%s",line);
 }
 switch(n) {
 case 1:st.text[0]=0;break; // 0x4391d0: prepare message window
 case 2:if(!a)st.text[0]=0;break;
 case 3:if(a==1)frontend_present();break; // native show/hide window updates
 case 4: { /* AI.exe util4 (0x43e788): show the staged page, fading in from black
            * when the 4th argument is 1 (e.g. title_bg.gcc reveal) */
   bool fi=vector_length(p)>4&&par(p,4)==1;
   if(fi&&frontend_fadein_start()){st.waiting=3;st.sys[255]=SDL_GetTicks()+600;break;}
   copy_rect(0,0,639,479,1,0,0,0,false);break; }
  case 5:case 6:copy_rect(0,0,639,479,1,0,0,0,false);break;
 case 7:fail("Util 7 transition not yet implemented");break;
 case 8:if(frontend_ppf_start(par(p,1),par(p,2),par(p,3),par(p,4))){st.waiting=3;st.sys[255]=SDL_GetTicks();}break;
 case 24: { /* util24 anim_wait: mode 4 = show staged image (title_bg/CG),
            * 4th arg 1 = fade it in from black (AI.exe 0x43d3e0 family). */
   unsigned mode=par(p,1);
   if(mode==4){
    /* args: (id,4,page,"file",flag): flag at index 4 (index 3 is the string) */
    bool fi=vector_length(p)>4&&par(p,4)==1;
    if(fi&&frontend_fadein_start()){st.waiting=3;st.sys[255]=SDL_GetTicks()+600;break;}
    copy_rect(0,0,639,479,1,0,0,0,false);
   }
   break; }
 case 34:frontend_quake(a);break;
 case 35: /* AI.exe util35 = masked scene transition (mask idx=arg0): reveal the new
           * frame staged on surface 1 over the current display (surface 0), phased per
           * the %02u.msk reveal values; VM pauses ~0.6s while it plays (smoke: instant). */
  {extern bool frontend_xfade_start(unsigned);
   if(frontend_xfade_start(a)){st.waiting=3;st.sys[255]=SDL_GetTicks();}}
  break;
 case 36:case 39:case 47:case 49:break; // Win32 menu/auto-mode controls
 case 37: { // engine title menu (0x43eb2c). START.MES dispatch reads var32[18]:
  //   1=はじめから 2=ロード 3=アルバム(allpic) 4=シーン(scene)
  //   5=サウンド(sound) 6=エンディング(ending)
  // Original order and unlock tests: AI.exe 44b3a0. Sound is always available.
  static const char *titles[6]={"开始游戏","读档","声音","相册","场景","结局"};
  static const unsigned tno[6]={1,2,5,3,4,6};
  bool enabled[6]={true,st.flag[1001]!=0,true,false,false,st.flag[152]!=0};
  for(unsigned k=200;k<320;k++)if(st.flag[k]==1)enabled[3]=true;
  for(unsigned k=400;k<440;k++)if(st.flag[k]==1)enabled[4]=true;
  // KWS saves are independent of the original PC save-exists flag.
  for(unsigned k=0;k<100&&!enabled[1];k++){
   char path[1200];
   snprintf(path,sizeof(path),"%s/slot%u.kws.0",save_dir,k);
   FILE *f=fopen(path,"rb");if(!f){snprintf(path,sizeof(path),"%s/slot%u.kws",save_dir,k);f=fopen(path,"rb");}
   if(f){fclose(f);enabled[1]=true;}
  }
  unsigned c=0;const char *items[6];
  for(unsigned k=0;k<6;k++)if(enabled[k]){items[c]=titles[k];menu_nums[c++]=tno[k];}
  if(getenv("KAWA_FLAGDUMP"))note("TITLE37 en=%d%d%d%d%d%d nums=%d,%d,%d,%d,%d,%d",enabled[0],enabled[1],enabled[2],enabled[3],enabled[4],enabled[5],menu_nums[0],menu_nums[1],menu_nums[2],menu_nums[3],menu_nums[4],menu_nums[5]);
  menu_nums[c]=0;choice_kind=37;set_choices(items,c,2);st.waiting=2;break; }
 case 38:slot_page=0;slot_pending=-1;save_ui_scan();storage_menu(38);break;
 case 40:st.var[9]=0;st.waiting=1;break; // hold splash until advance (OVER3/16/18/SAMPLE)
 case 41: {
  const char *items[16];music_playing=-1;
  for(unsigned k=0;k<16;k++){items[k]=k<14?music_files[k]:k==14?"停止":"返回标题";menu_nums[k]=k;}
  choice_kind=41;set_choices(items,16,2);st.waiting=2;break; }
 case 42: { // ending replay select (AI.exe util42=0x43ebe0): var32[18]=1..19, 戻る=0
  if(getenv("KAWA_FLAGDUMP")){unsigned n=0;for(unsigned k=1;k<=19;k++)if(extras_enabled(42,k))n++;note("EXTRAS42 unlocked=%u/19",n);}
  static char lbl[20][24];unsigned c=0;const char *items[20];
  for(unsigned k=1;k<=19;k++){snprintf(lbl[c],24,"结局%d",k);
   items[c]=lbl[c];menu_nums[c]=k;c++;}
  items[c]="返回";menu_nums[c]=0;c++;
  menu_nums[c]=0;choice_kind=42;set_choices(items,c,2);st.waiting=2;break; }
 case 43:if(getenv("KAWA_FLAGDUMP")){unsigned n=0;for(unsigned p=0;p<8;p++)for(unsigned v=1;v<=5;v++)if(st.flag[401+p*5+(v-1)]==1)n++;note("EXTRAS43 unlocked=%u/40",n);}scene_menu_open();break;
 case 44:if(getenv("KAWA_FLAGDUMP")){unsigned n=0;for(unsigned v=1;v<=123;v++)if(extras_enabled(44,v))n++;note("EXTRAS44 unlocked=%u/123",n);}vm_album_menu();break;
 case 45:st.var[18]=1+(rand()&1);break;
 case 46:st.waiting=1;break; // page-hold until advance (CG album etc.)
 case 48: { // timed wait, arg = 1/20 s ticks (credits pacing: 4000/20 etc.)
  unsigned ticks=par(p,1);
  if(ticks){st.waiting=3;st.sys[255]=SDL_GetTicks()+ticks*AX_TICK_MS;}
  else st.waiting=1;
  break; }
 default:fail("Unimplemented Util %u",n);break;
 }
}
static void anim(mes_parameter_list p) {
 if(vector_length(p)&&vector_A(p,0).type==MES_PARAM_STRING) { animation_load(str(p,0));return; }
 unsigned n=par(p,0);
 if(!ax_control(&animation,n,par(p,1),par(p,2))) {
  fail("Invalid AX control %u (%u,%u)",n,par(p,1),par(p,2));return;
 }
 if(ax_waiting(&animation))st.waiting=4;
}
static void audio_op(mes_parameter_list p) {
 unsigned n=par(p,0),c=par(p,1);
 switch(n) {
 case 0:audio_play(0,true);break;
 case 1:case 2:case 3:case 4:audio_stop(0);break;
 case 5:case 6:break; // volume controls to be exposed by frontend
 case 16:audio_play(1+(c%3),false);break;
 case 17:case 18:case 19:case 20:audio_stop(1+(c%3));break;
 case 21:case 22:break;
 case 32:audio_play(4,false);break;
 case 33:case 34:audio_stop(4);break;
 default:break; // original dispatch has no-op holes
 }
}
static const char *case_text(mes_statement_list list) {struct mes_statement *q;vector_foreach(q,list)if(q->aiw_op==0)return zh_tr(q->TXT.text);return "选择";}
static void menu_exec(struct mes_statement *q) {
 if(!vector_length(q->AIW_MENU_EXEC.exprs)){fail("Empty menu execution");return;}
 unsigned id=ev(vector_A(q->AIW_MENU_EXEC.exprs,0));
 if(id>=16||!menus[id]){fail("Undefined menu %u",id);return;}
 nchoices=0;const char *labels[100];struct aiw_mes_menu_case *c;unsigned oi=0;
 vector_foreach_p(c,menus[id]->AIW_DEF_MENU.cases) {
  ++oi; /* declared ordinal: condition-hidden cases still occupy a number (AI.exe 0x… sets var32[18] to it) */
  if(c->cond&&!ev(c->cond))continue;
  if(nchoices==100){fail("Too many choices");return;}
  if(!vector_length(c->body))continue;
  choices[nchoices]=c;choice_ord[nchoices]=oi;
  choice_locs[nchoices]=menu_locs[id];choice_locs[nchoices].addr=vector_A(c->body,0)->address;
  labels[nchoices]=case_text(c->body);nchoices++;
 }
 if(!nchoices){fail("Menu has no available choices");return;}
 last_menu_addr=q->address;
 note("MENU %s:%x n=%u",st.ip.script,q->address,nchoices);
 choice_kind=2;set_choices(labels,nchoices,2);st.waiting=2;
}
/* Engine scene-replay menu: one event page shows its 5 parts (EVENTxx.MES jumps
 * on var32[18]=1..5); prev/next switch event pages 0..7 (flag 401+page*5+part). */
static void scene_menu_open(void){
 scene_ui_close(); /* replay may change unlock flags even when the page is unchanged */
 static char lbl[8][24];unsigned c=0;const char *items[8];
 for(unsigned k=1;k<=5;k++){snprintf(lbl[c],24,"第%d部分",k);items[c]=lbl[c];menu_nums[c]=k;c++;}
 items[c]="上一事件";menu_nums[c]=EXTRA_PREV;c++;
 items[c]="下一事件";menu_nums[c]=EXTRA_NEXT;c++;
 items[c]="返回";menu_nums[c]=0;c++;
 choice_kind=43;set_choices(items,c,2);st.waiting=2;
 frontend_refresh(); /* page changes otherwise keep the same wait/selection and never redraw */
 note("SCENE page %u",scene_page);
}
void vm_choose(unsigned i) {
 if(st.waiting!=2)return;
 if(save_ui_kind(choice_kind)){
  unsigned kind=choice_kind,v=i<100?menu_nums[i]:~0u;
  if(!save_ui_enabled(kind,v))return;
  if(v>=SLOT_PAGE&&v<SLOT_PAGE+4){slot_page=v-SLOT_PAGE;storage_menu(kind);return;}
  if(v==~0u){slot_pending=-1;st.waiting=kind==38?0:1;return;}
  if(v==SLOT_MEMO){save_ui_edit();return;}
  if(v==SLOT_NO){slot_pending=-1;storage_menu(kind);return;}
  if(v==SLOT_YES){
   if(slot_pending<0||slot_pending>=40)return;
   unsigned slot=(unsigned)slot_pending;slot_pending=-1;
   st.waiting=1;
   bool ok=kind==45?save_state(slot):load_state(slot);
   if(ok){if(kind==45)save_ui_write_memo(slot);return;}
   save_ui_scan();storage_menu(kind);return;
  }
  if(v<40){save_ui_prepare(v);storage_menu(kind);return;}
  return;
 }
 if(choice_kind==37) { // title menu: report the item number START.MES expects
  if(i<8&&menu_nums[i]){st.var[18]=menu_nums[i];st.text[0]=0;st.waiting=0;}
  return;
 }
 if(choice_kind==38) { // load screen: resume the chosen slot or cancel
  unsigned s=i<100?menu_nums[i]:~0u;
  st.text[0]=0;st.waiting=0;
  if(s!=~0u&&!load_state(s))note("LOAD slot %u failed",s);
  return;
 }
 if(choice_kind==41) { // sound test: preview chosen track, keep the menu open; 戻る exits
  unsigned s=i<16?menu_nums[i]:15;
  if(!extras_enabled(41,s))return;
  st.text[0]=0;audio_stop(0);music_playing=-1;
  if(s==15){st.waiting=0;return;}
  if(s<14){music_playing=(int)s;audio_load(0,music_files[s]);audio_play(0,true);note("SOUNDTEST %s",music_files[s]);}
  frontend_refresh();st.waiting=2;return;
 }
 if(choice_kind==42) { // ending replay: var32[18]=ending no (0 = back to title via script tail)
  if(!extras_enabled(42,menu_nums[i]))return;
  unsigned no=i<100?menu_nums[i]:0;
  st.var[18]=no;st.text[0]=0;st.waiting=0;
  ending_replay_flag=(no!=0); /* replay launched from the エンディング menu */
  return;
 }
 if(choice_kind==43) { // scene replay (PC util43): page=event 0..7 x 5 parts
  unsigned s=i<100?menu_nums[i]:0;
  if(s==EXTRA_PREV){if(scene_page>0){scene_page--;scene_menu_open();}return;}
  if(s==EXTRA_NEXT){if(scene_page<7){scene_page++;scene_menu_open();}return;}
  if(!s){st.var[20]=8;st.var[18]=0;st.text[0]=0;st.waiting=0;return;} /* 戻る: SCENE.MES exits */
  if(!(st.flag[401+scene_page*5+(s-1)]==1))return; /* part locked */
  st.var[20]=scene_page;st.var[18]=s;st.text[0]=0;st.waiting=0;
  note("SCENE ev%u part%u",scene_page,s);return;
 }
 if(choice_kind==44) { // CG album page select
  unsigned p=i<100?menu_nums[i]:0;
  if(!extras_enabled(44,p))return;
  if(p==EXTRA_PREV||p==EXTRA_NEXT){st.var[20]+=p==EXTRA_NEXT?1:-1;vm_album_menu();return;}
  st.var[9]=1; /* any pick (incl. 連続再生) views one CG, then ALLPIC.MES returns to the menu;
                * A/Y/B never chain into the next unlocked group (user req 2026-09-08) */
  if(p==EXTRA_PLAY){for(p=1;p<=123&&!extras_enabled(44,p);p++){}if(p>123)return;}
  note("CH44 i=%u p=%u",i,p);
  if(p==0){ // タイトルへ: script chain has no exit path; jump to title engine-side
   note("ALBUM EXIT to title");st.text[0]=0;st.waiting=0;jump("start.mes",0);return;
  }
  st.var[18]=p;st.text[0]=0;st.waiting=0;return;
 }
 if(choice_kind==45||choice_kind==46) { // engine save/load slot pick
  unsigned s=i<100?menu_nums[i]:~0u;
  st.waiting=1;
  if(s==~0u)return;
  if(choice_kind==45){if(!save_state(s))note("SAVEMENU slot %u failed",s);}
  else {if(!load_state(s))note("LOADMENU slot %u failed",s);}
  return;
 }
 if(choice_kind==2&&i<nchoices){st.waiting=0;st.var[18]=choice_ord[i];st.text[0]=0;
 struct mes_statement *q;vector_foreach(q,choices[i]->body) {
  if(q->aiw_op==7)st.var[bound(q->SET_VAR_CONST.var_no,32)]=ev(vector_A(q->SET_VAR_CONST.val_exprs,0));
  else if(q->aiw_op!=0&&q->aiw_op!=0xff)fail("Unimplemented menu body op %02x",q->aiw_op);
 }}
}
unsigned vm_choice_kind(void) { return choice_kind; }
void vm_album_menu(void){
 static char labels[20][24];const char *items[20];unsigned c=0;
 if(st.var[20]>7)st.var[20]=0;
 unsigned first=st.var[20]*16+1,last=first+16;if(last>124)last=124;
 for(unsigned v=first;v<last;v++){snprintf(labels[c],24,"CG %u",v);items[c]=labels[c];menu_nums[c++]=v;}
 const unsigned controls[4]={EXTRA_PREV,EXTRA_NEXT,EXTRA_PLAY,0};
 const char *names[4]={"上一页","下一页","连续播放","返回标题"};
 for(unsigned k=0;k<4;k++){items[c]=names[k];menu_nums[c++]=controls[k];}
 choice_kind=44;set_choices(items,c,2);st.waiting=2;frontend_refresh();
}
unsigned vm_menu_value(unsigned i) { return i<100?menu_nums[i]:0; }
/* true while the current scene is a replay/view instead of live gameplay:
 * CG album photos, scene replays, ending/credits playback. Save/load menus
 * must not be summoned there (user req 2026-09-08). */
static bool in_replay_view(void){
 const char *s=st.ip.script;
 if(!s||!s[0])return false;
 if(!strncasecmp(s,"ALLPIC",6))return true;
 if(!strncasecmp(s,"EVENT",5)&&s[5]>='0'&&s[5]<='9')return true;
 if(is_over_script(s))return ending_replay_flag; /* only エンディング-replay OVERs */
 if(!strncasecmp(s,"CREDITS",7))return true;
 return false;
}
void vm_slot_menu(bool save) {
 /* Keep the dialogue and its continuation intact; other engine/script menus
  * have their own return contracts and must not be overwritten here. */
 if(st.waiting!=1)return;
 /* only during actual gameplay dialogue; never inside replay views
  * (CG/scene/ending/credits) — not even when they sit at a dialogue pause */
 if(in_replay_view())return;
 slot_page=0;slot_pending=-1;save_ui_scan();storage_menu(save?45:46);
}
static void storage_menu(unsigned kind){
 static char labels[15][24];const char *items[15];unsigned c=0;
 if(slot_pending>=0){
  if(kind==45){items[c]="编辑备注";menu_nums[c++]=SLOT_MEMO;}
  items[c]="确定";menu_nums[c++]=SLOT_YES;items[c]="取消";menu_nums[c++]=SLOT_NO;
 }else{
  for(unsigned i=0;i<10;i++){unsigned s=slot_page*10+i;snprintf(labels[c],24,"档位%02u",s+1);items[c]=labels[c];menu_nums[c++]=s;}
  for(unsigned p=0;p<4;p++){snprintf(labels[c],24,"第%u页",p+1);items[c]=labels[c];menu_nums[c++]=SLOT_PAGE+p;}
  items[c]="关闭";menu_nums[c++]=~0u;
 }
 choice_kind=kind;set_choices(items,c,2);st.waiting=2;frontend_refresh();
}
void vm_advance(void) {if(st.waiting==1){st.waiting=0;st.text[0]=0;} }
void vm_step(void) {
 if(st.waiting)return;
 struct script *s=get_script(st.ip.script);if(!s)return;
 if(ending_replay_flag&&!is_over_script(s->name)&&strncasecmp(s->name,"CREDITS",7))
  ending_replay_flag=false; /* replay ended: back to live flow */

 if(st.ip.addr>=s->size||!s->map[st.ip.addr]){fail("No statement at %x",st.ip.addr);return;}
 struct mes_statement *q=s->map[st.ip.addr];unsigned op=q->aiw_op;
 if(++steps>50000000){fail("Instruction budget exceeded");return;}
 if(getenv("KAWA_TRACE"))note("TRACE %s:%x op=%02x",s->name,q->address,op);
 st.ip.addr=q->next_address;
 mes_parameter_list p=q->CALL.params;
 switch(op) {
 case 0:frontend_text(zh_tr(q->TXT.text));break;
 case 1:st.ip.addr=q->JMP.addr;break;
 case 2:util(p);break;
 case 3:jump(str(p,0),0);break;
 case 4: {struct loc l={0};snprintf(l.script,32,"%s",str(p,0));if(get_script(l.script))push(l);break;}
 case 5:case 10:case 12:assign_values(op,q->SET_VAR_CONST.var_no,q->SET_VAR_CONST.val_exprs);break;
 case 6:case 11:case 13:assign_values(op,ev(q->SET_VAR_EXPR.var_expr),q->SET_VAR_EXPR.val_exprs);break;
 case 7:st.var[bound(q->SET_VAR_CONST.var_no,32)]=ev(vector_A(q->SET_VAR_CONST.val_exprs,0));break;
 case 8:case 9:fail("Pointer assignment not implemented");break;
 case 14:savedata_op(false,p);break;
 case 15:savedata_op(true,p);break;
 case 16:if(!ev(q->JZ.expr))st.ip.addr=q->JZ.addr;break;
 case 17:st.proc[bound(ev(q->DEF_PROC.no_expr),NPROC)]=st.ip;st.ip.addr=q->DEF_PROC.skip_addr;break;
 case 18:{unsigned n=bound(par(p,0),NPROC);if(!st.proc[n].script[0])fail("Undefined procedure %u",n);else push(st.proc[n]);break;}
 case 19:{unsigned id=bound(ev(q->AIW_DEF_MENU.expr),16);menus[id]=q;menu_locs[id]=st.ip;st.ip.addr=q->AIW_DEF_MENU.skip_addr;break;}
 case 20:menu_exec(q);break;
 case 21:{char b[32];snprintf(b,sizeof(b),"%u",par(p,0));frontend_text(b);break;}
 case 22:st.color=par(p,0);st.sys[7]=par(p,1);break;
 case 0x20:st.waiting=3;st.sys[255]=SDL_GetTicks()+par(p,0)*16;break;
 case 0x21:st.text[0]=0;break;
 case 0x22:st.waiting=1;st.messages++;note("MESSAGE %u %s:%x",st.messages,s->name,q->address);break;
 case 0x23:draw_image(str(p,0),st.sys[12],vector_length(p)>1?(int)par(p,1):-1,vector_length(p)>2?(int)par(p,2):-1);break;
 case 0x24:case 0x25:case 0x26:copy_rect(par(p,0),par(p,1),par(p,2),par(p,3),par(p,4),par(p,5),par(p,6),par(p,7),op==0x25);break;
 case 0x27:fill_rect(par(p,0),par(p,1),par(p,2),par(p,3),st.sys[12],st.sys[7]);break;
 case 0x28:fail("Surface invert not implemented");break;
 case 0x29:st.sys[7]=par(p,0);break;
 case 0x2a:frontend_present();break;
 case 0x2b:case 0x2c:copy_rect(0,0,639,479,1,0,0,0,false);unsupported++;note("APPROX crossfade %02x",op);break;
 case 0x2d:break;
 case 0x2e:anim(p);break;
 case 0x2f:audio_load(0,str(p,0));break;
 case 0x30:audio_load(1+par(p,1)%3,str(p,0));break;
 case 0x31:audio_load(4,str(p,0));audio_play(4,false);break;
 case 0x32:audio_op(p);break;
 case 0x35:st.message_id=q->AIW_0x35.a;break;
 case 0x37:break; // original consumes an unused dword
 case 0xfe:break;
 case 0xff:ret();break;
 default:fail("Unimplemented statement %02x",op);break;
 }
}
