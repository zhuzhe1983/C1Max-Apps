/* C1 Max native app launcher: four columns, two rows, horizontal pages.
 * Raw 96x96 BGRA icons keep the runtime independent of image libraries.
 * Define C1L_LOGIC_TEST for host-only paging/gesture/config-free tests.
 */
#define _GNU_SOURCE 1
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <assert.h>

#define PAGE_SIZE 8
#define COLS 4
#define SWIPE_PX 54
#define TAP_SLOP 12

typedef struct {
    int active, x, y, max_dx, max_dy, app, page;
    int64_t started;
} Gesture;
typedef struct { int kind, value; } GestureAction;
enum { GESTURE_NONE, GESTURE_OPEN, GESTURE_PAGE };

static int page_count(int count){ return count > 0 ? (count + PAGE_SIZE - 1) / PAGE_SIZE : 1; }
static int page_step(int page, int direction, int count){
    int next = page + direction, last = page_count(count) - 1;
    return next < 0 ? 0 : next > last ? last : next;
}
static int page_item(int page, int slot, int count){
    if(page < 0 || page >= page_count(count) || slot < 0 || slot >= PAGE_SIZE) return -1;
    int index = page * PAGE_SIZE + slot;
    return index < count ? index : -1;
}
static int page_selection(int selected, int page, int count){
    if(count <= 0) return -1;
    int slot = selected < 0 ? 0 : selected % PAGE_SIZE;
    int index = page_item(page, slot, count);
    return index >= 0 ? index : count - 1;
}
static void gesture_start(Gesture *g, int x, int y, int app, int page, int64_t now){
    *g = (Gesture){.active=1,.x=x,.y=y,.app=app,.page=page,.started=now};
}
static void gesture_move(Gesture *g, int x, int y){
    int dx=abs(x-g->x),dy=abs(y-g->y);
    if(dx>g->max_dx)g->max_dx=dx;
    if(dy>g->max_dy)g->max_dy=dy;
}
static GestureAction gesture_end(Gesture *g, int x, int y, int app, int page, int64_t now){
    GestureAction action={GESTURE_NONE,0};
    if(!g->active)return action;
    gesture_move(g,x,y);g->active=0;
    if(g->page!=page)return action;
    int dx=x-g->x,dy=y-g->y;
    if(abs(dx)>=SWIPE_PX && abs(dx)*2>=abs(dy)*3){
        action.kind=GESTURE_PAGE;action.value=dx<0?1:-1;return action;
    }
    if(g->max_dx<=TAP_SLOP && g->max_dy<=TAP_SLOP &&
       now>=g->started && now-g->started<600 && g->app>=0 && app==g->app){
        action.kind=GESTURE_OPEN;action.value=app;
    }
    return action;
}
static int launcher_logic_test(void){
    assert(page_count(0)==1 && page_count(8)==1 && page_count(9)==2 && page_count(64)==8);
    assert(page_step(0,-1,9)==0 && page_step(1,1,9)==1);
    assert(page_step(0,1,8)==0 && page_step(0,1,9)==1);
    assert(page_item(0,7,8)==7 && page_item(1,0,9)==8 && page_item(1,1,9)==-1);
    assert(page_item(0,8,64)==-1 && page_item(8,0,64)==-1);
    assert(page_selection(7,1,9)==8 && page_selection(8,0,9)==0);
    Gesture g;GestureAction a;
    gesture_start(&g,100,100,2,0,1000);a=gesture_end(&g,108,106,2,0,1100);
    assert(a.kind==GESTURE_OPEN && a.value==2);
    gesture_start(&g,180,100,2,0,1000);a=gesture_end(&g,110,108,1,0,1200);
    assert(a.kind==GESTURE_PAGE && a.value==1);
    gesture_start(&g,100,100,2,0,1000);a=gesture_end(&g,170,102,2,0,1200);
    assert(a.kind==GESTURE_PAGE && a.value==-1);
    gesture_start(&g,100,100,2,0,1000);gesture_move(&g,170,100);
    assert(gesture_end(&g,100,100,2,0,1200).kind==GESTURE_NONE);
    gesture_start(&g,100,100,2,0,1000);assert(gesture_end(&g,101,170,2,0,1200).kind==GESTURE_NONE);
    gesture_start(&g,100,100,2,0,1000);assert(gesture_end(&g,102,103,2,1,1200).kind==GESTURE_NONE);
    gesture_start(&g,100,100,2,0,1000);assert(gesture_end(&g,100,100,3,0,1200).kind==GESTURE_NONE);
    gesture_start(&g,100,100,2,0,1000);assert(gesture_end(&g,100,100,2,0,1700).kind==GESTURE_NONE);
    /* A swipe at either page boundary remains a swipe, never an app click. */
    gesture_start(&g,100,100,0,0,1000);a=gesture_end(&g,170,100,0,0,1100);
    assert(a.kind==GESTURE_PAGE && page_step(0,a.value,8)==0);
    puts("PASS launcher pages, partial-page shortcuts, selection, swipes and tap exclusion");
    return 0;
}

#ifdef C1L_LOGIC_TEST
int main(void){return launcher_logic_test();}
#else
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <poll.h>
#include <time.h>
#include <signal.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <sys/prctl.h>
#include <sys/stat.h>
#include <linux/fb.h>
#include <linux/input.h>
#include "font8x8.h"
#include "fontcjk.h"
#include "typeface.h"

#define LW 800
#define LH 340
#define MAX_APPS 64
#define LABEL_MAX 48
#define ARGV_MAX 8
#define ICON_SIZE 96
#define GRID_LEFT 20
#define GRID_TOP 60
#define CELL_W 190
#define CELL_H 120
#define CARD_W 182
#define CARD_H 112
#define FOOTER_Y 304
#define NAV_PREVIOUS 1000
#define NAV_NEXT 1001

typedef struct {
    char label[LABEL_MAX],argbuf[512],cwd[256],icon_id[32];
    char *argv[ARGV_MAX];
    uint32_t color,*icon;
    int present,icon_checked;
} App;
static App apps[MAX_APPS];
static int napps,current_page,selected_app,keyboard_action=-1,key_dirty;
static int pressed_app=-1;
static int font_ready;
static const uint32_t canvas_color=0xff101923,selected_color=0xff1b3441,pressed_color=0xff244653;
static const uint32_t focus_color=0xffa6ded1,text_color=0xfff0f4f5,muted_color=0xffb2c0cc,disabled_color=0xff7a8c99;
static int fb_fd=-1,fb_stride=1360,fb_nframes;
static size_t fb_map_sz,fb_frame_sz;
static uint8_t *fb_mem;
static uint32_t backbuf[LW*LH];
static struct fb_var_screeninfo fb_saved;
static int touch_fd=-1,key_fd=-1,matrix_fd=-1,flip_x,flip_y;
static int t_raw_x,t_raw_y,t_down,t_dropped;
static volatile sig_atomic_t want_quit;
static const char *icon_root;
static const uint32_t palette[]={0xff4385ba,0xff629c82,0xff8a74b5,0xffc18461,0xff638ac2,0xff718eac,0xff517a83};

static void on_signal(int signal_number){(void)signal_number;want_quit=1;}
static int64_t now_ms(void){struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return (int64_t)t.tv_sec*1000+t.tv_nsec/1000000;}

static int fb_open(const char *path){
    struct fb_fix_screeninfo f;
    fb_fd=open(path,O_RDWR|O_CLOEXEC);
    if(fb_fd<0||ioctl(fb_fd,FBIOGET_VSCREENINFO,&fb_saved)||ioctl(fb_fd,FBIOGET_FSCREENINFO,&f)){
        perror("[launcher] framebuffer");return -1;
    }
    if(fb_saved.xres!=340||fb_saved.yres!=800||fb_saved.bits_per_pixel!=32||
       f.line_length<1360||f.line_length%4||f.smem_len<(uint64_t)f.line_length*800)return -1;
    fb_stride=f.line_length;fb_frame_sz=(size_t)fb_stride*800;fb_map_sz=f.smem_len;
    fb_nframes=fb_map_sz/fb_frame_sz;if(fb_nframes>3)fb_nframes=3;
    fb_mem=mmap(NULL,fb_map_sz,PROT_READ|PROT_WRITE,MAP_SHARED,fb_fd,0);
    if(fb_mem==MAP_FAILED){fb_mem=NULL;perror("[launcher] mmap");return -1;}
    return 0;
}
static void blit_frame(uint8_t *base){
    for(int row=0;row<800;row++){
        uint32_t *dst=(uint32_t*)(base+(size_t)row*fb_stride);
        const uint32_t *src=&backbuf[799-row];
        for(int col=0;col<340;col++)dst[col]=src[col*LW];
    }
}
static void fb_present(void){
    struct fb_var_screeninfo v;
    if(!fb_mem||ioctl(fb_fd,FBIOGET_VSCREENINFO,&v))return;
    if(v.xres!=340||v.yres!=800||v.bits_per_pixel!=32||v.yres_virtual<1600){
        /* A child can leave fbdev2 configured for a single virtual page. */
        struct fb_fix_screeninfo f;
        if(ioctl(fb_fd,FBIOGET_FSCREENINFO,&f)||f.line_length!=(unsigned)fb_stride||
           (uint64_t)fb_saved.yres_virtual*fb_stride>f.smem_len)return;
        v=fb_saved;v.xoffset=v.yoffset=0;v.activate=FB_ACTIVATE_NOW;
        if(ioctl(fb_fd,FBIOPUT_VSCREENINFO,&v)||ioctl(fb_fd,FBIOGET_VSCREENINFO,&v))return;
    }
    int target=-1;
    for(int page=0;page<fb_nframes;page++){
        unsigned start=page*800,end=start+800;
        if(end<=v.yoffset||start>=v.yoffset+800){
            if(end<=v.yres_virtual){target=page;break;}
        }
    }
    if(target>=0){
        blit_frame(fb_mem+(size_t)target*fb_frame_sz);__sync_synchronize();
        v.xoffset=0;v.yoffset=target*800;v.activate=FB_ACTIVATE_NOW;
        if(ioctl(fb_fd,FBIOPAN_DISPLAY,&v))perror("[launcher] present page");
    }else if((uint64_t)v.yoffset*fb_stride+fb_frame_sz<=fb_map_sz){
        /* Single-page fallback: one complete redraw, never a continuous loop. */
        blit_frame(fb_mem+(size_t)v.yoffset*fb_stride);
    }
}

static void px(int x,int y,uint32_t color){if((unsigned)x<LW&&(unsigned)y<LH)backbuf[y*LW+x]=color;}
static void fill_rect(int x,int y,int w,int h,uint32_t color){
    if(x<0){w+=x;x=0;}if(y<0){h+=y;y=0;}if(x+w>LW)w=LW-x;if(y+h>LH)h=LH-y;
    for(int yy=0;yy<h;yy++)for(int xx=0;xx<w;xx++)backbuf[(y+yy)*LW+x+xx]=color;
}
static void round_rect(int x,int y,int w,int h,int radius,uint32_t color){
    for(int yy=0;yy<h;yy++)for(int xx=0;xx<w;xx++){
        int dx=xx<radius?radius-xx-1:xx>=w-radius?xx-(w-radius):0;
        int dy=yy<radius?radius-yy-1:yy>=h-radius?yy-(h-radius):0;
        if(dx*dx+dy*dy<=radius*radius)px(x+xx,y+yy,color);
    }
}
static void draw_char(int x,int y,char ch,uint32_t color,int scale){
    unsigned uc=(unsigned char)ch;if(uc>127)uc='?';
    const uint8_t *glyph=font8x8_basic[uc];
    for(int row=0;row<8;row++)for(int col=0;col<8;col++)if(glyph[row]&(1u<<col))
        for(int sy=0;sy<scale;sy++)for(int sx=0;sx<scale;sx++)px(x+col*scale+sx,y+row*scale+sy,color);
}
static uint32_t utf8_next(const char **text){
    const unsigned char *s=(const unsigned char*)*text;uint32_t cp;int n;
    if(s[0]<0x80){cp=s[0];n=1;}
    else if((s[0]&0xe0)==0xc0){cp=s[0]&0x1f;n=2;}
    else if((s[0]&0xf0)==0xe0){cp=s[0]&0x0f;n=3;}
    else if((s[0]&0xf8)==0xf0){cp=s[0]&7;n=4;}
    else {(*text)++;return '?';}
    for(int i=1;i<n;i++){
        if(!s[i]||(s[i]&0xc0)!=0x80){(*text)++;return '?';}
        cp=(cp<<6)|(s[i]&0x3f);
    }
    *text+=(size_t)n;return cp;
}
static int glyph_width(uint32_t cp,int scale){return cp<128?8*scale:16*(scale<2?1:scale/2);}
static void draw_text(int x,int y,const char *text,uint32_t color,int pixels){
    if(font_ready){typeface_draw(backbuf,LW,LH,x,y,text,color,pixels);return;}
    int scale=pixels>=20?2:1;
    while(*text){uint32_t cp=utf8_next(&text);
        if(cp<128)draw_char(x,y,(char)cp,color,scale);
        else {const uint16_t *g=cjk_lookup(cp);int s=scale<2?1:scale/2;
            if(g){for(int row=0;row<16;row++)for(int col=0;col<16;col++)if(g[row]&(1u<<col))
                for(int yy=0;yy<s;yy++)for(int xx=0;xx<s;xx++)px(x+col*s+xx,y+row*s+yy,color);}
            else draw_char(x,y,'?',color,scale);
        }
        x+=glyph_width(cp,scale);
    }
}
static int text_width(const char *text,int pixels){
    if(font_ready)return typeface_width(text,pixels);
    int width=0,scale=pixels>=20?2:1;while(*text)width+=glyph_width(utf8_next(&text),scale);return width;
}
static void text_center(int center,int y,const char *text,uint32_t color,int scale){draw_text(center-text_width(text,scale)/2,y,text,color,scale);}
static void label_center(int center,int y,const char *text,uint32_t color){
    char shown[LABEL_MAX+8];size_t used=0;const char *p=text;
    int ellipsis=text_width("…",20),long_label=text_width(text,20)>174;
    while(*p){
        const char *before=p;utf8_next(&p);size_t bytes=(size_t)(p-before);
        if(used+bytes>=LABEL_MAX)break;
        memcpy(shown+used,before,bytes);shown[used+bytes]=0;
        if(text_width(shown,20)+(long_label?ellipsis:0)>174){shown[used]=0;p=before;break;}
        used+=bytes;
    }
    shown[used]=0;if(*p)strcat(shown,"…");
    text_center(center,y,shown,color,20);
}

static int valid_icon_id(const char *id){
    size_t length=strlen(id);if(!length||length>=sizeof(apps[0].icon_id))return 0;
    for(size_t i=0;i<length;i++)if(!((id[i]>='a'&&id[i]<='z')||(id[i]>='0'&&id[i]<='9')||id[i]=='_'||id[i]=='-'))return 0;
    return 1;
}
static const char *infer_icon(char **argv,int argc){
    for(int i=1;i<argc;i++){if(!strcmp(argv[i],"--updates"))return "updates";if(!strcmp(argv[i],"--roms"))return "nes";}
    const char *base=strrchr(argv[0],'/');base=base?base+1:argv[0];
    return !strncmp(base,"c1max-",6)?base+6:base;
}
static int add_app(const char *label,const char *cwd,char **argv,int argc,const char *icon){
    if(napps>=MAX_APPS||argc<1||argc>=ARGV_MAX||!argv[0][0])return 0;
    App *a=&apps[napps];memset(a,0,sizeof(*a));
    if(strlen(label)>=sizeof(a->label)||(cwd&&strlen(cwd)>=sizeof(a->cwd)))return 0;
    strcpy(a->label,label);if(cwd)strcpy(a->cwd,cwd);
    size_t used=0;
    for(int i=0;i<argc;i++){size_t bytes=strlen(argv[i])+1;if(used+bytes>sizeof(a->argbuf))return 0;
        a->argv[i]=a->argbuf+used;memcpy(a->argv[i],argv[i],bytes);used+=bytes;
    }
    if(!icon||!*icon)icon=infer_icon(argv,argc);
    if(valid_icon_id(icon))strcpy(a->icon_id,icon);
    struct stat st;a->present=stat(argv[0],&st)==0&&S_ISREG(st.st_mode)&&access(argv[0],X_OK)==0;
    a->color=palette[napps%(sizeof(palette)/sizeof(palette[0]))];napps++;return 1;
}
static void load_config(const char *path){
    FILE *fp=fopen(path,"r");if(!fp)return;
    char line[1024];
    while(fgets(line,sizeof(line),fp)){
        if(!strchr(line,'\n')&&!feof(fp)){int c;while((c=fgetc(fp))!='\n'&&c!=EOF){};continue;}
        line[strcspn(line,"\r\n")]=0;if(!line[0]||line[0]=='#')continue;
        char *parts[16],*cursor=line,*part;int count=0;
        while((part=strsep(&cursor,"|"))){if(count==16)break;parts[count++]=part;}
        if(count<2||part||!parts[0][0])continue;
        char *argv[ARGV_MAX];int argc=0;const char *cwd=NULL,*icon=NULL;
        for(int i=1;i<count;i++){
            if(i>1&&!parts[i][0])continue;
            if(i>1&&!strncmp(parts[i],"cwd=",4)){cwd=parts[i]+4;continue;}
            if(i>1&&!strncmp(parts[i],"icon=",5)){icon=parts[i]+5;continue;}
            /* New six-column rows: label|exec|arg1|arg2|cwd=...|icon-id.
             * Old variable argument rows remain valid; an explicit icon= tag
             * is also accepted and removes ambiguity with ordinary arguments. */
            if(i==5&&count==6&&(!parts[4][0]||!strncmp(parts[4],"cwd=",4))&&valid_icon_id(parts[i])){icon=parts[i];continue;}
            if(argc>=ARGV_MAX-1){argc=-1;break;}argv[argc++]=parts[i];
        }
        if(argc>0&&!add_app(parts[0],cwd,argv,argc,icon))fprintf(stderr,"[launcher] Invalid app entry: %s\n",parts[0]);
    }
    fclose(fp);fprintf(stderr,"[launcher] Loaded %d applications\n",napps);
}
static void load_defaults(void){
    static const char *ids[]={"streamplayer","calendar","calculator","piano","nes","updates","terminal"};
    static const char *labels[]={"StreamPlayer","日历","计算器","钢琴","NES 游戏","应用更新","终端"};
    const char *root=getenv("C1_APPS_ROOT");if(!root)root="/storage/apps/current";
    for(int i=0;i<7;i++){
        char executable[256];const char *program=(i==4||i==5)?"streamplayer":ids[i];
        snprintf(executable,sizeof executable,"%s/%s/c1max-%s",root,program,program);
        char *argv[]={executable,i==4?"--roms":i==5?"--updates":NULL};
        add_app(labels[i],NULL,argv,i==4||i==5?2:1,ids[i]);
    }
}
static void load_icon(App *app){
    if(app->icon_checked)return;app->icon_checked=1;
    if(!app->icon_id[0])return;
    char path[512];if(snprintf(path,sizeof path,"%s/%s.bgra",icon_root,app->icon_id)>=(int)sizeof path)return;
    FILE *fp=fopen(path,"rb");if(!fp)return;
    size_t size=(size_t)ICON_SIZE*ICON_SIZE*sizeof(uint32_t);
    uint32_t *pixels=malloc(size);
    if(pixels){if(fread(pixels,1,size,fp)!=size||fgetc(fp)!=EOF){free(pixels);pixels=NULL;}}
    fclose(fp);app->icon=pixels;
}
static void release_other_icons(void){
    for(int i=0;i<napps;i++)if(i/PAGE_SIZE!=current_page){free(apps[i].icon);apps[i].icon=NULL;apps[i].icon_checked=0;}
}
static void draw_icon(App *app,int x,int y){
    load_icon(app);
    if(app->icon){
        for(int yy=0;yy<ICON_SIZE;yy++)for(int xx=0;xx<ICON_SIZE;xx++){
            uint32_t c=app->icon[yy*ICON_SIZE+xx],a=c>>24;
            if(a==255)px(x+xx,y+yy,c);
            else if(a){uint32_t bg=backbuf[(y+yy)*LW+x+xx];
                uint32_t r=(((c>>16)&255)*a+((bg>>16)&255)*(255-a)+127)/255;
                uint32_t g=(((c>>8)&255)*a+((bg>>8)&255)*(255-a)+127)/255;
                uint32_t b=((c&255)*a+(bg&255)*(255-a)+127)/255;
                px(x+xx,y+yy,0xff000000|(r<<16)|(g<<8)|b);
            }
        }
    }else {
        round_rect(x+13,y+13,70,70,20,0xff253641);
        char letter[5]={0};const char *end=app->label;utf8_next(&end);size_t bytes=end-app->label;
        if(bytes<sizeof letter)memcpy(letter,app->label,bytes);
        text_center(x+ICON_SIZE/2,y+29,letter,text_color,26);
    }
}
static int hit_test(int x,int y){
    if(page_count(napps)>1&&y>=6&&y<50){
        if(x>=640&&x<684)return current_page>0?NAV_PREVIOUS:-1;
        if(x>=736&&x<780)return current_page+1<page_count(napps)?NAV_NEXT:-1;
    }
    if(x<GRID_LEFT||x>=GRID_LEFT+CELL_W*4||y<GRID_TOP||y>=GRID_TOP+CELL_H*2)return -1;
    int col=(x-GRID_LEFT)/CELL_W,row=(y-GRID_TOP)/CELL_H;
    if((x-GRID_LEFT)%CELL_W>=CARD_W||(y-GRID_TOP)%CELL_H>=CARD_H)return -1;
    return page_item(current_page,row*COLS+col,napps);
}
static void chevron(int center,int cy,int direction,uint32_t color){
    for(int yy=-6;yy<=6;yy++){
        int x=center+direction*(3-abs(yy));fill_rect(x-1,cy+yy,2,1,color);
    }
}
static void draw_grid(const char *toast){
    fill_rect(0,0,LW,LH,canvas_color);
    draw_text(24,6,"应用",text_color,26);
    char heading[64];snprintf(heading,sizeof heading,"%d 个应用",napps);draw_text(96,18,heading,muted_color,16);
    if(page_count(napps)>1){
        round_rect(640,6,44,44,12,0xff1c2a36);round_rect(736,6,44,44,12,0xff1c2a36);
        chevron(662,28,-1,current_page>0?focus_color:disabled_color);
        chevron(758,28,1,current_page+1<page_count(napps)?focus_color:disabled_color);
        snprintf(heading,sizeof heading,"%d / %d",current_page+1,page_count(napps));
        text_center(710,15,heading,muted_color,16);
    }else draw_text(698,18,"C1 Max",muted_color,16);
    for(int slot=0;slot<PAGE_SIZE;slot++){
        int index=page_item(current_page,slot,napps);if(index<0)continue;
        int left=GRID_LEFT+(slot%COLS)*CELL_W,center=left+CARD_W/2,top=GRID_TOP+(slot/COLS)*CELL_H;
        int chosen=index==selected_app;App *app=&apps[index];
        if(chosen){
            round_rect(left,top,CARD_W,CARD_H,17,focus_color);
            round_rect(left+2,top+2,CARD_W-4,CARD_H-4,15,index==pressed_app?pressed_color:selected_color);
        }
        draw_icon(app,center-ICON_SIZE/2,top-4);
        round_rect(left+10,top+10,24,24,7,chosen?focus_color:0xff22313d);
        char shortcut[2]={"QWERTYUI"[slot],0};text_center(left+22,top+11,shortcut,chosen?canvas_color:muted_color,14);
        label_center(center,top+78,app->label,app->present?text_color:disabled_color);
        if(!app->present)draw_text(left+125,top+10,"未安装",disabled_color,14);
    }
    if(!napps)text_center(400,148,"尚未添加应用",muted_color,20);
    fill_rect(24,FOOTER_Y,752,1,0xff283744);
    draw_text(24,308,"Q–I 打开  ·  A/D 翻页",muted_color,14);
    draw_text(646,308,"电源键 · 返回",muted_color,14);
    int pages=page_count(napps),dots_width=pages*14;
    if(pages>1)for(int p=0;p<pages;p++)round_rect((LW-dots_width)/2+p*14,320,8,4,2,p==current_page?focus_color:disabled_color);
    if(toast){
        int width=text_width(toast,20)+48;if(width>744)width=744;
        round_rect((LW-width)/2-1,139,width+2,56,15,focus_color);
        round_rect((LW-width)/2,140,width,54,14,0xff263b47);text_center(LW/2,150,toast,text_color,20);
    }
}
static int change_page(int direction){
    int next=page_step(current_page,direction,napps);if(next==current_page)return 0;
    current_page=next;selected_app=page_selection(selected_app,next,napps);keyboard_action=-1;release_other_icons();return 1;
}

typedef struct {int down,x,y,cancelled;} Touch;
static int poll_input(Touch *out,int timeout){
    struct pollfd fds[3];int descriptors[]={touch_fd,key_fd,matrix_fd};
    for(int i=0;i<3;i++){fds[i].fd=descriptors[i];fds[i].events=POLLIN;fds[i].revents=0;}
    out->cancelled=0;
    if(poll(fds,3,timeout)<=0)return 0;
    for(int i=1;i<3;i++)if(fds[i].revents&POLLIN){struct input_event e;
        while(read(descriptors[i],&e,sizeof e)==sizeof e){
            if(e.type!=EV_KEY||e.value!=1)continue;
            if(e.code==KEY_POWER){want_quit=1;continue;}
            /* Backspace/Delete are editing keys, never launcher exit keys. */
            if(e.code==KEY_A||e.code==KEY_LEFT||e.code==KEY_UP){key_dirty|=change_page(-1);continue;}
            if(e.code==KEY_D||e.code==KEY_RIGHT||e.code==KEY_DOWN){key_dirty|=change_page(1);continue;}
            if(e.code>=KEY_Q&&e.code<=KEY_I){int index=page_item(current_page,e.code-KEY_Q,napps);
                if(index>=0){selected_app=index;keyboard_action=index;key_dirty=1;}}
            else if(e.code==KEY_ENTER&&selected_app>=0){keyboard_action=selected_app;key_dirty=1;}
        }
    }
    if(fds[0].revents&POLLIN){struct input_event e;
        while(read(touch_fd,&e,sizeof e)==sizeof e){
            if(e.type==EV_SYN&&e.code==SYN_DROPPED){t_dropped=1;t_down=0;continue;}
            if(t_dropped){if(e.type==EV_SYN&&e.code==SYN_REPORT){t_dropped=0;out->down=0;out->cancelled=1;return 1;}continue;}
            if(e.type==EV_ABS){if(e.code==ABS_X)t_raw_x=e.value;else if(e.code==ABS_Y)t_raw_y=e.value;}
            else if(e.type==EV_KEY&&e.code==BTN_TOUCH)t_down=e.value;
            else if(e.type==EV_SYN&&e.code==SYN_REPORT){
                int x=799-t_raw_y,y=t_raw_x;if(flip_x)x=799-x;if(flip_y)y=339-y;
                out->x=x<0?0:x>799?799:x;out->y=y<0?0:y>339?339:y;out->down=t_down;
                return 1; /* Keep down/up frames separate, including quick taps. */
            }
        }
    }
    return 0;
}
static void drain_input(void){
    struct input_event event;int descriptors[]={touch_fd,key_fd,matrix_fd};
    for(int i=0;i<3;i++)if(descriptors[i]>=0)while(read(descriptors[i],&event,sizeof event)==sizeof event){}
    t_down=t_dropped=0;struct input_absinfo axis;
    if(ioctl(touch_fd,EVIOCGABS(ABS_X),&axis)==0)t_raw_x=axis.value;
    if(ioctl(touch_fd,EVIOCGABS(ABS_Y),&axis)==0)t_raw_y=axis.value;
}
static void launch_app(App *app){
    if(!app->present||!app->argv[0])return;
    fprintf(stderr,"[launcher] Open %s\n",app->label);pid_t parent=getpid(),child=fork();
    if(child==0){
        setpgid(0,0);prctl(PR_SET_PDEATHSIG,SIGTERM);if(getppid()!=parent)_exit(1);
        if(app->cwd[0]&&chdir(app->cwd))_exit(126);
        execv(app->argv[0],app->argv);_exit(127);
    }
    if(child>0){
        int status=0;setpgid(child,child);
        for(;;){
            pid_t done=waitpid(child,&status,WNOHANG);if(done==child)break;
            if(done<0){if(errno==EINTR)continue;break;}
            if(want_quit){
                kill(-child,SIGTERM);int i;
                for(i=0;i<30;i++){if(waitpid(child,&status,WNOHANG)==child)break;usleep(100000);}
                if(i==30){kill(-child,SIGKILL);while(waitpid(child,&status,0)<0&&errno==EINTR){}}
                break;
            }
            usleep(50000);
        }
        fprintf(stderr,"[launcher] %s finished (status %d)\n",app->label,status);
    }else perror("[launcher] fork");
    drain_input();
}
static void open_selection(int index,char *toast,size_t size,int64_t *until){
    if(index<0||index>=napps)return;
    selected_app=index;
    if(apps[index].present){
        char message[80];snprintf(message,sizeof message,"正在打开 %s",apps[index].label);
        draw_grid(message);fb_present();launch_app(&apps[index]);toast[0]=0;
    }else{snprintf(toast,size,"%s 尚未安装",apps[index].label);*until=now_ms()+1600;}
}
int main(int argc,char **argv){
    if(argc==2&&!strcmp(argv[1],"--self-test"))return launcher_logic_test();
    signal(SIGINT,on_signal);signal(SIGTERM,on_signal);signal(SIGPIPE,SIG_IGN);
    if(getenv("C1L_FLIPX"))flip_x=atoi(getenv("C1L_FLIPX"));
    if(getenv("C1L_FLIPY"))flip_y=atoi(getenv("C1L_FLIPY"));
    const char *config=getenv("C1L_CONFIG");if(!config)config="/storage/apps/current/launcher/apps.txt";
    load_config(config);if(!napps)load_defaults();selected_app=napps?0:-1;
    char icon_path[512];icon_root=getenv("C1L_ICONS");
    if(!icon_root){const char *root=getenv("C1_APPS_ROOT");if(!root)root="/storage/apps/current";
        snprintf(icon_path,sizeof icon_path,"%s/launcher/icons",root);icon_root=icon_path;}
    char font_path[512];const char *font=getenv("C1L_FONT");
    if(!font){const char *root=getenv("C1_APPS_ROOT");if(!root)root="/storage/apps/current";
        snprintf(font_path,sizeof font_path,"%s/shared/NotoSansSC-Regular.ttf",root);font=font_path;}
    font_ready=typeface_open(font);
    if(!font_ready)fprintf(stderr,"[launcher] Font unavailable; using bitmap fallback\n");
    if(argc>=3&&!strcmp(argv[1],"--render")){
        if(argc>3)current_page=page_step(0,atoi(argv[3]),napps);
        selected_app=page_selection(0,current_page,napps);
        draw_grid(NULL);FILE *output=fopen(argv[2],"wb");
        if(!output)return 1;
        int good=fwrite(backbuf,1,sizeof backbuf,output)==sizeof backbuf;fclose(output);
        for(int i=0;i<napps;i++)free(apps[i].icon);typeface_close();return good?0:1;
    }
    if(fb_open(argc>1?argv[1]:"/dev/fb2"))return 1;
    touch_fd=open("/dev/input/event2",O_RDONLY|O_NONBLOCK|O_CLOEXEC);
    key_fd=open("/dev/input/event1",O_RDONLY|O_NONBLOCK|O_CLOEXEC);
    matrix_fd=open("/dev/input/event0",O_RDONLY|O_NONBLOCK|O_CLOEXEC);drain_input();
    Gesture gesture={0};Touch touch={0};char toast[96]={0};int64_t toast_until=0;int redraw=1;
    while(!want_quit){
        int got=poll_input(&touch,80);if(want_quit)break;
        if(key_dirty){redraw=1;key_dirty=0;}
        if(keyboard_action>=0){int index=keyboard_action;keyboard_action=-1;
            open_selection(index,toast,sizeof toast,&toast_until);gesture.active=0;got=0;redraw=1;}
        int64_t now=now_ms();
        if(got&&touch.cancelled){gesture.active=0;pressed_app=-1;redraw=1;got=0;}
        if(got){
            int index=hit_test(touch.x,touch.y);
            if(touch.down&&!gesture.active){
                gesture_start(&gesture,touch.x,touch.y,index,current_page,now);
                if(index>=0&&index<napps){selected_app=index;pressed_app=index;redraw=1;}
            }else if(touch.down){
                gesture_move(&gesture,touch.x,touch.y);
                if(pressed_app>=0&&(gesture.max_dx>TAP_SLOP||gesture.max_dy>TAP_SLOP)){pressed_app=-1;redraw=1;}
            }
            else if(gesture.active){
                pressed_app=-1;redraw=1;
                GestureAction action=gesture_end(&gesture,touch.x,touch.y,index,current_page,now);
                if(action.kind==GESTURE_PAGE){redraw|=change_page(action.value);}
                else if(action.kind==GESTURE_OPEN){
                    if(action.value==NAV_PREVIOUS||action.value==NAV_NEXT)redraw|=change_page(action.value==NAV_NEXT?1:-1);
                    else open_selection(action.value,toast,sizeof toast,&toast_until);
                    redraw=1;
                }
            }
        }
        if(toast[0]&&now>=toast_until){toast[0]=0;redraw=1;}
        if(redraw&&!want_quit){draw_grid(toast[0]?toast:NULL);fb_present();redraw=0;}
    }
    typeface_close();
    for(int i=0;i<napps;i++)free(apps[i].icon);
    if(fb_mem)munmap(fb_mem,fb_map_sz);if(fb_fd>=0)close(fb_fd);
    if(touch_fd>=0)close(touch_fd);if(key_fd>=0)close(key_fd);if(matrix_fd>=0)close(matrix_fd);
    return 0;
}
#endif
