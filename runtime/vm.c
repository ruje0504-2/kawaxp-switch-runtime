/* GPL-2.0-or-later. KAWAXP AST interpreter; original program addresses in RE.md. */
#include "kawa.h"
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
static void jump(const char *name,uint32_t addr) { struct script *s=get_script(name);if(!s)return;snprintf(st.ip.script,32,"%s",s->name);st.ip.addr=addr; }
static void push(struct loc to) { if(st.depth==NFRAME){fail("Call stack overflow");return;}st.frames[st.depth++]=st.ip;st.ip=to; }
static void ret(void) { if(st.depth)st.ip=st.frames[--st.depth];else st.waiting=98; }
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
 char path[1200];snprintf(path,sizeof(path),"%s/flag0100",save_dir);
 FILE *f=fopen(path,write?"wb":"rb");
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
 }
 fclose(f);note("FLAGBANK %s %s",write?"SAVE":"LOAD",path);
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
static void util(mes_parameter_list p) {
 unsigned n=par(p,0),a=par(p,1);
 switch(n) {
 case 1:st.text[0]=0;break; // 0x4391d0: prepare message window
 case 2:if(!a)st.text[0]=0;break;
 case 3:if(a==1)frontend_present();break; // native show/hide window updates
 case 4:case 5:case 6:copy_rect(0,0,639,479,1,0,0,0,false);break;
 case 7:fail("Util 7 transition not yet implemented");break;
 case 8:copy_rect(par(p,1),par(p,2),par(p,1)+par(p,3)-1,par(p,2)+par(p,4)-1,1,par(p,1),par(p,2),0,true);break;
 case 24: // persistent animation/audio slot bookkeeping, 0x43d3e0
  if(a==0||a==1||a==2||a==3||a==4||a==5||a==6||a==7) { /* frontend state keeps loaded assets; animation slots added separately */ }
  else fail("Util24 mode %u",a);break;
 case 34:if(a) {unsupported++;note("APPROX quake %u",a);}break;
 case 35:copy_rect(0,0,639,479,a,0,0,par(p,2),false);break;
 case 36:case 39:case 47:case 49:break; // Win32 menu/auto-mode controls
 case 37: { // engine title menu (0x43eb2c). START.MES dispatch reads var32[18]:
  //   1=はじめから 2=ロード 3=アルバム(allpic) 4=シーン(scene)
  //   5=サウンド(sound) 6=エンディング(ending)
  // Extras 3..6 appear once any ending was seen (START.MES sets var4[152]).
  static const char *titles[6]={"はじめから","ロード","アルバム","シーン","サウンド","エンディング"};
  static const unsigned tno[6]={1,2,3,4,5,6};
  unsigned c=st.flag[152]?6:2;const char *items[6];
  for(unsigned k=0;k<c;k++){items[k]=titles[k];menu_nums[k]=tno[k];}
  menu_nums[c]=0;choice_kind=37;set_choices(items,c,2);st.waiting=2;break; }
 case 38: { // engine load screen (0x43eb78): list runtime slots that exist, then resume
  static char lbls[48][24];unsigned c=0;const char *items[48];
  for(unsigned s=0;s<100;s++){char p[1200];
   snprintf(p,sizeof(p),"%s/slot%u.kws",save_dir,s);
   FILE *f=fopen(p,"rb");if(!f)continue;fclose(f);
   snprintf(lbls[c],24,"スロット%02u",s);items[c]=lbls[c];menu_nums[c]=s;c++;}
  items[c]="戻る";menu_nums[c]=~0u;c++;
  menu_nums[c]=0;choice_kind=38;set_choices(items,c,2);st.waiting=2;break; }
 case 40:st.var[9]=0;st.waiting=1;break; // hold splash until advance (OVER3/16/18/SAMPLE)
 case 41: { // engine sound test (0x43eb88): pick a track -> preview, stays until 戻る
  static const char *bw[16]={"dead","gemend","h1","h2","hall","hallf","inbi1","inbi2",
                             "kyofu","last","misako","niwa2","or","yokan2","yokan3","yokan"};
  static char lbl[17][24];unsigned c=0;const char *items[17];
  for(unsigned k=0;k<16;k++){snprintf(lbl[c],24,"BGM%d %s",k+1,bw[k]);items[c]=lbl[c];menu_nums[c]=k+1;c++;}
  items[c]="戻る";menu_nums[c]=~0u;c++;
  menu_nums[c]=0;choice_kind=41;set_choices(items,c,2);st.waiting=2;break; }
 case 42: { // ending replay select (0x43eb94): unlocked endings -> var32[18]=1..19, 戻る=0
  static char lbl[20][24];unsigned c=0;const char *items[20];
  for(unsigned k=1;k<=19;k++){if(!st.flag[129+k])continue;snprintf(lbl[c],24,"エンディング%d",k);
   items[c]=lbl[c];menu_nums[c]=k;c++;}
  items[c]="戻る";menu_nums[c]=0;c++;
  menu_nums[c]=0;choice_kind=42;set_choices(items,c,2);st.waiting=2;break; }
 case 43: { // scene replay select (0x43ebe0): var32[20]=0..7 -> event01..08, 戻る=20:8,18:0
  static char lbl[9][24];unsigned c=0;const char *items[9];
  for(unsigned k=0;k<8;k++){snprintf(lbl[c],24,"シーン%d",k+1);items[c]=lbl[c];menu_nums[c]=k;c++;}
  items[c]="戻る";menu_nums[c]=8;c++;
  menu_nums[c]=0;choice_kind=43;set_choices(items,c,2);st.waiting=2;break; }
 case 44: { // CG album page select (0x43ec2c): offer pages whose seen flag var4[199+n] is set
  static char lbl[101][24];unsigned c=0;const char *items[101];
  for(unsigned k=1;k<=123&&c<99;k++){if(!st.flag[199+k])continue;snprintf(lbl[c],24,"CG %d",k);
   items[c]=lbl[c];menu_nums[c]=k;c++;}
  items[c]="タイトルへ";menu_nums[c]=0;c++;
  menu_nums[c]=0;choice_kind=44;set_choices(items,c,2);st.waiting=2;break; }
 case 45:st.var[18]=1+(rand()&1);break;
 case 46:case 48:st.waiting=1;break;
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
static const char *case_text(mes_statement_list list) {struct mes_statement *q;vector_foreach(q,list)if(q->aiw_op==0)return q->TXT.text;return "選択";}
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
void vm_choose(unsigned i) {
 if(st.waiting!=2)return;
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
  unsigned s=i<100?menu_nums[i]:~0u;
  st.text[0]=0;
  if(s==~0u){st.waiting=0;return;}
  static const char *bw[16]={"dead","gemend","h1","h2","hall","hallf","inbi1","inbi2",
                             "kyofu","last","misako","niwa2","or","yokan2","yokan3","yokan"};
  static char lbl[17][24];unsigned c=0;const char *items[17];
  for(unsigned k=0;k<16;k++){snprintf(lbl[c],24,"BGM%d %s",k+1,bw[k]);items[c]=lbl[c];menu_nums[c]=k+1;c++;}
  items[c]="戻る";menu_nums[c]=~0u;c++;
  menu_nums[c]=0;choice_kind=41;
  char fn[64];snprintf(fn,sizeof(fn),"%s.wav",bw[s-1]);
  audio_stop(0);audio_load(0,fn);audio_play(0,true);note("SOUNDTEST %s",fn);
  set_choices(items,c,2);st.waiting=2;return;
 }
 if(choice_kind==42) { // ending replay: var32[18]=ending no (0 = back to title via script tail)
  st.var[18]=i<100?menu_nums[i]:0;st.text[0]=0;st.waiting=0;return;
 }
 if(choice_kind==43) { // scene replay: var32[20]=0..7 event; 8+var18=0 = back
  unsigned s=i<100?menu_nums[i]:8;
  st.var[20]=s;st.var[18]=(s==8)?0:1;st.text[0]=0;st.waiting=0;return;
 }
 if(choice_kind==44) { // CG album page select
  unsigned p=i<100?menu_nums[i]:0;
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
void vm_slot_menu(bool save) {
 /* Keep the dialogue and its continuation intact; other engine/script menus
  * have their own return contracts and must not be overwritten here. */
 if(st.waiting!=1)return;
 /* engine save/load slot list (0..39), mirrors original 40-slot SaveData menu */
 static char lbls[41][24]; unsigned c=0; const char *items[41];
 for (unsigned s=0;s<40;s++) {
  char p[1200]; snprintf(p,sizeof(p),"%s/slot%u.kws",save_dir,s);
  bool exists=false; FILE*f=fopen(p,"rb"); if(f){exists=true;fclose(f);}
  snprintf(lbls[c],24,"スロット%02u%s",s,exists?"":" (空)");
  items[c]=lbls[c]; menu_nums[c]=s; c++;
 }
 items[c]="戻る"; menu_nums[c]=~0u; c++;
 menu_nums[c]=0; choice_kind=save?45:46; set_choices(items,c,2); st.waiting=2;
}
void vm_advance(void) {if(st.waiting==1){st.waiting=0;st.text[0]=0;} }
void vm_step(void) {
 if(st.waiting)return;
 struct script *s=get_script(st.ip.script);if(!s)return;
 if(st.ip.addr>=s->size||!s->map[st.ip.addr]){fail("No statement at %x",st.ip.addr);return;}
 struct mes_statement *q=s->map[st.ip.addr];unsigned op=q->aiw_op;
 if(++steps>50000000){fail("Instruction budget exceeded");return;}
 if(getenv("KAWA_TRACE"))note("TRACE %s:%x op=%02x",s->name,q->address,op);
 st.ip.addr=q->next_address;
 mes_parameter_list p=q->CALL.params;
 switch(op) {
 case 0:frontend_text(q->TXT.text);break;
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
