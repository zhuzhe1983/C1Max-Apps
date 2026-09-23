# Original MPlayer raw YUV tap

`src/yuv_pipe.c` interposes the original program's dynamically imported
`avcodec_decode_video2`. It calls the real decoder first, leaves its return value,
AVFrame and got-picture flag intact, and copies completed progressive YUV420P
frames to `C1_YUV_FIFO` as a single YUV4MPEG2 stream. It does not replace audio,
use a PNG encoder, or access a framebuffer. Run the original MPlayer with `-vo
null`; the application owns the sole video compositor.

The hook uses the original headers from FFmpeg 4.2 and rejects any runtime
`avutil_version()` except **56.31.100 before accessing AVFrame fields**. Headers
were reduced to the 16 actual transitive dependencies reported by the compiler.
Their official source archive and individual SHA-256 values are recorded in
`vendor/ffmpeg42/SOURCE.json`; its LGPL 2.1 license is retained alongside them.
No FFmpeg library is linked into the shim.

Build from the repository root:

```sh
docker run --rm -v "$PWD/apps:/work" -w /work \
  c1max-apps-builder:bookworm \
  sh streamplayer/tools/build-yuv-pipe.sh /work/.build/mips/c1max-yuv-pipe.so
```

The script uses `-mabi=32 -march=mips32r2 -mfp64 -mnan=2008 -shared -fPIC
-nostdlib`. This matches the supplied stock executable: ELF ABI version 3,
NaN2008, O32, hard float with a 64-bit FPU. The result has no `DT_NEEDED` or GLIBC
symbol-version requirements. The toolchain's missing NaN2008 libc stub header
is supplied through a **temporary** symlink to its ordinary hard-float stub;
no installed toolchain files are modified and no toolchain libc is linked.
The shim calls no floating-point libc interface. Original MPlayer already loads
the device's libc and libdl, which supply the unresolved runtime symbols.

The application creates a private FIFO, opens it `O_RDWR|O_NONBLOCK|O_CLOEXEC`,
then sets these variables in only the child process before exec:

```text
LD_PRELOAD=/storage/apps/current/streamplayer/c1max-yuv-pipe.so
C1_YUV_FIFO=/storage/apps/data/streamplayer/frames-PID.y4m
```

All currently authorized tests must use **`-ao null`**. Normal audio output must
remain disabled until the user authorizes an audio test. The hook does not change
the existing audio implementation, but retaining code paths does not demonstrate
audio/video synchronization.

The hook writes Y/U/V planes tightly packed, strips line padding, accepts negative
stride, and preserves sample aspect ratio. Limit: even dimensions up to 512×288,
8-bit planar YUV420P, progressive frames, fixed geometry per player process.
Frame-size changes, other formats, and interlaced video fail explicitly. Header
FPS is the application's server-transcode contract of 20 fps, not a guessed
device clock. Filters such as scaling/cropping in MPlayer run **after** this tap;
request the required size from the server and do display scaling in the app.

FIFO open is nonblocking, symlink substitution is rejected, and `F_GETPIPE_SZ`
confirms an actual pipe before any bytes are written. Writes become blocking to
preserve frame boundaries; the application must continuously drain the FIFO.
Closing its reader reports an error and disables further frame emission without
terminating the real decoder with SIGPIPE. Signal masking is restored, and only
a newly generated SIGPIPE is consumed. The writer buffer is bounded at about
216 KiB. MPlayer is single-client: do not share this FIFO between processes.

Errors are emitted once to stderr as `C1_YUV_ERROR=<reason>`, where reason is:
`missing_decoder`, `abi_mismatch`, `unsupported_format`, `invalid_geometry`,
`interlaced`, `invalid_stride`, `geometry_changed`, `open_fifo`, `not_fifo`,
`write_fifo`, or `signal_mask`. Decoder behavior continues unchanged after output
failure so the parent can perform its existing orderly playback cleanup.

**Timing limitation:** this hook emits immediately after decode and before the
original VO presentation/sleep path. Original loop pacing remains in place, but
a displayed frame may be early. A buffered FIFO can accumulate additional delay.
No extra delay is applied in this implementation. Silent frame-rate/pause tests
do not prove sound-picture alignment; an authorized audio comparison is required.

## Host verification

`tests/yuv_pipe_probe.py` uses a separately built **real FFmpeg 4.2** decoder,
not a fabricated AVFrame or the incompatible FFmpeg 5.x decoder ABI. It generates
silent synthetic H.264, intercepts 20 decoded frames, and compares every emitted
YUV byte against independent `av_image_copy_to_buffer` output. It also disconnects
the FIFO mid-stream and checks that the error appears while all 20 frames still
decode and the process exits normally. Both tests passed on native Linux.

For the current host, FFmpeg 4.2 shared libraries and headers are available at
`/tmp/c1max-yuv-native/ffmpeg`. Reproduce with:

```sh
docker run --rm -v "$PWD/apps:/work:ro" \
  -v /tmp/c1max-yuv-native/ffmpeg:/ffmpeg:ro \
  -e C1_FFMPEG42_PREFIX=/ffmpeg -w /work \
  c1max-video-probe:bookworm python3 streamplayer/tests/yuv_pipe_probe.py
```

The host decoder test does not reproduce MPlayer's entire scheduling loop or
device sound output. No ADB or device playback is performed by these scripts.

## MPlayer 1.4 timing and pause/seek review (2026-09-23)

The host source review used the official
[MPlayer 1.4 release archive](https://mplayerhq.hu/MPlayer/releases/MPlayer-1.4.tar.xz),
SHA-256 `82596ed558478d28248c7bc3828eb09e6948c099bbd76bb7ee745a0e3275b548`.
Line numbers below refer to that unmodified archive, not a later MPlayer version
or the device vendor's unpublished changes.

The main loop calls `update_video`/decode at `mplayer.c:3813`, waits in
`sleep_until_update` at line 3884, calls the VO's `flip_page` at line 3894,
and then adjusts synchronization at line 3902. A buffered frame prevents another
decode while the loop finishes waiting. `sleep_until_update` at lines 2232–2307
uses the audio driver's delay when audio is present; its video-only path uses the
remaining frame interval. The decode hook therefore retains the main loop's
frame-rate pacing. It does **not** turn normal playback into unrestricted decode.

The tap still sends the current frame **before** that frame's presentation wait.
During steady 20 fps playback this normally means up to approximately one frame
(50 ms) of early delivery, with decoder, IPC and compositor jitter. This is an
estimate for steady playback, not a hard synchronization bound: startup,
timestamp discontinuities and resynchronization may require longer waits, and
FIFO backlog adds delay. The stock `libvo/vo_yuv4mpeg.c:185–190` writes its frame
in **`flip_page`**, after the wait; it is not the same extraction point as this
hook. Also, `libmpcodecs/dec_video.c:448–449` may discard a decoded frame after the
hook has already emitted it, so soft frame dropping is not reproduced exactly.

The integration owner observed roughly 98 frames in 5 seconds on the device,
unchanged frame counts during a pause longer than 18 seconds, and approximately
20 fps after resuming, using `C1_STREAMPLAYER_SILENT=1` (`-ao null`) with the mixer
at zero. These observations support frame pacing and pause behavior. **No audible
playback or audio/video alignment test was performed in this round.** The hook
remains at its tested extraction point. A future authorized audio comparison
must quantify any offset before adding a presentation delay.

Holding a frame until the next decode call would usually place its delivery
after the preceding loop's presentation wait, but it is not a universal timing
fix: a pause can prevent the next call, EOF may leave the final frame pending,
and dropped frames/filter buffering break a simple one-call/one-presentation
assumption. This alternative is deliberately not implemented.

Two independent pause/seek races were identified and corrected in `main.cpp`:

1. A queued `ANS_pause=no` response could overwrite a more recent local pause
   request, so a seek inherited the wrong state. Pause intent is now owned by the
   application; periodic queries do not overwrite it. The stock 1.4 pause
   property is **read-only** (`command.c:644–648` calls `m_property_flag_ro`), so
   `set_property pause yes/no` is not a valid replacement for the pause command.
2. During asynchronous seek, the old frame/time stayed visible after the decoder
   was stopped. Those old values could satisfy the deferred-pause condition,
   silently write to no decoder, and consume `pause_on_start`. Consumption now
   also requires `player > 0 && !restarting`, along with a valid time and a frame
   from the replacement decoder.

Regression cases for this state transition are: keep a pending pause through
repeated seek-wait polls and a stale pause reply; do not consume it with only a
new time or only a new frame; send it once when the replacement has both; allow
an explicit Play request during the wait to cancel it; preserve pause intent
through consecutive seeks. These cases describe the required behavior and do
not by themselves claim additional device test coverage.
