#include <stdio.h>
#include <string.h>
#include "tinyalsa/asoundlib.h"
int main(){
  int rates[]={44100,48000,32000,16000,8000,22050};
  int chs[]={2,1};
  int periods[]={1024,960,512,256,2048,128};
  for(int r=0;r<6;r++)for(int c=0;c<2;c++)for(int p=0;p<6;p++){
    struct pcm_config cfg; memset(&cfg,0,sizeof cfg);
    cfg.channels=chs[c]; cfg.rate=rates[r]; cfg.format=PCM_FORMAT_S16_LE;
    cfg.period_size=periods[p]; cfg.period_count=4;
    struct pcm* pcm=pcm_open(0,0,PCM_OUT,&cfg);
    if(pcm&&pcm_is_ready(pcm)){
      printf("OK  rate=%d ch=%d period=%d\n",rates[r],chs[c],periods[p]);
      pcm_close(pcm);
    }
    else if(pcm) pcm_close(pcm);
  }
  printf("探测完毕\n");
  return 0;
}
