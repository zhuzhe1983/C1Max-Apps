// fb2 显示测试：mmap /dev/fb2，画测试图案。用于确认显示管线 + 旋转方向。
// 面板原生 340x800 竖屏 32bpp BGRA，DPU 旋转90°显示为 800x340 横屏。
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdint.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <linux/fb.h>

// BGRA: 内存 B,G,R,A → little-endian uint32 = 0xAARRGGBB
static inline uint32_t rgb(uint8_t r, uint8_t g, uint8_t b){ return 0xFF000000u|(r<<16)|(g<<8)|b; }

int main(int argc, char**argv){
    const char* dev = argc>1? argv[1] : "/dev/fb2";
    int fd = open(dev, O_RDWR);
    if(fd<0){ perror("open fb"); return 1; }
    struct fb_var_screeninfo v; struct fb_fix_screeninfo f;
    if(ioctl(fd, FBIOGET_VSCREENINFO, &v)<0){ perror("VSCREENINFO"); return 1; }
    if(ioctl(fd, FBIOGET_FSCREENINFO, &f)<0){ perror("FSCREENINFO"); return 1; }
    int W=v.xres, H=v.yres, bpp=v.bits_per_pixel;
    int stride = f.line_length;          // 字节/行
    long frame_sz = (long)stride*H;
    long map_sz = f.smem_len ? f.smem_len : frame_sz*3;
    printf("fb: %dx%d %dbpp stride=%d frame=%ld map=%ld xoff=%d yoff=%d yvirt=%d\n",
           W,H,bpp,stride,frame_sz,map_sz,v.xoffset,v.yoffset,v.yres_virtual);
    uint8_t* mem = mmap(0, map_sz, PROT_READ|PROT_WRITE, MAP_SHARED, fd, 0);
    if(mem==MAP_FAILED){ perror("mmap"); return 1; }

    // 往所有缓冲帧都画（应对三缓冲/page-flip，无论当前显示哪帧都能看到）
    int nframe = map_sz/frame_sz; if(nframe<1) nframe=1; if(nframe>3) nframe=3;
    for(int fr=0; fr<nframe; fr++){
        uint8_t* base = mem + (long)fr*frame_sz;
        for(int y=0;y<H;y++){
            uint32_t* row = (uint32_t*)(base + (long)y*stride);
            for(int x=0;x<W;x++){
                uint32_t c;
                // 四象限（native坐标）
                int left = x < W/2, top = y < H/2;
                if(top&&left)        c=rgb(255,0,0);     // 左上 红
                else if(top&&!left)  c=rgb(0,255,0);     // 右上 绿
                else if(!top&&left)  c=rgb(0,0,255);     // 左下 蓝
                else                 c=rgb(255,255,0);   // 右下 黄
                // 渐变叠加（按y）
                int g = (y*255)/H; c = (c & 0xFF000000) | (((c>>16&0xFF)*g/255)<<16)|(((c>>8&0xFF)*g/255)<<8)|((c&0xFF)*g/255);
                // 白色边框
                if(x<3||x>=W-3||y<3||y>=H-3) c=rgb(255,255,255);
                // native 原点(0,0) 附近画 20x20 青色方块（判断原点在屏幕哪个角）
                if(x<20&&y<20) c=rgb(0,255,255);
                row[x]=c;
            }
        }
    }
    printf("绘制完成：%d 帧，四象限(RG/BY)+边框+左上角青色原点标记\n", nframe);
    munmap(mem, map_sz); close(fd);
    return 0;
}
