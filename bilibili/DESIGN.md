# C1 Bilibili — compact video browser

Adapted using ui-ux-pro-max for a fixed 800×340 LVGL display with touch and a
physical keyboard. wiliwili informs navigation and API flow; no desktop layout
or browser runtime is imported.

- Background #0e1018, raised surfaces #181b28, text #f5f5fa, secondary #aeb5c9,
  accent #fb7299. Accent marks selection and the single main action.
- Header 48 px, left rail 134 px, main content x=151..783, status footer y=314.
  Five 44 px navigation targets; three 204 px video cards with 192×108 covers.
- Noto Sans SC: body 18, supporting text 16, headings 24. Fallback glyphs use
  LVGL Montserrat. Video titles reserve two lines; author gets its own row.
- Search uses real keys, visible focus, Backspace deletion and distinct Back.
  Each network operation displays a status and supports Back to cancel.
- Bounded lists and asynchronous requests; no blur, animated backgrounds or
  virtual keyboard. Home can show a previously fetched cache during refresh.
- Playback reuses the single framebuffer compositor. Controls occupy the
  existing safe strips y=0..35 and y=218..339 and hide after 4.5 seconds.
- Launcher icon: genuine alpha PNG generated with built-in imagegen; full
  production prompt is stored in launcher/assets/bilibili-prompt.json.
