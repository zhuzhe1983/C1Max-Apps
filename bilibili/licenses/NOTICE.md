# C1 Bilibili

C1 Bilibili's API adapter is GPL-3.0-or-later. It references and adapts the WBI
permutation/signing and endpoint flows from **wiliwili**, copyright its authors:
https://github.com/xfangfang/wiliwili
Reference commit: 88e5876bea9502d06f46a8656e3530684d3aaf7d

Relevant files: wiliwili/source/api/util/wbi.cpp, search_api.cpp, mine_api.cpp,
video_detail_api.cpp, and wiliwili/include/api/bilibili/api.h.
The GUI is a C1-specific LVGL implementation, not the wiliwili GUI port.

Video output reuses C1Max StreamPlayer's complete-frame adapter and compositor.
QR generation is LVGL's bundled Nayuki QR-Code-generator (MIT; bundled notice).
