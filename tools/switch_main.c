#include <switch.h>
#include <stdio.h>
int probe_main(int argc, char **argv);
int main(int argc, char **argv) {
 consoleInit(NULL);
 padConfigureInput(1,HidNpadStyleSet_NpadStandard);
 PadState pad; padInitializeDefault(&pad);
 puts("KAWAXP RESOURCE DIAGNOSTIC 0.1\nNOT A PLAYABLE GAME PORT\n\nA: validate MES scripts and GCC images\n+: exit\nData: sdmc:/switch/KAWAXP/\n");
 while(appletMainLoop()) {
  padUpdate(&pad);
  u64 down=padGetButtonsDown(&pad);
  if(down & HidNpadButton_Plus) break;
  if(down & HidNpadButton_A) {
   char *mes[]={"probe","mes","sdmc:/switch/KAWAXP/mes.ARC"};
   char *cg[]={"probe","cg","sdmc:/switch/KAWAXP/gcc.ARC"};
   int m=probe_main(3,mes);
   int g=probe_main(3,cg);
   printf("\nMES %s / GCC %s\nThis does not execute game scripts.\n+: exit\n",m?"FAIL":"PASS",g?"FAIL":"PASS");
  }
  consoleUpdate(NULL);
 }
 consoleExit(NULL);
 return 0;
}
