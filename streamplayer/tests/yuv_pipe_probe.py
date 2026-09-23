#!/usr/bin/env python3
"""Real FFmpeg 4.2 H.264 decode/hook test; native Linux, silent, no MPlayer/ADB.
Requires gcc, ffmpeg (sample encoder), and C1_FFMPEG42_PREFIX shared libraries.
"""
import os, pathlib, select, signal, struct, subprocess, tempfile, time

ROOT = pathlib.Path(__file__).resolve().parents[1]
PREFIX = pathlib.Path(os.environ['C1_FFMPEG42_PREFIX'])

with tempfile.TemporaryDirectory(prefix='c1-yuv-probe-') as directory:
    work = pathlib.Path(directory)
    source = work/'decode.c'
    source.write_text(r'''
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/imgutils.h>
static void save(AVFrame *f, FILE *out) {
    uint8_t bytes[512*288*4];
    int n=av_image_copy_to_buffer(bytes,sizeof(bytes),(const uint8_t * const *)f->data,f->linesize,f->format,f->width,f->height,1);
    if(n<0 || fwrite(bytes,n,1,out)!=1) exit(8);
    usleep(50000);
}
int main(int argc,char**argv) {
    AVFormatContext *format=NULL; AVCodecContext *codec; AVFrame *frame=av_frame_alloc(); AVPacket packet;
    int stream,got=0,result,count=0; FILE *out;
    if(argc!=3) return 2;
    if(avformat_open_input(&format,argv[1],NULL,NULL)<0 || avformat_find_stream_info(format,NULL)<0) return 3;
    stream=av_find_best_stream(format,AVMEDIA_TYPE_VIDEO,-1,-1,NULL,0); if(stream<0)return 4;
    codec=avcodec_alloc_context3(NULL);avcodec_parameters_to_context(codec,format->streams[stream]->codecpar);
    if(avcodec_open2(codec,avcodec_find_decoder(codec->codec_id),NULL)<0)return 5;
    out=fopen(argv[2],"wb");if(!out)return 6;
    av_init_packet(&packet);
    while(av_read_frame(format,&packet)>=0) {
        if(packet.stream_index==stream) {
            result=avcodec_decode_video2(codec,frame,&got,&packet);if(result<0)return 7;
            if(got){save(frame,out);++count;}
        }
        av_packet_unref(&packet);
    }
    packet.data=NULL;packet.size=0;
    do {result=avcodec_decode_video2(codec,frame,&got,&packet);if(result<0)return 7;if(got){save(frame,out);++count;}}while(got);
    fclose(out);av_frame_free(&frame);avcodec_free_context(&codec);avformat_close_input(&format);
    printf("libavutil=%u frames=%d\n",avutil_version(),count);return count==20?0:9;
}
''')
    decoder=work/'decode'
    subprocess.run(['gcc','-std=c99','-D_DEFAULT_SOURCE','-Wno-deprecated-declarations',
                    '-I'+str(PREFIX/'include'),str(source),'-L'+str(PREFIX/'lib'),
                    '-Wl,-rpath,'+str(PREFIX/'lib'),'-lavformat','-lavcodec','-lavutil','-o',str(decoder)],check=True)
    (work/'libavutil').mkdir()
    (work/'libavutil/avconfig.h').write_text('#define AV_HAVE_BIGENDIAN 0\n#define AV_HAVE_FAST_UNALIGNED 0\n')
    shim=work/'c1max-yuv-pipe.so'
    subprocess.run(['gcc','-std=c99','-Os','-Wall','-Wextra','-Werror','-fPIC','-shared','-nostdlib',
                    '-fno-stack-protector','-fno-builtin','-I'+str(work),'-I'+str(ROOT/'vendor/ffmpeg42'),
                    str(ROOT/'src/yuv_pipe.c'),'-o',str(shim)],check=True)
    sample=work/'sample.h264'
    subprocess.run(['ffmpeg','-v','error','-f','lavfi','-i','testsrc2=size=302x170:rate=20','-frames:v','20',
                    '-an','-c:v','libx264','-pix_fmt','yuv420p','-profile:v','baseline','-bf','0','-f','h264',str(sample)],check=True)
    for broken in (False,True):
        fifo=work/('broken.fifo' if broken else 'frames.fifo');os.mkfifo(fifo)
        fd=os.open(fifo,os.O_RDWR|os.O_NONBLOCK)
        reference=work/('broken.yuv' if broken else 'reference.yuv')
        env=dict(os.environ,LD_PRELOAD=str(shim),C1_YUV_FIFO=str(fifo),LD_LIBRARY_PATH=str(PREFIX/'lib'))
        process=subprocess.Popen([str(decoder),str(sample),str(reference)],env=env,stdout=subprocess.PIPE,stderr=subprocess.PIPE,start_new_session=True)
        data=bytearray();deadline=time.monotonic()+10
        try:
            while process.poll() is None and time.monotonic()<deadline:
                if fd<0: time.sleep(.01);continue
                ready,_,_=select.select([fd],[],[],.02)
                if ready:data.extend(os.read(fd,65536))
                if broken and len(data)>3*(302*170*3//2):os.close(fd);fd=-1
            stdout,stderr=process.communicate(timeout=2)
            assert process.returncode==0,(stdout,stderr,process.returncode)
            if fd>=0:
                while True:
                    try:chunk=os.read(fd,65536)
                    except BlockingIOError:break
                    if not chunk:break
                    data.extend(chunk)
            if broken:
                assert b'C1_YUV_ERROR=write_fifo' in stderr,stderr
                print('Reader disconnect: EPIPE reported, SIGPIPE did not kill decoder; all 20 frames still decoded')
            else:
                header,raw=bytes(data).split(b'\n',1)
                assert b'W302 H170' in header and b'Ip' in header,header
                size=302*170*3//2;frames=[]
                while raw:
                    assert raw.startswith(b'FRAME\n') and len(raw)>=size+6
                    frames.append(raw[6:size+6]);raw=raw[size+6:]
                assert len(frames)==20 and b''.join(frames)==reference.read_bytes()
                assert b'C1_YUV_ERROR=' not in stderr,stderr
                print('Real FFmpeg 4.2 H.264: 20 Y4M frames exactly match independent av_image_copy_to_buffer reference')
                print(stdout.decode().strip())
        finally:
            if fd>=0:os.close(fd)
            if process.poll() is None:os.killpg(process.pid,signal.SIGKILL);process.wait()
