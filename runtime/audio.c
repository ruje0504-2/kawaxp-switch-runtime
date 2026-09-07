/* GPL-2.0-or-later. Audio: decode .wav (RIFF PCM) natively and .ogg via
 * libsndfile (host) or vorbisfile (Switch, where sndfile is unavailable). */
#include "kawa.h"
#include <stdlib.h>
#include <string.h>
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
 *ch=vi->channels;*rate=vi->rate;
 long total=(long)ov_pcm_total(&vf,-1);if(total<0||total>44100*60*30){ov_clear(&vf);return -1;}
 float *r=malloc((size_t)total*(*ch)*4);
 float **pbuf;int bs;long got=0;
 while(got<total){long rd=ov_read_float(&vf,&pbuf,4096,&bs);
  if(rd==0)break;if(rd<0)continue;
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
void audio_init(void) {
 if(smoke&&!audio_forced())return;
 const char *names[]={"bgm.AWF","effect.AWF","effect2.AWF","effect3.AWF","voice.ARC"};
 for(int i=0;i<5;i++){char p[1200];snprintf(p,sizeof(p),"%s/%s",data_dir,names[i]);arcs[i]=archive_open(p,ARCHIVE_RAW);}
 SDL_AudioSpec spec={.freq=44100,.format=AUDIO_F32SYS,.channels=2,.samples=1024,.callback=mix};
 device=SDL_OpenAudioDevice(NULL,0,&spec,NULL,0);if(device)SDL_PauseAudioDevice(device,0);else note("AUDIO unavailable %s",SDL_GetError());
}
void audio_stop(int ch){if(ch<0||ch>=5||!device)return;SDL_LockAudioDevice(device);channels[ch].playing=false;SDL_UnlockAudioDevice(device);}
void audio_load(int ch,const char *name) {
 if(ch<0||ch>=5)return;
 note("AUDIO LOAD ch=%d %s",ch,name);if((smoke&&!audio_forced())||!device)return;
 audio_stop(ch);struct archive_data *d=NULL;
 if(ch==0||ch==4){if(arcs[ch])d=archive_get(arcs[ch],name);}
 else for(int i=1;i<4&&!d;i++)if(arcs[i]&&archive_get_index(arcs[i],name)>=0)d=archive_get(arcs[i],name);
 if(!d){note("AUDIO MISSING %s",name);return;}
 float *raw=NULL;int ach=0,arate=0;long got=-1;
#ifdef __SWITCH__
 got=switch_decode(d->data,d->size,&raw,&ach,&arate);
#else
 got=host_decode(d->data,d->size,&raw,&ach,&arate);
#endif
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
void audio_fini(void){if(device)SDL_CloseAudioDevice(device);for(int i=0;i<5;i++){free(channels[i].data);if(arcs[i])archive_close(arcs[i]);}}
