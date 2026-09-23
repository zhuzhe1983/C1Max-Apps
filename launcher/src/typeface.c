/* Antialiased Noto Sans SC; the existing font is mapped read-only, not copied.
 * stb_truetype comes from the pinned LVGL dependency (MIT/public domain).
 * A bounded cache keeps repeated framebuffer redraws inexpensive. */
#define _GNU_SOURCE
#include "typeface.h"
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <unistd.h>
#include <string.h>
#include <math.h>
#define STB_TRUETYPE_IMPLEMENTATION
#include "stb_truetype_htcw.h"

enum { CACHE_SIZE=512 };
typedef struct { uint32_t cp; int size,w,h,dx,dy,advance; unsigned char *mask; } Glyph;
static Glyph glyphs[CACHE_SIZE];
static unsigned next_glyph;
static stbtt_fontinfo face;
static unsigned char *mapping;
static size_t mapped_size;
static int ascent;

static uint32_t next_cp(const char **p){
    const unsigned char *s=(const unsigned char *)*p;uint32_t cp;int n;
    if(s[0]<128){(*p)++;return s[0];}
    if((s[0]&0xe0)==0xc0){cp=s[0]&31;n=2;}
    else if((s[0]&0xf0)==0xe0){cp=s[0]&15;n=3;}
    else if((s[0]&0xf8)==0xf0){cp=s[0]&7;n=4;}
    else {(*p)++;return 0xfffd;}
    for(int i=1;i<n;i++){
        if(!s[i]||(s[i]&0xc0)!=0x80){(*p)++;return 0xfffd;}
        cp=(cp<<6)|(s[i]&63);
    }
    *p+=n;return cp;
}
static Glyph *glyph(uint32_t cp,int pixels){
    for(unsigned i=0;i<CACHE_SIZE;i++)if(glyphs[i].size==pixels&&glyphs[i].cp==cp)return &glyphs[i];
    Glyph *g=&glyphs[next_glyph++%CACHE_SIZE];free(g->mask);memset(g,0,sizeof *g);
    g->cp=cp;g->size=pixels;
    float scale=stbtt_ScaleForMappingEmToPixels(&face,(float)pixels);
    int advance;stbtt_GetCodepointHMetrics(&face,cp,&advance,NULL);
    g->advance=(int)lroundf(advance*scale);
    g->mask=stbtt_GetCodepointBitmap(&face,scale,scale,cp,&g->w,&g->h,&g->dx,&g->dy);
    return g;
}
int typeface_open(const char *path){
    int fd=open(path,O_RDONLY|O_CLOEXEC);struct stat st;
    if(fd<0)return 0;
    if(fstat(fd,&st)||st.st_size<1024||st.st_size>32*1024*1024){close(fd);return 0;}
    mapped_size=(size_t)st.st_size;
    mapping=mmap(NULL,mapped_size,PROT_READ,MAP_PRIVATE,fd,0);close(fd);
    if(mapping==MAP_FAILED){mapping=NULL;return 0;}
    int offset=stbtt_GetFontOffsetForIndex(mapping,0);
    if(offset<0||(size_t)offset>=mapped_size||!stbtt_InitFont(&face,mapping,offset)){
        munmap(mapping,mapped_size);mapping=NULL;return 0;
    }
    stbtt_GetFontVMetrics(&face,&ascent,NULL,NULL);return 1;
}
void typeface_close(void){
    for(unsigned i=0;i<CACHE_SIZE;i++){free(glyphs[i].mask);memset(&glyphs[i],0,sizeof glyphs[i]);}
    if(mapping)munmap(mapping,mapped_size);mapping=NULL;next_glyph=0;
}
int typeface_width(const char *text,int pixels){
    if(!mapping||pixels<8||pixels>40)return -1;
    int width=0;while(*text)width+=glyph(next_cp(&text),pixels)->advance;return width;
}
void typeface_draw(uint32_t *out,int width,int height,int x,int y,const char *text,uint32_t color,int pixels){
    if(!mapping||pixels<8||pixels>40)return;
    int baseline=y+(int)lroundf(ascent*stbtt_ScaleForMappingEmToPixels(&face,(float)pixels));
    while(*text){
        Glyph *g=glyph(next_cp(&text),pixels);
        if(g->mask)for(int yy=0;yy<g->h;yy++)for(int xx=0;xx<g->w;xx++){
            int dx=x+g->dx+xx,dy=baseline+g->dy+yy;
            if((unsigned)dx>=(unsigned)width||(unsigned)dy>=(unsigned)height)continue;
            unsigned a=g->mask[yy*g->w+xx];if(!a)continue;
            uint32_t *pixel=&out[dy*width+dx],bg=*pixel;
            unsigned r=(((color>>16)&255)*a+((bg>>16)&255)*(255-a)+127)/255;
            unsigned green=(((color>>8)&255)*a+((bg>>8)&255)*(255-a)+127)/255;
            unsigned b=((color&255)*a+(bg&255)*(255-a)+127)/255;
            *pixel=0xff000000|(r<<16)|(green<<8)|b;
        }
        x+=g->advance;
    }
}
