# C1Max Launcher / Quiet objects

2026-09-23. Uses ui-ux-pro-max's contrast, touch, focus, spacing and typography
guidelines, adapted to a fixed 800×340 MIPS framebuffer. Search recommendations
for landing-page CTAs, React Native, phone breakpoints and virtual keyboards do
not apply to this physical-keyboard appliance.

- Retain four columns, two rows, eight apps per page and horizontal swipes.
- Floating sculpted icons on real alpha; never draw an opaque icon backplate.
- Quiet charcoal background, one desaturated mint focus outline. Unselected
  apps have no card border. Press feedback darkens the selected surface without
  moving icons or hit targets. No continuously animated decorations.
- Noto Sans SC, antialiased: title 26px, app labels 20px, secondary 16px, keycaps
  14px. Preserve Chinese and English; truncate overlong labels with an ellipsis.
- Semantic colors: canvas #101923, selected #1B3441, pressed #244653,
  focus #A6DED1, primary text #F0F4F5, secondary #B2C0CC, disabled #7A8C99.
- Outer horizontal margins 20px, 8px between 182×112 app targets. Header 56px,
  rows start at 60/180px. Label top 138/258px. Footer starts at 304px.
- Page arrows have 44×44 targets; Q–I opens an app, A/D/swipe changes page,
  Power returns to stock. At boundaries arrows are disabled; empty slots do
  nothing. The selection survives returning from a child application.
- Rendering only on change. Cache only the current page's icons and bound the
  font glyph cache. Reuse the existing licensed system font through mmap.
- Validate actual 800×340 output, transparent icons over different colors,
  missing icons, Chinese, long names, 0/7/9/64 entries and press feedback.

No screen reader or system text-scaling facility exists in this framebuffer
application; do not claim web/native mobile accessibility APIs are implemented.
