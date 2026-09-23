These are 16 unmodified FFmpeg 4.2 (`n4.2`) libavutil headers needed by
`src/yuv_pipe.c`, selected using the actual cross-compiler's `-M` dependency
output. They define the original AVFrame ABI; no guessed partial struct is used.

Source archive: https://ffmpeg.org/releases/ffmpeg-4.2.tar.xz

`SOURCE.json` records the archive SHA-256 and each retained header's SHA-256.
`COPYING.LGPLv2.1` is the upstream license. The platform-generated `avconfig.h`
is created in a temporary build directory for little-endian MIPS; it is not
represented as an original upstream source file.
