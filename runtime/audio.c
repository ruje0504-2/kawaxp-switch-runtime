/* GPL-2.0-or-later. Audio: decode .wav (RIFF PCM) natively and .ogg via
 * libsndfile (host) or vorbisfile (Switch, where sndfile is unavailable).
 *
 * Switch multi-core: full-file decode (ogg/vorbis + resample) used to run
 * synchronously on the VM thread at every message/voice boundary, stalling the
 * frame loop for 50-300ms on the A57. Decode now runs on a dedicated worker
 * thread pinned to a free core; audio_load() only enqueues, and a voice starts
 * as soon as its decode lands (want_play). */
#include "kawa.h"
#include <stdlib.h>
#include <string.h>
#ifdef __SWITCH__
#include <switch.h>
#endif
#ifdef __SWITCH__
#include <vorbis/vorbisfile.h>
struct smem { const unsigned char *p; size_t n, pos; };
static size_t vread_s(void*out,size_t sz,size_t n,void*p){struct smem*m=p;size_t tot=sz*n;if(tot>m->n-m->pos)tot=m->n-m->pos;memcpy(out,m->p+m->pos,tot);m->pos+=tot;return sz?tot/sz:0;}
static int vseek_s(void*p,ogg_int64_t off,int whence){struct smem*m=p;long long n=(whence==SEEK_SET?0:whence==SEEK_CUR?(long long)m->pos:(long long)m->n)+off;if(n<0||n>(long long)m->n)return -1;m->pos=(size_t)n;return 0;}
static long vtell_s(void*p){return (long)((struct smem*)p)->pos;}
static ov_callbacks ogg_cb={vread_s,vseek_s,NULL,vtell_s};
/* decode RIFF/WAVE PCM16 or Ogg Vorbis into interleaved float PCM.
 * returns frames decoded, sets *ch *rate; caller frees *out. */
static long switch_decode(const unsigned char *data,size_t size,float **out,int *ch,int *rate){
 *out=NULL;*ch=0;*rate=0;
 if(size>=12&&!memcmp(data,"RIFF",4)&&!memcmp(data+8,"WAVE",4)) {
  /* RIFF chunks */
  size_t i=12;int bps=16,fmtch=1,srate=0;
  const unsigned char *pcm=NULL;size_t pcmn=0;
  while(i+8<=size){unsigned tag=(unsigned)(data[i]|(data[i+1]<<8)|(data[i+2]<<16)|((unsigned)data[i+3]<<24));unsigned n=(unsigned)(data[i+4]|(data[i+5]<<8)|(data[i+6]<<16)|((unsigned)data[i+7]<<24));i+=8;
   if(tag==0x20746d66&&n>=16){fmtch=data[i]|(data[i+1]<<8);*ch=data[i+2]|(data[i+3]<<8);srate=(int)(data[i+4]|(data[i+5]<<8)|(data[i+6]<<16)|((unsigned)data[i+7]<<24));bps=data[i+14]|(data[i+15]<<8);}
   if(tag==0x61746164){pcm=data+i;pcmn=n;} /* 'data' */
   i+=n;if(n&1)i++;
  }
  if(!pcm||!pcmn||fmtch!=1||*ch<1||*ch>8||srate<=0)return -1;
  if(bps==8){float*r=malloc((pcmn/(size_t)*ch)*(*ch)*4);long fr=0;
   for(size_t k=0;k+(*ch)-1<pcmn;k+=(size_t)*ch,fr++)for(int j=0;j<*ch;j++)r[fr*(*ch)+j]=((float)pcm[k+j]-128)/128.0f;*out=r;*rate=srate;return fr;}
  if(bps==16){size_t nf=pcmn/2/(size_t)*ch;float*r=malloc(nf*(*ch)*4);
   for(size_t k=0;k<nf;k++)for(int j=0;j<*ch;j++){short v=(short)(pcm[(k*(size_t)*ch+j)*2]|(pcm[(k*(size_t)*ch+j)*2+1]<<8));r[k*(*ch)+j]=v/32768.0f;}
   *out=r;*rate=srate;return (long)nf;}
  return -1;
 }
 /* Ogg Vorbis */
 OggVorbis_File vf;struct smem mem={data,size,0};
 if(ov_open_callbacks(&mem,&vf,NULL,0,ogg_cb))return -1;
 vorbis_info *vi=ov_info(&vf,-1);if(!vi){ov_clear(&vf);return -1;}
 *ch=vi->channels;*rate=vi->rate;  if(*ch<1||*ch>8){ov_clear(&vf);return -1;}
 long total=(long)ov_pcm_total(&vf,-1);if(total<0||total>44100*60*30){ov_clear(&vf);return -1;}
  size_t alloc_frames=(size_t)total;
  float *r=malloc(alloc_frames*(size_t)(*ch)*4);
  if(!r){ov_clear(&vf);return -1;}
  float **pbuf;int bs;long got=0;int errs=0;
  /* ov_pcm_total can UNDERSTATE the real sample count (last-page granule
   * rounding; S083.OGG actually decodes +10000 frames past it), so read to true
   * EOF and grow the buffer instead of trusting `total` as an upper bound. */
  for(;;){long rd=ov_read_float(&vf,&pbuf,4096,&bs);
   if(rd==0)break;
   if(rd<0){if(++errs>200)break;continue;}
   if((size_t)got+(size_t)rd>alloc_frames){
    size_t nf=alloc_frames;while(nf<(size_t)got+(size_t)rd)nf*=2;
    float *nr=realloc(r,nf*(size_t)(*ch)*4);
    if(!nr){free(r);ov_clear(&vf);return -1;}
    r=nr;alloc_frames=nf;}
   for(long k=0;k<rd;k++)for(int j=0;j<*ch;j++)r[(got+k)*(*ch)+j]=pbuf[j][k];
   got+=rd;}
  ov_clear(&vf);*out=r;return got;
}
#else
#include <sndfile.h>
struct memory_file { const unsigned char *p;sf_count_t n,pos; };
static sf_count_t vlen(void *p){return ((struct memory_file*)p)->n;}
static sf_count_t vseek(sf_count_t off,int whence,void *p){struct memory_file *m=p;sf_count_t n=(whence==SEEK_SET?0:whence==SEEK_CUR?m->pos:m->n)+off;if(n<0||n>m->n)return -1;return m->pos=n;}
static sf_count_t vread(void *out,sf_count_t n,void *p){struct memory_file *m=p;if(n>m->n-m->pos)n=m->n-m->pos;memcpy(out,m->p+m->pos,n);m->pos+=n;return n;}
static sf_count_t vwrite(const void *b,sf_count_t n,void *p){(void)b;(void)n;(void)p;return 0;}
static sf_count_t vtell(void *p){return ((struct memory_file*)p)->pos;}
static SF_VIRTUAL_IO vio={vlen,vseek,vread,vwrite,vtell};
static long host_decode(const unsigned char *data,size_t size,float **out,int *ch,int *rate){
 *out=NULL;*ch=0;*rate=0;
 struct memory_file mem={data,size,0};SF_INFO info={0};SNDFILE *f=sf_open_virtual(&vio,SFM_READ,&info,&mem);
 if(!f||info.frames<=0||info.frames>44100*60*30||info.channels<1||info.channels>8){if(f)sf_close(f);return -1;}
 float *raw=malloc(info.frames*info.channels*sizeof(float));if(!raw){sf_close(f);return -1;}
 sf_count_t got=sf_readf_float(f,raw,info.frames);sf_close(f);
 *out=raw;*ch=info.channels;*rate=info.samplerate;return (long)got;
}
#endif
struct channel {float *data;size_t frames,pos,loop_start,loop_end;bool playing,loop;};
static struct channel channels[5];static SDL_AudioDeviceID device;
static struct archive *arcs[5];
static void mix(void *ud,Uint8 *out,int n) {
 (void)ud;memset(out,0,n);float *dst=(float*)out;unsigned frames=n/8;
 for(int k=0;k<5;k++) {
  struct channel *c=&channels[k];if(!c->playing||!c->data)continue;
  for(unsigned j=0;j<frames;j++) {
   if(c->loop&&c->pos>=c->loop_end)c->pos=c->loop_start;
   if(c->pos>=c->frames){c->playing=false;break;}
   float gain=k==0?0.35f:0.65f;
   dst[2*j]+=c->data[2*c->pos]*gain;dst[2*j+1]+=c->data[2*c->pos+1]*gain;c->pos++;
  }
 }
 for(int i=0;i<n/4;i++)dst[i]=dst[i]>1?1:dst[i]<-1?-1:dst[i];
}
static int audio_forced(void){const char*e=getenv("KAWA_AUDIO");return e&&e[0];}

/* ---- Switch async decode worker -----------------------------------------
 * Every voice/SE/BGM used to be fully decoded (vorbis/RIFF + resample) on the
 * VM thread at the message boundary it plays for - a 50-300ms stall per line.
 * Decode now runs on a dedicated worker thread pinned to a spare core:
 *   - audio_load() only bumps the channel generation and queues the name;
 *   - the worker fetches the archive entry and decodes it on its own core;
 *   - when the decode lands it is installed under the SDL device lock and, if
 *     audio_play() had already been requested (want_play), playback starts.
 * Lock order is always: SDL device lock -> adec_mu (never the reverse). */
#ifdef __SWITCH__
static Thread adec_thr;
static Mutex adec_mu;
static CondVar adec_cv;
static bool adec_run;
static uint32_t adec_gen[5];            /* bumped on every audio_load */
static bool adec_loading[5];            /* current-gen decode not installed yet */
static struct { char name[128]; bool queued; } adec_job[5];
static bool want_play[5];
static bool want_loop[5];
static int adec_core(void){
 const char*e=getenv("KAWA_CORE_AUDIO");
 if(e&&*e&&strcmp(e,"any")){int c=atoi(e);if(c>=0&&c<=3)return c;}
 return 2; /* default: spare core (user observed 1/2 idle; game+HOS on 0/3) */
}
static void *adec_process(int ch,const char *name,struct archive_data **d_out,size_t *frames_out,uint32_t *loop0,uint32_t *loop1){
 *d_out=NULL;*frames_out=0;*loop0=0;*loop1=0;
 struct archive_data *d=NULL;
 if(ch==0||ch==4){if(arcs[ch])d=archive_get(arcs[ch],name);}
 else for(int i=1;i<4&&!d;i++)if(arcs[i]&&archive_get_index(arcs[i],name)>=0)d=archive_get(arcs[i],name);
 if(!d)return NULL;
 float *raw=NULL;int ach=0,arate=0;long got=switch_decode(d->data,d->size,&raw,&ach,&arate);
 if(got<=0||!raw){free(raw);return NULL;}
 SDL_AudioCVT cvt;int ret=SDL_BuildAudioCVT(&cvt,AUDIO_F32SYS,ach,arate,AUDIO_F32SYS,2,44100);
 if(ret<0){free(raw);return NULL;}
 cvt.len=(size_t)got*ach*4;cvt.buf=malloc((size_t)cvt.len*cvt.len_mult);
 if(!cvt.buf){free(raw);return NULL;}
 memcpy(cvt.buf,raw,cvt.len);free(raw);
 if(ret){if(SDL_ConvertAudio(&cvt)){free(cvt.buf);return NULL;}}else cvt.len_cvt=cvt.len;
 *frames_out=cvt.len_cvt/8;
 if(d->meta.loop_start<d->meta.loop_end&&d->meta.loop_end<=*frames_out){*loop0=d->meta.loop_start;*loop1=d->meta.loop_end;}
 *d_out=d;
 return cvt.buf;
}
/* install a finished decode under the device lock; re-checks the generation
 * (a newer load may have been queued while we were decoding). */
static void adec_install(int ch,uint32_t mygen,void *buf,size_t frames,uint32_t loop0,uint32_t loop1,struct archive_data *d){
 SDL_LockAudioDevice(device);
 mutexLock(&adec_mu);
 bool current=adec_gen[ch]==mygen;
 bool start=false;
 if(current){
  struct channel *c=&channels[ch];free(c->data);memset(c,0,sizeof(*c));
  c->data=(float*)buf;c->frames=frames;c->loop_end=frames;
  if(loop0<loop1&&loop1<=frames){c->loop_start=loop0;c->loop_end=loop1;}
  if(want_play[ch]){want_play[ch]=false;start=true;c->loop=want_loop[ch];}
  adec_loading[ch]=false;
 }
 mutexUnlock(&adec_mu);
 if(current&&start){channels[ch].pos=0;channels[ch].playing=true;}
 SDL_UnlockAudioDevice(device);
 if(d)archive_data_release(d);
 if(current)
  note("AUDIO OK ch=%d frames=%lu loop=[%lu,%lu)",ch,(unsigned long)frames,(unsigned long)loop0,(unsigned long)loop1);
 else
  {free(buf);note("AUDIO SKIP stale ch=%d",ch);}
}
static void adec_entry(void *arg){
 (void)arg;
 note("AUDIO worker started on core %d",svcGetCurrentProcessorNumber());
 for(;;){
  int ch=-1;
  mutexLock(&adec_mu);
  for(;;){
   if(!adec_run){mutexUnlock(&adec_mu);return;}
   for(int i=0;i<5;i++)if(adec_job[i].queued){ch=i;break;}
   if(ch>=0)break;
   condvarWait(&adec_cv,&adec_mu);
  }
  char name[128];snprintf(name,sizeof(name),"%s",adec_job[ch].name);
  adec_job[ch].queued=false;
  uint32_t mygen=adec_gen[ch];
  mutexUnlock(&adec_mu);
  struct archive_data *d=NULL;size_t frames=0;uint32_t l0=0,l1=0;
  void *buf=adec_process(ch,name,&d,&frames,&l0,&l1);
  mutexLock(&adec_mu);
  bool current=adec_gen[ch]==mygen;
  mutexUnlock(&adec_mu);
  if(!buf){
   if(d)archive_data_release(d);
   mutexLock(&adec_mu);
   if(current)adec_loading[ch]=false;
   mutexUnlock(&adec_mu);
   note("AUDIO DECODE failed %s",name);
   continue;
  }
  if(!current){free(buf);if(d)archive_data_release(d);continue;} /* superseded */
  adec_install(ch,mygen,buf,frames,l0,l1,d);
 }
}
static void adec_start(void){
 mutexInit(&adec_mu);condvarInit(&adec_cv);adec_run=true;
 int core=adec_core();
 Result rc=threadCreate(&adec_thr,adec_entry,NULL,NULL,0x10000,0x30,core);
 if(rc==0)rc=threadStart(&adec_thr);
 if(rc!=0){adec_run=false;note("AUDIO worker thread failed rc=0x%x",rc);}
}
static void adec_stop(void){
 if(!adec_run)return;
 mutexLock(&adec_mu);adec_run=false;condvarWakeAll(&adec_cv);mutexUnlock(&adec_mu);
 threadWaitForExit(&adec_thr);threadClose(&adec_thr);
 adec_run=false;
}
#endif

void audio_init(void) {
 if(smoke&&!audio_forced())return;
 const char *names[]={"bgm.AWF","effect.AWF","effect2.AWF","effect3.AWF","voice.ARC"};
 for(int i=0;i<5;i++){char p[1200];snprintf(p,sizeof(p),"%s/%s",data_dir,names[i]);arcs[i]=archive_open(p,ARCHIVE_RAW);}
 SDL_AudioSpec spec={.freq=44100,.format=AUDIO_F32SYS,.channels=2,.samples=1024,.callback=mix};
 device=SDL_OpenAudioDevice(NULL,0,&spec,NULL,0);if(device)SDL_PauseAudioDevice(device,0);else note("AUDIO unavailable %s",SDL_GetError());
#ifdef __SWITCH__
 if(device&&!getenv("KAWA_AUDIOSYNC"))adec_start();
#endif
}
void audio_stop(int ch){if(ch<0||ch>=5||!device)return;SDL_LockAudioDevice(device);channels[ch].playing=false;SDL_UnlockAudioDevice(device);}
/* stop every channel now (voice/BGM/SE) and cancel any decode still queued for
 * playback, so replay scenes leave no audio behind when returning to a menu. */
int audio_bgm_dirty; /* set when a ch0 (BGM) load happened since the last replay */
void audio_stop_all(void){
 if(!device)return;
 for(int ch=0;ch<5;ch++){
  SDL_LockAudioDevice(device);channels[ch].playing=false;SDL_UnlockAudioDevice(device);
#ifdef __SWITCH__
  mutexLock(&adec_mu);want_play[ch]=false;adec_loading[ch]=false;mutexUnlock(&adec_mu);
#endif
 }
}
/* stop voice/SE but keep ch0: scene replays never load their own BGM, so ch0
 * still holds the title/menu music and must survive leaving a replay. */
void audio_stop_voice_se(void){
 if(!device)return;
 for(int ch=1;ch<5;ch++){
  SDL_LockAudioDevice(device);channels[ch].playing=false;SDL_UnlockAudioDevice(device);
#ifdef __SWITCH__
  mutexLock(&adec_mu);want_play[ch]=false;adec_loading[ch]=false;mutexUnlock(&adec_mu);
#endif
 }
}
#ifdef __SWITCH__
void audio_load(int ch,const char *name) {
 if(ch<0||ch>=5)return;
 note("AUDIO LOAD ch=%d %s",ch,name);if(ch==0)audio_bgm_dirty=1;if((smoke&&!audio_forced())||!device)return;
 if(!adec_run||getenv("KAWA_AUDIOSYNC")) {
  /* synchronous fallback: worker unavailable or KAWA_AUDIOSYNC=1 */
  audio_stop(ch);struct archive_data *d=NULL;
  if(ch==0||ch==4){if(arcs[ch])d=archive_get(arcs[ch],name);}
  else for(int i=1;i<4&&!d;i++)if(arcs[i]&&archive_get_index(arcs[i],name)>=0)d=archive_get(arcs[i],name);
  if(!d){note("AUDIO MISSING %s",name);return;}
  float *raw=NULL;int ach=0,arate=0;long got=-1;
  got=switch_decode(d->data,d->size,&raw,&ach,&arate);
  if(got<=0||!raw){note("AUDIO DECODE failed %s",name);free(raw);archive_data_release(d);return;}
  SDL_AudioCVT cvt;int ret=SDL_BuildAudioCVT(&cvt,AUDIO_F32SYS,ach,arate,AUDIO_F32SYS,2,44100);
  if(ret<0){free(raw);archive_data_release(d);return;}
  cvt.len=(size_t)got*ach*4;cvt.buf=malloc((size_t)cvt.len*cvt.len_mult);
  if(!cvt.buf){free(raw);archive_data_release(d);return;}
  memcpy(cvt.buf,raw,cvt.len);free(raw);
  if(ret){if(SDL_ConvertAudio(&cvt)){free(cvt.buf);archive_data_release(d);return;}}else cvt.len_cvt=cvt.len;
  SDL_LockAudioDevice(device);struct channel *c=&channels[ch];free(c->data);memset(c,0,sizeof(*c));c->data=(float*)cvt.buf;c->frames=cvt.len_cvt/8;c->loop_end=c->frames;
  if(d->meta.loop_start<d->meta.loop_end&&d->meta.loop_end<=c->frames){c->loop_start=d->meta.loop_start;c->loop_end=d->meta.loop_end;}
  SDL_UnlockAudioDevice(device);archive_data_release(d);
  note("AUDIO OK ch=%d frames=%lu loop=[%lu,%lu)",ch,(unsigned long)c->frames,(unsigned long)c->loop_start,(unsigned long)c->loop_end);
  return;
 }
 /* async: stop current clip now, queue latest-wins decode on the worker */
 audio_stop(ch);
 mutexLock(&adec_mu);
 adec_gen[ch]++;
 snprintf(adec_job[ch].name,sizeof(adec_job[ch].name),"%s",name);
 adec_job[ch].queued=true;
 adec_loading[ch]=true;
 want_play[ch]=false;want_loop[ch]=false;
 mutexUnlock(&adec_mu);
 condvarWakeAll(&adec_cv);
}
void audio_play(int ch,bool loop){
 if(ch<0||ch>=5||!device)return;
 /* If a decode for this channel is still in flight, don't start the previous
  * clip; remember the request and start as soon as the new data lands. */
 mutexLock(&adec_mu);
 if(adec_loading[ch]){want_play[ch]=true;want_loop[ch]=loop;mutexUnlock(&adec_mu);return;}
 mutexUnlock(&adec_mu);
 SDL_LockAudioDevice(device);
 if(channels[ch].data){channels[ch].pos=0;channels[ch].loop=loop;channels[ch].playing=true;}
 SDL_UnlockAudioDevice(device);
}
#else
void audio_load(int ch,const char *name) {
 if(ch<0||ch>=5)return;
 note("AUDIO LOAD ch=%d %s",ch,name);if(ch==0)audio_bgm_dirty=1;if((smoke&&!audio_forced())||!device)return;
 audio_stop(ch);struct archive_data *d=NULL;
 if(ch==0||ch==4){if(arcs[ch])d=archive_get(arcs[ch],name);}
 else for(int i=1;i<4&&!d;i++)if(arcs[i]&&archive_get_index(arcs[i],name)>=0)d=archive_get(arcs[i],name);
 if(!d){note("AUDIO MISSING %s",name);return;}
 float *raw=NULL;int ach=0,arate=0;long got=-1;
 got=host_decode(d->data,d->size,&raw,&ach,&arate);
 if(got<=0||!raw){note("AUDIO DECODE failed %s",name);free(raw);archive_data_release(d);return;}
 SDL_AudioCVT cvt;
 int ret=SDL_BuildAudioCVT(&cvt,AUDIO_F32SYS,ach,arate,AUDIO_F32SYS,2,44100);
 if(ret<0){free(raw);archive_data_release(d);return;}
 cvt.len=(size_t)got*ach*4;cvt.buf=malloc((size_t)cvt.len*cvt.len_mult);
 if(!cvt.buf){free(raw);archive_data_release(d);return;}
 memcpy(cvt.buf,raw,cvt.len);free(raw);
 if(ret) {if(SDL_ConvertAudio(&cvt)){free(cvt.buf);archive_data_release(d);return;}}else cvt.len_cvt=cvt.len;
 SDL_LockAudioDevice(device);struct channel *c=&channels[ch];free(c->data);memset(c,0,sizeof(*c));c->data=(float*)cvt.buf;c->frames=cvt.len_cvt/8;c->loop_end=c->frames;
 if(d->meta.loop_start<d->meta.loop_end&&d->meta.loop_end<=c->frames){c->loop_start=d->meta.loop_start;c->loop_end=d->meta.loop_end;}
 SDL_UnlockAudioDevice(device);archive_data_release(d);
 note("AUDIO OK ch=%d frames=%lu loop=[%lu,%lu)",ch,(unsigned long)c->frames,(unsigned long)c->loop_start,(unsigned long)c->loop_end);
}
void audio_play(int ch,bool loop){if(ch<0||ch>=5||!device)return;SDL_LockAudioDevice(device);channels[ch].pos=0;channels[ch].loop=loop;channels[ch].playing=true;SDL_UnlockAudioDevice(device);}
#endif
void audio_fini(void){
#ifdef __SWITCH__
 adec_stop();
#endif
 if(device)SDL_CloseAudioDevice(device);for(int i=0;i<5;i++){free(channels[i].data);if(arcs[i])archive_close(arcs[i]);}
}
