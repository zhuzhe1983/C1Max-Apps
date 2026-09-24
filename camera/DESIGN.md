# Instant Camera — compact dark viewfinder

The user asked for four photographic ratios, selectable instant-paper frames,
and removal of the large white area surrounding the live picture. The design
was reviewed with `ui-ux-pro-max`; its generic website recommendations are
adapted to a fixed 800×340 native LVGL display and physical keyboard.

- The live image owns the left 568×340 pixels. Fit the complete selected crop
  and paper without stretching. Unused area is near-black, never a white card.
- The right 232 pixels contain the title, aperture resolution, four ratio
  buttons, film/paper selectors, shutter and album. Shutter is the sole warm
  primary action. Selection sheets expose secondary choices only when needed.
- Startup/live/saving/saved status is plain text below the title in the right
  rail. It never overlays the viewfinder; resolution sits above the ratio buttons.
- Background `#131918`, stage `#0b100f`, control surface `#222c29`, text
  `#f0f2e9`, secondary text `#aebbb4`, selected mint `#b8dbb7`, shutter `#ad4937`.
  Selected mint controls use dark text, an outline and a labeled ratio.
- Use the existing Noto Sans SC font at 18/16 px. No decorative lens illustration,
  decorative shadows, blur or expensive animations. Generated paper assets supply only the photo frame material. Touch controls are at
  least 44 px high; ratios have 48 px width. Pressed states change fill, not size.
- R/F/B cycle ratio/filter/paper; G opens the album; Space, Enter and the camera
  key shoot. Sheets support A/D, Enter and Back. Power returns to the launcher.
- No paper is the default. White/cream instant paper has a wider bottom margin;
  black film has sprocket marks. Generated alpha PNG frames are rendered into the JPEG, and their
  exact geometry is also used for the live preview. Ratio labels describe the
  image inside the paper. Last choices persist across app launches.
- Use the same dark palette for the album and deletion confirmation. Keep
  errors readable over the stage, and show startup/saving/saved status immediately.

Shutter feedback: a 180 ms white pulse (35 ms peak plus fade), with a quiet
two-part mechanical transient following system volume. Freeze the image first,
then show the pulse; never bake the white effect into the photo. Present complete
frames on an inactive framebuffer page to avoid visible stripes during the fade.
