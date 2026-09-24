// InfoNES 系统层 for 快易典 C1 Max (Ingenic X2000, MIPS)
// 显示: /dev/fb2 340x800 32bpp BGRA, DPU旋转90°→800x340横屏
//   display(dx,dy) → native offset (799-dx)*1360 + dy*4
// 声音: tinyalsa → ALSA card0 "mypai" (ES8326 + AW87xxx)
// 输入: /dev/input/event2 触摸(屏上虚拟方向键+AB) + event0 物理键
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdarg.h>
#include <time.h>
#include <signal.h>
#include <vector>
#include <initializer_list>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <linux/fb.h>
#include <linux/input.h>
#include "InfoNES.h"
#include "InfoNES_System.h"
#include "InfoNES_pAPU.h"
#include "../../shared/power_hold.hpp"
extern "C" {
#include "../../launcher/src/typeface.h"
}
#include "tinyalsa/asoundlib.h"

// ---------------- 显示 ----------------
static int fbfd=-1; static uint8_t* fbmem=0; static long frame_sz=0; static int fb_stride=1360;
static struct fb_var_screeninfo vinfo;
#define DISP_W 800
#define DISP_H 340
// NES 视口：铺满高度340，宽=256*340/240≈362，居中
#define VP_W 362
#define VP_H 340
static int VP_X0 = (DISP_W - VP_W)/2;   // 219
static int lutx[VP_W], luty[VP_H];      // display→NES 采样查表

// ---------------- 声音 ----------------
static struct pcm* pcm=0;
static int SND_RATE=44100;
static short mixbuf[1024*2];

// ---------------- 输入 ----------------
static int ev_touch=-1, ev_key=-1, ev_return=-1;
static int tx_min=0,tx_max=0,ty_min=0,ty_max=0;   // 触摸ABS范围
static int cur_x=-1,cur_y=-1,touching=0;
static DWORD padTouch=0,padKeys=0;
static volatile sig_atomic_t g_quit=0;
static keyboard::PowerHold power_key;
static clockid_t input_clock=CLOCK_REALTIME;
static uint64_t power_hint_until=0;
static bool power_notice_visible=false;
static std::vector<uint32_t> power_notice;
static void quit_signal(int){g_quit=1;}
static uint64_t monotonic_us(){timespec t{};clock_gettime(CLOCK_MONOTONIC,&t);return uint64_t(t.tv_sec)*1000000ULL+t.tv_nsec/1000;}
static uint64_t input_clock_ms(){timespec t{};clock_gettime(input_clock,&t);return uint64_t(t.tv_sec)*1000+t.tv_nsec/1000000;}
static void power_action(keyboard::PowerHold::Action action){
  if(action==keyboard::PowerHold::Hint)power_hint_until=monotonic_us()+2500000;
  else if(action==keyboard::PowerHold::Exit)g_quit=1;
}

// NES pad 位: A=1<<0 B=1<<1 Sel=1<<2 Start=1<<3 Up=1<<4 Down=1<<5 Left=1<<6 Right=1<<7
enum { NA=1,NB=2,NSEL=4,NSTART=8,NUP=16,NDOWN=32,NLEFT=64,NRIGHT=128 };

void*InfoNES_MemoryCopy(void*d,const void*s,int n){ return memcpy(d,s,n); }
void*InfoNES_MemorySet(void*d,int c,int n){ return memset(d,c,n); }
void InfoNES_DebugPrint(char*m){ fprintf(stderr,"%s\n",m); }
void InfoNES_MessageBox(char*m,...){ va_list a; va_start(a,m); vfprintf(stderr,m,a); va_end(a); fputc('\n',stderr);}    

// ---- ROM 读取 (标准 InfoNES) ----
int InfoNES_ReadRom(const char* fn){
  FILE*fp=fopen(fn,"rb"); if(!fp) return -1;
  ROM=0;VROM=0;
  if(fread(&NesHeader,sizeof NesHeader,1,fp)!=1 || memcmp(NesHeader.byID,"NES\x1a",4)!=0 || !NesHeader.byRomSize){fclose(fp);return -1;}
  memset(SRAM,0,SRAM_SIZE);
  if((NesHeader.byInfo1&4) && fread(&SRAM[0x1000],512,1,fp)!=1){fclose(fp);return -1;}
  size_t rn=NesHeader.byRomSize*0x4000, vn=NesHeader.byVRomSize*0x2000;
  ROM=(BYTE*)malloc(rn);
  if(!ROM || fread(ROM,1,rn,fp)!=rn){free(ROM);ROM=0;fclose(fp);return -1;}
  if(vn){VROM=(BYTE*)malloc(vn);if(!VROM||fread(VROM,1,vn,fp)!=vn){free(ROM);free(VROM);ROM=VROM=0;fclose(fp);return -1;}}
  fclose(fp); return 0;
}
void InfoNES_ReleaseRom(){ if(ROM){free(ROM);ROM=0;} if(VROM){free(VROM);VROM=0;} }

// ---- 显示 ----
static inline uint32_t rgb(int r,int g,int b){ return 0xFF000000u|((uint32_t)r<<16)|((uint32_t)g<<8)|(uint32_t)b; }
// ---- 手柄绘制辅助（写显示坐标，含旋转，画进3帧）----
static void px(int dx,int dy,uint32_t c){
  if(dx<0||dx>=DISP_W||dy<0||dy>=DISP_H) return;
  int nf=3; long ms=frame_sz*3; if(ms) nf=ms/frame_sz; if(nf<1)nf=1; if(nf>3)nf=3;
  for(int f=0;f<nf;f++) *(uint32_t*)(fbmem+(long)f*frame_sz+(long)(799-dx)*fb_stride+dy*4)=c;
}
static void fillrect(int x,int y,int w,int h,uint32_t c){ for(int j=0;j<h;j++)for(int i=0;i<w;i++) px(x+i,y+j,c); }
static void fillcirc(int cx,int cy,int r,uint32_t c){ for(int y=-r;y<=r;y++)for(int x=-r;x<=r;x++) if(x*x+y*y<=r*r) px(cx+x,cy+y,c); }
static void draw_controls(){
  fillrect(0,0,DISP_W,DISP_H,rgb(24,24,32));  // 深色背景
  uint32_t gray=rgb(90,90,100), red=rgb(210,50,50), yel=rgb(220,190,40), dk=rgb(50,50,60);
  // D-pad 十字(左边距)
  fillrect(40,155,130,50,gray);   // 横
  fillrect(80,110,50,140,gray);   // 竖
  fillcirc(105,180,14,dk);        // 中心
  // A(红)/B(黄) 右边距
  fillcirc(720,150,48,red);
  fillcirc(630,215,48,yel);
  // Start/Select(底部小圆)
  fillcirc(700,305,24,gray);
  fillcirc(610,305,24,gray);
}
static void update_power_notice(){
  const bool wanted=monotonic_us()<power_hint_until;
  if(wanted==power_notice_visible)return;
  if(wanted){for(int y=0;y<52;y++)for(int x=0;x<200;x++)px(10+x,10+y,power_notice[y*200+x]);}
  else fillrect(10,10,200,52,rgb(24,24,32));
  power_notice_visible=wanted;
}

static int fb_init(){
  fbfd=open("/dev/fb2",O_RDWR); if(fbfd<0){perror("fb2");return -1;}
  struct fb_fix_screeninfo fix;
  ioctl(fbfd,FBIOGET_VSCREENINFO,&vinfo);
  ioctl(fbfd,FBIOGET_FSCREENINFO,&fix);
  fb_stride=fix.line_length;                 // 1360
  frame_sz=(long)fb_stride*vinfo.yres;       // 1088000
  long map=fix.smem_len?fix.smem_len:frame_sz*3;
  fbmem=(uint8_t*)mmap(0,map,PROT_READ|PROT_WRITE,MAP_SHARED,fbfd,0);
  if(fbmem==MAP_FAILED){perror("mmap");return -1;}
  // 尝试把显示 pan 到 frame0，之后只画 frame0
  vinfo.xoffset=0; vinfo.yoffset=0;
  ioctl(fbfd,FBIOPAN_DISPLAY,&vinfo);
  memset(fbmem,0,map);   // 全屏清黑(3帧)
  power_notice.assign(200*52,0xff18252f);
  for(int x=0;x<200;x++)for(int y=0;y<52;y++)if(x==0||y==0||x==199||y==51)power_notice[y*200+x]=0xff526a76;
  const char *root=getenv("C1_APPS_ROOT");char font_path[512];snprintf(font_path,sizeof font_path,"%s/shared/NotoSansSC-Regular.ttf",root?root:"/storage/apps/current");
  if(typeface_open(font_path)){const char *lines[]={"长按五秒","电源键退出"};for(int n=0;n<2;n++){int w=typeface_width(lines[n],15);typeface_draw(power_notice.data(),200,52,(200-w)/2,5+n*22,lines[n],0xffd8eceb,15);}typeface_close();}
  draw_controls();       // 画可见手柄
  // 采样查表
  for(int i=0;i<VP_W;i++) lutx[i]=i*256/VP_W;      // 0..255
  for(int j=0;j<VP_H;j++) luty[j]=j*240/VP_H;      // 0..239
  return 0;
}
void InfoNES_LoadFrame(){
  // 当前显示帧基址（若pan生效则yoffset=0）
  long base=(long)vinfo.yoffset*fb_stride;
  WORD* wf=WorkFrame;
  for(int dy=0; dy<VP_H; dy++){
    int ny=luty[dy]; WORD* line=&wf[ny*256];
    for(int i=0;i<VP_W;i++){
      int dx=VP_X0+i;
      WORD p=line[lutx[i]];                 // RGB565
      uint32_t r=(p>>11)&0x1F, g=(p>>5)&0x3F, b=p&0x1F;
      uint32_t c=0xFF000000u | ((r<<3)<<16) | ((g<<2)<<8) | (b<<3);
      *(uint32_t*)(fbmem+base+(long)(799-dx)*fb_stride+dy*4)=c;
    }
  }
}

// ---- 声音 ----
void InfoNES_SoundInit(){}
int InfoNES_SoundOpen(int samples_per_sync,int rate){
  SND_RATE=rate;
  struct pcm_config cfg; memset(&cfg,0,sizeof cfg);
  cfg.channels=2; cfg.rate=rate; cfg.format=PCM_FORMAT_S16_LE;
  cfg.period_size=1024; cfg.period_count=8;
  pcm=pcm_open(0,0,PCM_OUT,&cfg);
  if(!pcm||!pcm_is_ready(pcm)){ fprintf(stderr,"pcm_open失败: %s\n",pcm?pcm_get_error(pcm):"null"); pcm=0; return 0;}
  fprintf(stderr,"PCM opened %dHz\n",rate);
  return 1;
}
void InfoNES_SoundClose(){ if(pcm){pcm_close(pcm);pcm=0;} }
void InfoNES_SoundOutput(int samples,BYTE*w1,BYTE*w2,BYTE*w3,BYTE*w4,BYTE*w5){
  if(!pcm) return;
  if(samples>1024) samples=1024;
  for(int i=0;i<samples;i++){
    int m=(int)w1[i]+w2[i]+w3[i]+w4[i]+w5[i]; // 0..1275
    int v=(m-640)*48;                          // 居中放大
    if(v>32767)v=32767; if(v<-32768)v=-32768;
    mixbuf[i*2]=(short)v; mixbuf[i*2+1]=(short)v;  // L=R
  }
  pcm_writei(pcm,mixbuf,samples);
}

// ---- 输入 ----
static void input_init(){
  ev_touch=open("/dev/input/event2",O_RDONLY|O_NONBLOCK);
  ev_key=open("/dev/input/event0",O_RDONLY|O_NONBLOCK);
  ev_return=open("/dev/input/event1",O_RDONLY|O_NONBLOCK);
  bool monotonic=true;clockid_t wanted=CLOCK_MONOTONIC;
#ifdef EVIOCSCLOCKID
  for(int fd:{ev_key,ev_return})if(fd>=0&&ioctl(fd,EVIOCSCLOCKID,&wanted)<0)monotonic=false;
  if(!monotonic){wanted=CLOCK_REALTIME;for(int fd:{ev_key,ev_return})if(fd>=0)ioctl(fd,EVIOCSCLOCKID,&wanted);}
#else
  monotonic=false;
#endif
  input_clock=monotonic?CLOCK_MONOTONIC:CLOCK_REALTIME;
  if(ev_touch>=0){
    struct input_absinfo ai;
    if(ioctl(ev_touch,EVIOCGABS(ABS_X),&ai)==0){tx_min=ai.minimum;tx_max=ai.maximum;}
    if(ioctl(ev_touch,EVIOCGABS(ABS_Y),&ai)==0){ty_min=ai.minimum;ty_max=ai.maximum;}
    if(tx_max==0){ if(ioctl(ev_touch,EVIOCGABS(ABS_MT_POSITION_X),&ai)==0){tx_min=ai.minimum;tx_max=ai.maximum;}
                   if(ioctl(ev_touch,EVIOCGABS(ABS_MT_POSITION_Y),&ai)==0){ty_min=ai.minimum;ty_max=ai.maximum;} }
    fprintf(stderr,"touch ABS x[%d,%d] y[%d,%d]\n",tx_min,tx_max,ty_min,ty_max);
  }
}
// 触摸原始坐标 → 横屏display坐标(0..799,0..339)。触摸面板通常是原生340x800方向。
static void map_touch(int rx,int ry,int*dx,int*dy){
  // 先归一化到原生 nx(0..339) ny(0..799)
  int nx = tx_max>tx_min ? (rx-tx_min)*340/(tx_max-tx_min) : rx;
  int ny = ty_max>ty_min ? (ry-ty_min)*800/(ty_max-ty_min) : ry;
  // native(nx,ny) → display(dx,dy): nx=dy, ny=799-dx  ⇒ dx=799-ny, dy=nx
  *dx=799-ny; *dy=nx;
  if(*dx<0)*dx=0; if(*dx>799)*dx=799; if(*dy<0)*dy=0; if(*dy>339)*dy=339;
}
// 屏上虚拟按键区域(横屏坐标)：左侧方向键，右侧AB
static DWORD region_to_pad(int dx,int dy){
  DWORD p=0;
  // 左上角退出
  // D-pad 方形区(允许对角)
  if(dx>=35 && dx<=175 && dy>=105 && dy<=255){
    int ox=dx-105, oy=dy-180;
    if(oy<-22) p|=NUP; else if(oy>22) p|=NDOWN;
    if(ox<-22) p|=NLEFT; else if(ox>22) p|=NRIGHT;
  }
  // A / B
  if((dx-720)*(dx-720)+(dy-150)*(dy-150) < 50*50) p|=NA;
  if((dx-630)*(dx-630)+(dy-215)*(dy-215) < 50*50) p|=NB;
  // Start / Select
  if((dx-700)*(dx-700)+(dy-305)*(dy-305) < 28*28) p|=NSTART;
  if((dx-610)*(dx-610)+(dy-305)*(dy-305) < 28*28) p|=NSEL;
  return p;
}
static void poll_touch(){
  struct input_event e; 
  while(ev_touch>=0 && read(ev_touch,&e,sizeof e)==sizeof e){
    if(e.type==EV_ABS){
      if(e.code==ABS_MT_POSITION_X||e.code==ABS_X) cur_x=e.value;
      else if(e.code==ABS_MT_POSITION_Y||e.code==ABS_Y) cur_y=e.value;
      else if(e.code==ABS_MT_TRACKING_ID) touching=(e.value>=0);
    } else if(e.type==EV_KEY && e.code==BTN_TOUCH){ touching=e.value; }
    else if(e.type==EV_SYN){
      if(touching && cur_x>=0){ int dx,dy; map_touch(cur_x,cur_y,&dx,&dy); padTouch=region_to_pad(dx,dy);}    
      else padTouch=0;
    }
  }
}
void InfoNES_PadState(DWORD*p1,DWORD*p2,DWORD*sys){
  poll_touch();
  struct input_event e;
  while(ev_key>=0&&read(ev_key,&e,sizeof e)==sizeof e){
    if(e.type==EV_SYN&&e.code==SYN_DROPPED){power_key.reset();padKeys=0;continue;}
    if(e.type!=EV_KEY)continue;DWORD bit=0;
    if(e.code==KEY_POWER){power_action(power_key.event(e.value,uint64_t(e.time.tv_sec)*1000+e.time.tv_usec/1000));continue;}
    switch(e.code){case KEY_W:bit=NUP;break;case KEY_S:bit=NDOWN;break;case KEY_A:bit=NLEFT;break;case KEY_D:bit=NRIGHT;break;case KEY_J:bit=NA;break;case KEY_K:bit=NB;break;case KEY_Q:bit=NSEL;break;case KEY_E:bit=NSTART;break;default:break;}
    if(e.value)padKeys|=bit;else padKeys&=~bit;
  }
  while(ev_return>=0&&read(ev_return,&e,sizeof e)==sizeof e){
    if(e.type==EV_SYN&&e.code==SYN_DROPPED){power_key.reset();padKeys=0;continue;}
    if(e.type!=EV_KEY)continue;
    if(e.code==KEY_ENTER){if(e.value)padKeys|=NSTART;else padKeys&=~NSTART;}
    else if(e.code==KEY_POWER)power_action(power_key.event(e.value,uint64_t(e.time.tv_sec)*1000+e.time.tv_usec/1000));
  }
  power_action(power_key.tick(input_clock_ms()));
  update_power_notice();
  *p1=padTouch|padKeys; *p2=0;
  *sys = g_quit ? PAD_SYS_QUIT : 0;
}

// ---- 帧同步 ~60fps ----
static long last_us=0;
static long now_us(){ struct timespec t; clock_gettime(CLOCK_MONOTONIC,&t); return t.tv_sec*1000000L+t.tv_nsec/1000; }
void InfoNES_Wait(){
  if(pcm) return;               // 有声音时用音频节流，跳过
  long target=16666; long n=now_us();
  if(last_us){ long d=n-last_us; if(d<target) usleep(target-d); }
  last_us=now_us();
}
int InfoNES_Menu(){ return 0; }  // 直接进游戏

int main(int argc,char**argv){
  signal(SIGTERM,quit_signal);signal(SIGINT,quit_signal);
  if(argc<2){ fprintf(stderr,"用法: %s rom.nes\n",argv[0]); return 1; }
  if(fb_init()<0) return 1;
  input_init();
  fprintf(stderr,"加载 ROM: %s\n",argv[1]);
  if(InfoNES_Load(argv[1])!=0){ fprintf(stderr,"ROM加载失败\n"); return 1; }
  fprintf(stderr,"开始模拟\n");
  InfoNES_Cycle();       // 主模拟循环
  InfoNES_Fin();
  if(fbmem) munmap(fbmem,frame_sz*3);
  return 0;
}
WORD NesPalette[ 64 ] =
{
  0x39ce, 0x1071, 0x0015, 0x2013, 0x440e, 0x5402, 0x5000, 0x3c20,
  0x20a0, 0x0100, 0x0140, 0x00e2, 0x0ceb, 0x0000, 0x0000, 0x0000,
  0x5ef7, 0x01dd, 0x10fd, 0x401e, 0x5c17, 0x700b, 0x6ca0, 0x6521,
  0x45c0, 0x0240, 0x02a0, 0x0247, 0x0211, 0x0000, 0x0000, 0x0000,
  0x7fff, 0x1eff, 0x2e5f, 0x223f, 0x79ff, 0x7dd6, 0x7dcc, 0x7e67,
  0x7ae7, 0x4342, 0x2769, 0x2ff3, 0x03bb, 0x0000, 0x0000, 0x0000,
  0x7fff, 0x579f, 0x635f, 0x6b3f, 0x7f1f, 0x7f1b, 0x7ef6, 0x7f75,
  0x7f94, 0x73f4, 0x57d7, 0x5bf9, 0x4ffe, 0x0000, 0x0000, 0x0000
};
