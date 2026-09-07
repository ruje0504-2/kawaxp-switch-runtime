/* KAWAXP compatibility probe; GPL-2.0-or-later, uses libai5. */
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include "ai5/game.h"
#include "ai5/arc.h"
#include "ai5/mes.h"
#include "ai5/cg.h"
#include "nulib/port.h"
#ifdef __SWITCH__
#include <switch.h>
static void pump(void) { consoleUpdate(NULL); }
#else
static void pump(void) {}
#endif
int probe_main(int argc, char **argv) {
 if (argc < 3 || (strcmp(argv[1],"mes") && strcmp(argv[1],"cg"))) { fprintf(stderr,"usage: probe mes|cg archive [entry]\n"); return 2; }
 ai5_set_game("kawarazakike");
 struct archive *a=archive_open(argv[2],ARCHIVE_RAW);
 if (!a) return 1;
 unsigned ok=0,failed=0;
 struct archive_data *d;
 archive_foreach(d,a) {
  if(argc>3 && strcasecmp(argv[3],d->name)) continue;
  if (!archive_data_load(d)) { failed++; continue; }
  if(!strcmp(argv[1],"mes")) {
   mes_statement_list list=vector_initializer;
   if(mes_parse_statements(d->data,d->size,&list)) {
    printf("MES\t%s\t%u\t%zu\n",d->name,d->size,vector_length(list));
    if(argc>3) { struct mes_statement *s; vector_foreach(s,list) { fprintf(stdout,"@%x ",s->address); _aiw_mes_statement_print(s,port_stdout(),0); } }
    struct mes_statement *s; vector_foreach(s,list) mes_statement_free(s);
    vector_destroy(list); ok++;
   } else { fprintf(stderr,"FAILED\t%s\n",d->name); failed++; }
  } else {
   struct cg *c=cg_load_arcdata(d);
   if(c) { printf("CG\t%s\t%u\t%u\t%u\n",d->name,c->metrics.w,c->metrics.h,c->metrics.bpp); cg_free(c);ok++; }
   else {fprintf(stderr,"FAILED\t%s\n",d->name); failed++;}
  }
  archive_data_release(d);
  pump();
 }
 archive_close(a);
 if(!ok && !failed) { fprintf(stderr,"No matching entries\n"); failed++; }
 fprintf(stderr,"ok=%u failed=%u\n",ok,failed);
 return failed ? 1:0;
}

#ifndef __SWITCH__
int main(int argc, char **argv) { return probe_main(argc,argv); }
#endif
