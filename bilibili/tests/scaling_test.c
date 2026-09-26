/* Host test of the actual shared decode tap: source bounds and point scaling.
 * A regular temporary output replaces IPC; decoder ABI checks are covered by
 * the on-device test, not bypassed in production. */
#define _GNU_SOURCE
#include "../../streamplayer/src/yuv_pipe.c"
#include <assert.h>
static void run(int w,int h,int scaling,int reject){
    size_t size=(size_t)w*h*3/2;unsigned char*src=malloc(size);assert(src);
    for(int y=0;y<h;y++)for(int x=0;x<w;x++)src[y*w+x]=(unsigned char)(x+y);
    memset(src+(size_t)w*h,128,(size_t)w*h/2);
    AVFrame f={0};f.width=w;f.height=h;f.format=AV_PIX_FMT_YUV420P;f.sample_aspect_ratio.num=f.sample_aspect_ratio.den=1;
    f.data[0]=src;f.data[1]=src+(size_t)w*h;f.data[2]=f.data[1]+(size_t)w*h/4;
    f.linesize[0]=w;f.linesize[1]=f.linesize[2]=w/2;
    FILE*out=tmpfile();assert(out);output_fd=dup(fileno(out));checked=1;failed=0;width=height=source_width=source_height=0;
    setenv("C1_YUV_FIFO","/unused-test-fifo",1);if(scaling)setenv("C1_YUV_SCALE","1",1);else unsetenv("C1_YUV_SCALE");
    emit_frame(&f);
    if(reject){assert(failed);assert(ftell(out)==0);}else{
        assert(!failed&&width<=512&&height<=288);rewind(out);char head[256];assert(fgets(head,sizeof head,out));int ow=0,oh=0;assert(sscanf(head,"YUV4MPEG2 W%d H%d",&ow,&oh)==2);assert(ow==width&&oh==height);assert(fgets(head,sizeof head,out));assert(strncmp(head,"FRAME ",6)==0);
        for(int y=0;y<height;y++)for(int x=0;x<width;x++)assert(fgetc(out)==(unsigned char)(x*w/width+y*h/height));
        for(int i=0;i<width*height/2;i++)assert(fgetc(out)==128);assert(fgetc(out)==EOF);
        close(output_fd);
    }
    output_fd=-1;fclose(out);free(src);
}
int main(void){run(400,224,0,0);run(640,360,0,1);run(640,360,1,0);run(360,640,1,0);run(1280,720,1,1);puts("PASS unchanged native-size frames, opt-in scaling, portrait geometry, pixel mapping and oversized rejection");}
