# Instant Camera / 拍立得

Native LVGL/V4L2 camera for the C1 Max's OV5648 and Ingenic ISP (`/dev/video4`). A dark, full-height viewfinder replaces the old white card. Four image ratios, six filters and four paper choices share one rendering pipeline: the crop, look and paper seen in the preview are also saved into the JPEG.

## Picture and paper

| Image ratio | JPEG image without paper | Composition |
| --- | --- | --- |
| 4:3 (default) | 972×729 | Landscape |
| 3:4 | 768×1024 | Portrait |
| 1:1 | 972×972 | Square |
| 16:9 | 960×540 | Wide frame |

Ratios describe the image **inside** the paper. Choices are **无边框 / No paper** (default), **白相纸 / White**, **奶油纸 / Cream**, and **黑胶片 / Black film** with sprocket marks. White and cream have a wider bottom margin. All three frames now use imagegen-created RGBA PNG artwork with genuine alpha, fine paper/film texture and shaped edges; the old procedural flat-color frames are replaced. Paper adds pixels around the image; it never stretches or covers the image. The selected frame is part of the saved JPEG, rather than only a UI decoration. PNG alpha is blended over a dark matte outside the picture (JPEG itself cannot preserve alpha). Frame corners and edges are mapped separately for all four ratios; incidental generated pixels in the aperture are excluded so the whole photograph stays clear.

Each shutter press freezes the current camera image, flashes the screen for **180 ms** with a short white peak and fade, then saves the unflashed image. A complete-frame framebuffer presenter prevents strip-by-strip flash tearing. Saving begins after an extra display interval. Startup failures do not flash or play a success sound; repeated triggers during capture and a short cooldown are ignored. A failed save remains an explicit error. The sound helper runs asynchronously, is reaped, has a 1.5 second timeout, and is stopped on exit. No audio sample is recorded from the microphone.

The six filters are Original, Mono, Sepia, Warm, Cool, and Faded/Vignette. Last ratio, paper and filter are saved in `$C1_APPS_DATA/camera/settings`. Selection sheets can be operated by touch or A/D, Enter and Back. The viewfinder occupies 568×340 pixels, fits the complete composition and uses a near-black background outside it. A portrait picture naturally leaves more unused screen width. The paper sheet uses thumbnails of the real overlays. Controls remain in a separate right-hand rail; startup/live/saving/saved status appears below its title, with resolution above the ratio buttons. Status text and its background never cover the live picture; camera failures still open an explicit error panel.

## Controls

- **Space**, the physical camera key, **Enter**, or the on-screen **拍摄** button: take a photo. While starting, wait for “实时取景”. A failed camera can be retried with the same shutter controls.
- **R**: cycle 4:3 → 3:4 → 1:1 → 16:9. Each ratio also has a direct touch button.
- **F** or left/right: cycle filters. Tap the filter control to choose from a sheet.
- **B**: cycle paper. Tap the paper control to choose from a sheet.
- **G** or **相册**: open the album. Use **A/D**, left/right or the previous/next buttons to browse.
- In the album, **返回拍摄**, Back, G, Enter, Space or the camera key returns to the viewfinder. Another shutter press after startup takes a new photo.
- **删除**, **X**, or top-right **Backspace** in the album opens a confirmation. Enter/删除 confirms; Back/取消 cancels. Other navigation is blocked during confirmation. Held Enter is suppressed after deletion.
- **Power**: return to the launcher and release the camera.

## Capture and hardware limits

The device's media graph reports **2048×1944** at the OV5648/ISP input. Capture requests **1024×972 NV12**, half that reported size on each axis, instead of nonuniformly scaling that input into the previous 640×480. Raw probes at 1024×972 and 1280×960 both streamed at approximately 15 fps on the test device. The ratio is then center-cropped in upright coordinates, with a 90° clockwise correction for this camera's mounting. Selecting a ratio does not restart the camera or change its ISP mode.

Only pixels needed for the viewfinder are converted from NV12 during live capture. A validated copy of the latest frame is retained before requeuing the V4L2 buffer; full RGB conversion occurs only at the shutter and is freed after encoding. This avoids reading ISP buffers while they are being written and keeps memory bounded. The 1024×972 source has about 3.24 times as many samples as the earlier 640×480 capture.

**16:9 is a crop, not an optical wide-angle mode.** The OV5648 is a [5 MP sensor capable of full-frame/windowed/binned output](https://www.ovt.com/press-releases/omnivision-launches-cost-competitive-5-megapixel-camerachiptrade-sensor-for-smartphones-and-tablets/), but that specification does not mean this device's current ISP exposes every native pixel or changes its lens field of view. This app does not reprogram sensor registers, enable full 5 MP capture, or provide autofocus controls. Physical pixel geometry outside the driver-reported mode has not been independently calibrated.

The `ispvideo` driver reports NV12 `bytesperline=1536` and `sizeimage=1492992` at 1024×972 although Y/UV rows are tightly packed at 1024 bytes. The existing stride correction is restricted to this driver and exact packed-size relationship; normal padded NV12/NV21 retain their reported stride. Actual dimensions, mmap sizes and payload lengths are checked. The first 800 ms and five frames are allowed for exposure startup. Four seconds without usable frames stops the camera and offers a retry. Capture only runs while the viewfinder is open; the app records no audio or video. The shutter plays a brief, original synthesized mechanical click at the existing system volume; volume zero remains silent.

## Files and album

Photos live in `$C1_APPS_DATA/camera/photos/` (normally `/storage/apps/data/camera/photos/`). New filenames use `print-YYYYMMDD-HHMMSS-XXXXXX.jpg`, with unique 0600 files. JPEG quality is 92. A photo is published only after writing, flushing and syncing succeeds; errors remove partial files. Photos can also be retrieved over ADB or the terminal.

The album reads JPEGs newest first and opens the latest capture. It fits one complete photo without cropping; empty, broken and missing files are handled. Browsing does not modify the original. Deletion is limited to the selected, unchanged regular JPEG, with device/inode/mtime checks and no symlink or outside-directory deletion. Input is bounded to 8 MiB, 2048×2048 pixels, and one decoded photo at a time. Camera capture stops in the album and restarts on return.

Legacy sideways 640×480 photos matching the exact `instant-YYYYMMDD-HHMMSS-XXXXXX.jpg` format receive a read-only clockwise correction in the album. Previously saved 480×640 photos, new `print-` landscape/portrait/square images and imported JPEGs keep their stored orientation. Old photos are neither rewritten nor recompressed.

## Validation and licenses

Run `bash camera/run-tests.sh` from the apps repository with a host C++ compiler and Python Pillow. ASan/UBSan cover NV12/NV21 colors and padding, short buffers, clockwise rotation, centered exact-ratio crops, paper bounds, shared preview/JPEG sampling, unique files, failed writes, album sorting/decoding/deletion and old/new orientation. Pillow independently decodes all 16 ratio/paper combinations and checks dimensions, center colors and paper colors.

On-device checks on 2026-09-24 exercised four ratios, all papers, filter selection, Space/camera-key/Enter/touch shutter events, the album, deletion confirmation and restarting with remembered settings. These are short functional checks and injected input events, not claims of long-term reliability, physical keycap testing or image quality in every lighting condition. Evidence and memory/timing details are in `../VALIDATION.md` and ignored `.build/camera-format-qa/`.

JPEG writing uses Sean Barrett's stb_image_write from commit `2c980bb59875b0d32144a71867fbdebb2f77cd20`, with a local unsigned JPEG bit buffer fix for UBSan. JPEG reading uses stb_image v2.30 from the pinned LVGL dependency, compiled for JPEG and the generated PNG frame assets; album admission still requires a JPEG signature. Both carry their public-domain/MIT terms in `licenses/`.

## Generated artwork and audio

- Built-in **image_gen**, one generation call per frame; final prompts: [`frame-prompts.json`](frame-prompts.json).
- Original RGBA files (preserved alpha): [`white.png`](assets/frames/white.png), [`cream.png`](assets/frames/cream.png), [`film.png`](assets/frames/film.png). Each has a `.window` sidecar describing its transparent aperture. No Python-drawn replacement artwork is used.
- Runtime decoding keeps a bounded 512×512 texture per used style; full generated originals remain in the package. Preview, paper thumbnails and JPEG export share the alpha compositor.
- [`tools/make-shutter-sound.py`](tools/make-shutter-sound.py) deterministically synthesizes the original 48 kHz stereo `assets/shutter.wav`; no external audio samples or licenses are needed.
