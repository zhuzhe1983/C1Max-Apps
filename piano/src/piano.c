// piano.c — 触摸屏钢琴 for 快易典 C1 Max (Ingenic X2000, mipsel, Linux 4.4)
//
// 显示 : /dev/fb2 原生 340x800 32bpp BGRA stride=1360, 3 缓冲(每帧 1088000B)。
//        DPU 旋转 90° 显示 800x340 横屏。横屏 display(dx,dy) → 原生偏移 (799-dx)*1360 + dy*4。
// 触摸 : /dev/input/event2 (cst3xx)。实测此固件仅单点：BTN_TOUCH + ABS_X + ABS_Y + ABS_PRESSURE
//        (无 ABS_MT_*)。原生坐标 tx=ABS_X∈[0,339], ty=ABS_Y∈[0,799]，映射同显示旋转：
//        dx = 799 - ty, dy = tx。
// 声音 : ALSA card0 dev0 "mypai"(ES8326 codec + AW87xxx PA)，S16_LE 44100 stereo。
//        出声前须打开功放/喇叭：aw87xxx profile=Music, SPKPA_L/R=Switch；保留当前音量。
//        (由 run.sh 用 amixer 设置；本程序也用 tinyalsa mixer 兜底设置一次)。
// 合成 : 每键一个正弦振荡器 + AR 包络，单线程 pcm_writei 定拍。虽然输入是单点，
//        合成器是复音的(释放中的音会继续响)，所以快速滑奏/断奏听感自然。
//
// 编译 : mipsel-linux-gnu-gcc -static -O2 (见 build.sh)

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdint.h>
#include <time.h>
#include "audio_client.h"
#include "songs.h"
#include <signal.h>
#include <errno.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <linux/fb.h>
#include <linux/input.h>

#include "tinyalsa/pcm.h"
#include "tinyalsa/mixer.h"
#include "../../launcher/src/font8x8.h"

// ---------------- 显示 ----------------
#define DISP_W 800
#define DISP_H 340
static uint8_t *g_fb = NULL;      // mmap 基址
static long g_frame_sz = 0;       // 单帧字节
static int  g_nframe = 1;         // 缓冲帧数
static int  g_stride = 1360;
static uint32_t *g_back = NULL;   // 横屏 backbuffer 800*340

static inline uint32_t argb(uint8_t r, uint8_t g, uint8_t b){
    return 0xFF000000u | ((uint32_t)r<<16) | ((uint32_t)g<<8) | b;
}
// 把 backbuffer 的横屏矩形 [x0,x1)×[y0,y1) blit 到 fb2 所有帧(旋转映射)
static void blit_rect(int x0,int y0,int x1,int y1){
    if(x0<0)x0=0; if(y0<0)y0=0; if(x1>DISP_W)x1=DISP_W; if(y1>DISP_H)y1=DISP_H;
    for(int f=0; f<g_nframe; f++){
        uint8_t *fb = g_fb + (long)f*g_frame_sz;
        for(int dx=x0; dx<x1; dx++){
            long col = (long)(799-dx)*g_stride;     // 原生行基址 (native ny = 799-dx)
            const uint32_t *src = g_back + (long)dx; // backbuffer 列(以 dx 为行)
            for(int dy=y0; dy<y1; dy++){
                *(uint32_t*)(fb + col + (long)dy*4) = src[(long)dy*DISP_W];
            }
        }
    }
}
static inline void bb_set(int dx,int dy,uint32_t c){ g_back[(long)dy*DISP_W + dx] = c; }

// ---------------- 键盘布局 ----------------
#define KEY_TOP 76
#define NKEYS 24
// 24 个键 = 2 个八度 C4..B5：14 白键 + 10 黑键
// 白键 MIDI: 60 62 64 65 67 69 71  72 74 76 77 79 81 83
// 黑键 MIDI: 61 63    66 68 70     73 75    78 80 82
#define NWHITE 14
static const int white_midi[NWHITE] = {60,62,64,65,67,69,71, 72,74,76,77,79,81,83};
// 黑键：挂在哪个白键索引之后(该白键右边缘处居中)
static const int black_after[10] = {0,1, 3,4,5, 7,8, 10,11,12};
static const int black_midi[10]  = {61,63,66,68,70, 73,75,78,80,82};

typedef struct {
    int   is_black;
    int   x, y, w, h;   // 横屏 display 矩形
    int   midi;
    float freq;
} Key;
static Key g_keys[NKEYS];   // [0..NWHITE) 白键, [NWHITE..) 黑键
static volatile int g_gate[NKEYS]; // 绘制和合成共用真实按下状态

static float midi_freq(int m){ return 440.0f * powf(2.0f, (m-69)/12.0f); }

static void layout_keys(void){
    int ww = DISP_W / NWHITE;           // 白键宽 ≈57
    int used = ww*NWHITE;
    int x0 = (DISP_W - used)/2;          // 居中留边
    for(int i=0;i<NWHITE;i++){
        Key *k=&g_keys[i];
        k->is_black=0; k->x=x0+i*ww; k->y=KEY_TOP; k->w=ww; k->h=DISP_H-KEY_TOP;
        k->midi=white_midi[i]; k->freq=midi_freq(k->midi);
    }
    int bw = ww*2/3, bh = (DISP_H-KEY_TOP)*3/5;    // 黑键更窄更短
    for(int j=0;j<10;j++){
        Key *k=&g_keys[NWHITE+j];
        int wa = black_after[j];
        int cx = g_keys[wa].x + ww;      // 两白键交界
        k->is_black=1; k->x=cx-bw/2; k->y=KEY_TOP; k->w=bw; k->h=bh;
        k->midi=black_midi[j]; k->freq=midi_freq(k->midi);
    }
}
// 命中测试：黑键在上，先测黑键
static int hit_key(int dx,int dy){
    for(int j=NWHITE;j<NKEYS;j++){
        Key*k=&g_keys[j];
        if(dx>=k->x&&dx<k->x+k->w&&dy>=k->y&&dy<k->y+k->h) return j;
    }
    for(int i=0;i<NWHITE;i++){
        Key*k=&g_keys[i];
        if(dx>=k->x&&dx<k->x+k->w&&dy>=k->y&&dy<k->y+k->h) return i;
    }
    return -1;
}
// 只画 backbuffer；提交必须在白键、黑键两层合成完成之后。
static void paint_key(int idx,int pressed){
    Key*k=&g_keys[idx];
    uint32_t fill, border=argb(20,20,20);
    if(k->is_black) fill = pressed? argb(80,120,255) : argb(24,24,28);
    else            fill = pressed? argb(150,180,255) : argb(248,248,250);
    for(int dy=k->y; dy<k->y+k->h; dy++)
        for(int dx=k->x; dx<k->x+k->w; dx++){
            int edge = (dx==k->x||dx==k->x+k->w-1||dy==k->y||dy==k->y+k->h-1);
            bb_set(dx,dy, edge? border : fill);
        }
}
static void draw_key(int idx,int pressed){
    if(idx<0||idx>=NKEYS) return;
    Key*k=&g_keys[idx];
    int x0=k->x,y0=k->y,x1=k->x+k->w,y1=k->y+k->h;
    paint_key(idx,pressed);
    if(!k->is_black){
        // 白键的矩形位于黑键下面。恢复相交黑键，并保留物理键/触摸的 gate 状态。
        for(int j=NWHITE;j<NKEYS;j++){
            Key*b=&g_keys[j];
            if(k->x<b->x+b->w&&b->x<k->x+k->w&&k->y<b->y+b->h&&b->y<k->y+k->h){
                paint_key(j,g_gate[j]);
                if(b->x<x0)x0=b->x; if(b->y<y0)y0=b->y;
                if(b->x+b->w>x1)x1=b->x+b->w; if(b->y+b->h>y1)y1=b->y+b->h;
            }
        }
    }
    blit_rect(x0,y0,x1,y1);
}
static void draw_toolbar(void);
static void draw_all(void){
    // 底色
    for(int y=0;y<DISP_H;y++) for(int x=0;x<DISP_W;x++) bb_set(x,y,argb(10,10,14));
    for(int i=0;i<NWHITE;i++) paint_key(i,g_gate[i]); // 先白键
    for(int j=NWHITE;j<NKEYS;j++) paint_key(j,g_gate[j]); // 黑键覆盖
    draw_toolbar();
    blit_rect(0,0,DISP_W,DISP_H);
}

// 不打开 framebuffer/input/ALSA；验证增量重绘与完整合成一致、三帧旋转写入一致。
static int verify_render(uint32_t*reference){
    for(int f=0;f<g_nframe;f++)for(int y=0;y<DISP_H;y++)for(int x=0;x<DISP_W;x++){
        uint32_t actual=*(uint32_t*)(g_fb+(long)f*g_frame_sz+(long)(799-x)*g_stride+(long)y*4);
        if(actual!=g_back[(long)y*DISP_W+x])return 0;
    }
    memcpy(reference,g_back,DISP_W*DISP_H*sizeof(*g_back));
    draw_all();
    if(memcmp(reference,g_back,DISP_W*DISP_H*sizeof(*g_back)))return 0;
    for(int i=0;i<NKEYS;i++){
        Key*k=&g_keys[i];
        int x=k->x+k->w/2,y=k->is_black?k->y+k->h/2:300;
        uint32_t expected=k->is_black?(g_gate[i]?argb(80,120,255):argb(24,24,28)):
                                        (g_gate[i]?argb(150,180,255):argb(248,248,250));
        if(g_back[(long)y*DISP_W+x]!=expected)return 0;
    }
    return 1;
}
static int render_test(void){
    g_stride=1360;g_frame_sz=(long)g_stride*800;g_nframe=3;
    g_back=calloc(DISP_W*DISP_H,sizeof(*g_back));
    g_fb=calloc(g_nframe,g_frame_sz);
    uint32_t*reference=malloc(DISP_W*DISP_H*sizeof(*g_back));
    int ok=g_back&&g_fb&&reference;
    if(ok){
        layout_keys();draw_all();ok=verify_render(reference);
        // 按下/释放每个键，包括第一组键。
        for(int i=0;ok&&i<NKEYS;i++)for(int pressed=1;ok&&pressed>=0;pressed--){
            g_gate[i]=pressed;draw_key(i,pressed);ok=verify_render(reference);
        }
        // 保持全部黑键按下，再依次按放白键，验证重绘不清除黑键高亮。
        for(int i=NWHITE;ok&&i<NKEYS;i++){
            g_gate[i]=1;draw_key(i,1);ok=verify_render(reference);
        }
        for(int i=0;ok&&i<NWHITE;i++)for(int pressed=1;ok&&pressed>=0;pressed--){
            g_gate[i]=pressed;draw_key(i,pressed);ok=verify_render(reference);
        }
    }
    free(reference);free(g_back);free(g_fb);g_back=NULL;g_fb=NULL;
    fprintf(ok?stdout:stderr,"piano render test: %s (24 keys, held black keys, 3 rotated frames)\n",ok?"PASS":"FAIL");
    return ok?0:1;
}

// ---------------- 合成 ----------------
#define SR       44100
#define MAXPER   2048            // 渲染缓冲上限(帧)
static int PERIOD = 1024;        // 实际 period(运行时由 pcm 协商决定)
#define SINLEN   1024
static float g_sin[SINLEN];
// 每键：门(gate)、包络 env、相位 phase
static float g_env[NKEYS];
static float g_phase[NKEYS];
static float g_step[NKEYS];       // freq/SR
// AR 包络系数（每采样线性增量）
#define ATK (1.0f/(0.004f*SR))    // ~4ms 起音
#define REL (1.0f/(0.180f*SR))    // ~180ms 释放

static int16_t g_buf[MAXPER*2];

static void render_period(void){
    for(int n=0;n<PERIOD;n++){
        float mix=0.0f;
        for(int k=0;k<NKEYS;k++){
            int g=g_gate[k];
            if(!g && g_env[k]<=0.0f) continue;
            // 包络
            if(g){ g_env[k]+=ATK; if(g_env[k]>1.0f)g_env[k]=1.0f; }
            else { g_env[k]-=REL; if(g_env[k]<0.0f)g_env[k]=0.0f; }
            // 振荡
            g_phase[k]+=g_step[k]; if(g_phase[k]>=1.0f)g_phase[k]-=1.0f;
            float s=g_sin[(int)(g_phase[k]*SINLEN)&(SINLEN-1)];
            mix += s*g_env[k];
        }
        mix *= 0.18f;                       // 总音量/防削波
        if(mix>1.0f)mix=1.0f; else if(mix<-1.0f)mix=-1.0f;
        int16_t v=(int16_t)(mix*30000.0f);
        g_buf[2*n]=v; g_buf[2*n+1]=v;
    }
}

// ---------------- 混音器兜底使能功放 ----------------
static void mixer_set_enum(struct mixer*m,const char*name,unsigned int idx){
    struct mixer_ctl*c=mixer_get_ctl_by_name(m,name);
    if(c) mixer_ctl_set_value(c,0,idx);
}
static void enable_speaker(void){
    struct mixer*m=mixer_open(0);
    if(!m) return;
    mixer_set_enum(m,"aw87xxx_profile_switch_0",0); // Music (使能 AW87xxx PA)
    mixer_set_enum(m,"SPKPA_L",1);                  // Switch
    mixer_set_enum(m,"SPKPA_R",1);
    mixer_close(m);
}

// ---------------- 触摸 ----------------
// 原生 tx=ABS_X∈[0,339], ty=ABS_Y∈[0,799] → display dx=799-ty, dy=tx
#define TX_MAX 340
#define TY_MAX 800
static void map_touch(int tx,int ty,int*dx,int*dy){
    if(tx<0)tx=0; if(tx>TX_MAX-1)tx=TX_MAX-1;
    if(ty<0)ty=0; if(ty>TY_MAX-1)ty=TY_MAX-1;
    *dx = (DISP_W-1) - ty; if(*dx<0)*dx=0; if(*dx>DISP_W-1)*dx=DISP_W-1;
    *dy = tx;              if(*dy>DISP_H-1)*dy=DISP_H-1;
}

static volatile int g_run=1;
static void on_sig(int s){(void)s; g_run=0;}

static struct pcm *manual_pcm;
static int selected_song=0,background=0,auto_active=0;
static struct c1_audio_status music;
static struct pcm *open_pcm(void){
    enable_speaker();
    static const struct { unsigned ps, pc; } cand[] = {
        {1280,8},{1280,4},{1024,8},{1024,4},{2048,4},{960,8},{512,8},{256,8}
    };
    struct pcm*pcm=NULL;
    for(unsigned i=0;i<sizeof(cand)/sizeof(cand[0]);i++){
        struct pcm_config cfg; memset(&cfg,0,sizeof(cfg));
        cfg.channels=2; cfg.rate=SR; cfg.format=PCM_FORMAT_S16_LE;
        cfg.period_size=cand[i].ps; cfg.period_count=cand[i].pc;
        struct pcm*p=pcm_open(0,0,PCM_OUT,&cfg);
        if(p&&pcm_is_ready(p)){
            pcm=p; PERIOD=(int)cand[i].ps; if(PERIOD>MAXPER)PERIOD=MAXPER;
            printf("pcm ready: 2ch %uHz period=%u x%u (cand %u)\n",SR,cand[i].ps,cand[i].pc,i);
            break;
        }
        if(p){ fprintf(stderr,"cand %u (%u x%u) failed: %s\n",i,cand[i].ps,cand[i].pc,pcm_get_error(p)); pcm_close(p);}
    }
    return pcm;

}
static long long millis(void){struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return (long long)t.tv_sec*1000+t.tv_nsec/1000000;}
static void text_at(int x,int y,const char*s,int scale){
    for(;*s&&x+8*scale<=DISP_W;s++,x+=8*scale)for(int r=0;r<8;r++)for(int c=0;c<8;c++)if(font8x8_basic[(unsigned char)*s&127][r]&(1<<c))
        for(int j=0;j<scale;j++)for(int i=0;i<scale;i++)bb_set(x+c*scale+i,y+r*scale+j,argb(232,242,235));
}
static void draw_toolbar(void){
    for(int y=0;y<KEY_TOP;y++)for(int x=0;x<DISP_W;x++)bb_set(x,y,argb(24,49,47));
    text_at(16,18,"<",2);text_at(66,18,auto_active?(music.state==C1_AUDIO_PAUSED?"PLAY":"PAUSE"):"PLAY",2);text_at(173,18,">",2);
    text_at(223,18,songs[selected_song].title,2);text_at(642,18,background?"BG: ON":"BG: OFF",2);
    if(music.owner==C1_AUDIO_PIANO&&music.state==C1_AUDIO_ERROR){text_at(16,53,"Audio unavailable - try Play again",1);return;}
    text_at(16,53,"CAMERA play/pause  ENTER next  BACK previous  SHIFT+CAMERA background",1);
}
static void refresh_music(void){
    struct c1_audio_status next;int active=c1_audio_get(&next)==0&&next.owner==C1_AUDIO_PIANO&&next.state>=C1_AUDIO_CONNECTING&&next.state<=C1_AUDIO_PAUSED;
    if(next.owner==C1_AUDIO_PIANO)music=next;
    if(active){if(next.song>=0&&next.song<SONG_COUNT)selected_song=next.song;}
    if(active||auto_active){auto_active=active;int note=active&&music.state==C1_AUDIO_PLAYING?song_note(selected_song,music.position_ms):0;
        for(int i=0;i<NKEYS;i++){int on=note==g_keys[i].midi;if(g_gate[i]!=on){g_gate[i]=on;draw_key(i,on);}}
        draw_toolbar();blit_rect(0,0,DISP_W,KEY_TOP);
    }
}
static void start_song(int song){
    selected_song=(song+SONG_COUNT)%SONG_COUNT;if(manual_pcm){pcm_close(manual_pcm);manual_pcm=NULL;}
    memset((void*)g_gate,0,sizeof g_gate);memset(g_env,0,sizeof g_env);
    struct c1_audio_request r={0};r.action=C1_AUDIO_PLAY;r.owner=C1_AUDIO_PIANO;r.kind=C1_AUDIO_SONG;r.index=selected_song;r.background=background;r.duration_ms=song_duration(selected_song);
    const char*root=getenv("C1_APPS_ROOT");if(!root)root="/storage/apps/current";snprintf(r.url,sizeof r.url,"%s/piano/c1max-piano",root);snprintf(r.title,sizeof r.title,"%s",songs[selected_song].title);
    auto_active=c1_audio_call(&r,&music,1)==0;
    if(auto_active)for(int i=1;i<SONG_COUNT;i++){r.action=C1_AUDIO_APPEND;r.index=(selected_song+i)%SONG_COUNT;r.duration_ms=song_duration(r.index);snprintf(r.title,sizeof r.title,"%s",songs[r.index].title);c1_audio_call(&r,NULL,0);}
    draw_all();
}
static void control(int action){
    if(action==0)start_song(selected_song-1);
    if(action==1){if(auto_active)c1_audio_command(C1_AUDIO_PIANO,C1_AUDIO_PAUSE,0,&music);else start_song(selected_song);}
    if(action==2)start_song(selected_song+1);
    if(action==3){int value=c1_audio_background_preference(C1_AUDIO_PIANO,!background);if(value>=0){background=value;c1_audio_command(C1_AUDIO_PIANO,C1_AUDIO_BACKGROUND,background,&music);}}
    draw_toolbar();blit_rect(0,0,DISP_W,KEY_TOP);
}
static void manual(void){
    if(manual_pcm)return;c1_audio_command(0,C1_AUDIO_STOP,0,NULL);auto_active=0;memset((void*)g_gate,0,sizeof g_gate);memset(g_env,0,sizeof g_env);manual_pcm=open_pcm();draw_all();
}
static int play_song_audio(int song){
    if(song<0||song>=SONG_COUNT)return 2;layout_keys();for(int k=0;k<NKEYS;k++)g_step[k]=g_keys[k].freq/SR;
    struct pcm *pcm=open_pcm();if(!pcm)return 1;setvbuf(stdout,NULL,_IOLBF,0);fcntl(0,F_SETFL,O_NONBLOCK);puts("C1_PIANO_READY");
    long long position=0;int paused=0,failed=0,failures=0;char pending[1024];size_t used=0;
    while(g_run&&position<song_duration(song)+220){
        char ch;while(read(0,&ch,1)==1){if(ch=='\n'){pending[used]=0;used=0;
            if(!strcmp(pending,"quit")){g_run=0;break;}
            if(!strcmp(pending,"pause"))paused=!paused;
            if(strstr(pending,"get_time_pos"))printf("C1_POSITION=%.3f\n",position/1000.0);
            char *seek=strstr(pending,"seek ");if(seek){position+=(long long)(atof(seek+5)*1000);if(position<0)position=0;memset(g_env,0,sizeof g_env);}
        }else if(used<sizeof pending-1)pending[used++]=ch;}
        int note=paused?0:song_note(song,position);for(int i=0;i<NKEYS;i++)g_gate[i]=note==g_keys[i].midi;
        render_period();if(pcm_writei(pcm,g_buf,PERIOD)<0){pcm_prepare(pcm);if(++failures>=6){failed=1;break;}continue;}failures=0;
        if(!paused)position+=(long long)PERIOD*1000/SR;
    }
    pcm_close(pcm);return failed;
}
int main(int argc,char**argv){
    if(argc==2&&!strcmp(argv[1],"--render-test"))return render_test();
    signal(SIGINT,on_sig); signal(SIGTERM,on_sig);
    int run_ms = argc>1? atoi(argv[1]) : 0;   // >0：跑这么多毫秒后自动退出(测试用)

    for(int i=0;i<SINLEN;i++) g_sin[i]=sinf(2.0f*(float)M_PI*i/SINLEN);

    if(argc==4&&!strcmp(argv[1],"--audio-only")&&!strcmp(argv[2],"--song"))return play_song_audio(atoi(argv[3]));
    background=c1_audio_background_preference(C1_AUDIO_PIANO,-1);
    // fb2
    int fbfd=open("/dev/fb2",O_RDWR);
    if(fbfd<0){perror("open fb2");return 1;}
    struct fb_var_screeninfo v; struct fb_fix_screeninfo f;
    ioctl(fbfd,FBIOGET_VSCREENINFO,&v); ioctl(fbfd,FBIOGET_FSCREENINFO,&f);
    g_stride=f.line_length;                       // 1360
    g_frame_sz=(long)g_stride*v.yres;             // 1360*800=1088000
    long map_sz=f.smem_len? f.smem_len : g_frame_sz*3;
    g_nframe=map_sz/g_frame_sz; if(g_nframe<1)g_nframe=1; if(g_nframe>3)g_nframe=3;
    g_fb=mmap(0,map_sz,PROT_READ|PROT_WRITE,MAP_SHARED,fbfd,0);
    if(g_fb==MAP_FAILED){perror("mmap");return 1;}
    g_back=calloc(DISP_W*DISP_H,4);
    printf("fb: %dx%d stride=%d frame=%ld nframe=%d\n",v.xres,v.yres,g_stride,g_frame_sz,g_nframe);

    layout_keys();
    for(int k=0;k<NKEYS;k++) g_step[k]=g_keys[k].freq/SR;
    draw_all();

    // 触摸
    int evfd=open("/dev/input/event2",O_RDONLY|O_NONBLOCK);
    if(evfd<0) perror("open event2");   // 无触摸也让声音/画面能测
    else { struct input_event tmp; while(read(evfd,&tmp,sizeof(tmp))==(int)sizeof(tmp)){} } // 丢弃开机前残留事件

    manual_pcm=NULL;
    // 触摸解析状态(单点协议 B 之外的单点：ABS_X/ABS_Y + BTN_TOUCH)
    int matrix=open("/dev/input/event0",O_RDONLY|O_NONBLOCK),gpio=open("/dev/input/event1",O_RDONLY|O_NONBLOCK);
    int physical[NKEYS]={0}; int shift_down=0;
    const int note_keys[NKEYS]={KEY_A,KEY_W,KEY_S,KEY_E,KEY_D,KEY_F,KEY_T,KEY_G,KEY_Y,KEY_H,KEY_U,KEY_J,KEY_K,KEY_O,KEY_L,KEY_P,KEY_Z,KEY_X,KEY_C,KEY_V,KEY_B,KEY_N,KEY_M,KEY_Q};
    int touching=0, cur_tx=0, cur_ty=0, cur_key=-1;
    long long elapsed=0; long long per_us=(long long)PERIOD*1000000/SR;
    int frames_written=0, write_err=0;long long last_music=0;int toolbar_down=0;
    refresh_music();

    while(g_run){
        struct input_event ke;
        while(matrix>=0&&read(matrix,&ke,sizeof ke)==sizeof ke)if(ke.type==EV_KEY){
            if(ke.code==KEY_LEFTSHIFT||ke.code==KEY_RIGHTSHIFT)shift_down=ke.value!=0;
            if(ke.code==KEY_SPACE&&ke.value==1)control(1);
            for(int n=0;n<NKEYS;n++)if(ke.code==note_keys[n]){if(ke.value)manual();if(!manual_pcm)continue;for(int k=0;k<NKEYS;k++)if(g_keys[k].midi==60+n){physical[k]=ke.value!=0;g_gate[k]=physical[k]||cur_key==k;draw_key(k,g_gate[k]);}}
        }
        while(gpio>=0&&read(gpio,&ke,sizeof ke)==sizeof ke)if(ke.type==EV_KEY&&ke.value==1){
            if(ke.code==KEY_POWER)g_run=0;
            else if(ke.code==KEY_SPACE)control(1);else if(ke.code==KEY_ENTER)control(2);
            else if(ke.code==KEY_BACKSPACE||ke.code==KEY_ESC)control(0);else if(ke.code==KEY_SHUFFLE)control(shift_down?3:1);
        }
        // 1) 非阻塞排空触摸事件
        if(evfd>=0){
            struct input_event ie; int changed=0;
            while(read(evfd,&ie,sizeof(ie))==(int)sizeof(ie)){
                if(ie.type==EV_ABS){
                    if(ie.code==ABS_X) cur_tx=ie.value;
                    else if(ie.code==ABS_Y) cur_ty=ie.value;
                } else if(ie.type==EV_KEY && ie.code==BTN_TOUCH){
                    touching=ie.value; changed=1;
                } else if(ie.type==EV_SYN && ie.code==SYN_REPORT){
                    changed=1;
                }
            }
            if(changed){
                int want=-1;
                if(touching){int dx,dy;map_touch(cur_tx,cur_ty,&dx,&dy);
                    if(dy<KEY_TOP){if(!toolbar_down){if(dx<53)control(0);else if(dx<160)control(1);else if(dx<214)control(2);else if(dx>=628)control(3);}toolbar_down=1;}
                    else {want=hit_key(dx,dy);if(want>=0)manual();}
                }else toolbar_down=0;
                if(want!=cur_key){
                    if(cur_key>=0){ g_gate[cur_key]=physical[cur_key]; draw_key(cur_key,g_gate[cur_key]); }
                    if(want>=0){ g_gate[want]=1; draw_key(want,1); }
                    cur_key=want;
                }
            }
        }
        // 2) 渲染一个 period 并写 PCM（阻塞→定拍）
        if(millis()-last_music>=150){last_music=millis();refresh_music();}
        if(manual_pcm){
            render_period();
            int rc=pcm_writei(manual_pcm,g_buf,PERIOD);
            if(rc<0){ write_err++; pcm_prepare(manual_pcm); }
            else frames_written+=PERIOD;
        } else {
            usleep(per_us);   // 无 PCM 时也维持循环节奏
        }
        elapsed+=per_us;
        if(run_ms>0 && elapsed/1000>=run_ms) break;
    }

    printf("exit: frames_written=%d write_err=%d\n",frames_written,write_err);
    if(manual_pcm)pcm_close(manual_pcm);
    c1_audio_command(C1_AUDIO_PIANO,C1_AUDIO_DETACH,0,NULL);
    if(evfd>=0) close(evfd);
    if(matrix>=0)close(matrix);if(gpio>=0)close(gpio);
    free(g_back); munmap(g_fb,map_sz); close(fbfd);
    return 0;
}
