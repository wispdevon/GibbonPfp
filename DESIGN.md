# GibbonPfp Design

GibbonPfp adapts the portable visual language from Strider's `DESIGN.md` to a
native Qt Quick workspace. It is an editing tool, with no account or web service.

## Foundation

Use Inter for work, Space Grotesk for identity/headings, and Geist Mono for pixel
dimensions and numeric zoom readouts. Fonts are bundled under the SIL Open Font License.
Use Medium (500) for work text and controls, DemiBold (600) for actions, and Bold
(700) for headings. Explicitly apply these fonts to dropdown delegates, menus,
fields, checkboxes, tooltips, and app dialogs. Validate bundled font loading at
startup. Native operating-system file dialogs retain their platform typography.

| Token | Light | Dark |
| --- | --- | --- |
| Canvas | `#efece6` | `#151617` |
| Foreground | `#17181b` | `#f2f0ea` |
| Panel | `#f8f6f2` | `#202224` |
| Strong surface | `#e7e0d5` | `#2a2d30` |
| Muted text | `#626970` | `#a6abb0` |
| Accent | `#1d1f23` | `#bfc9d1` |

The light muted text is slightly darker than Strider's base token for small
desktop labels. Use a subtle 18px dot grid on the continuous canvas, 8px control
corners, 12px panels, and 40px command targets. State colors stay small and always
have a text label. Theme follows the OS initially and persists locally.

## Work surface

Queue → Preview / Crop → Result → adjustments follows the work sequence.
The two image panes start at equal widths, with a draggable divider. Queue and
adjustments collapse independently and initially hide when logical space is limited.
Narrow workspaces show at most one sidebar. Adjustment sections are Frame, Refine,
Export. Dense regions scroll independently. Minimum window size is 1024 × 720. Avoid nested cards,
decorative heroes, pills for ordinary commands, and distracting idle animation.

Output previews are decoded from the actual JPEG/PNG bytes. The left pane shows
the finished export inside the editable crop, over a dimmed
full source. The right pane offers Result/Mask inspection and crop-coordinate
brushes. Both share one completed output revision; mark pending work as Updating.
Use a checkerboard behind transparent pixels. Keyboard navigation, tool labels,
visible focus, and non-drag crop zoom controls remain available. Crop zoom is
relative to each photo’s automatic frame, from 40–100%: 100% is the automatic
crop, and lower percentages widen it. Preserve the requested headroom fraction
while zooming where source space permits. Headroom changes recalculate automatic
framing without resetting the selected zoom. Keep the percentage through dragging,
undo, and sessions; report source-boundary limits. A Load sample button provides
a flat illustrated portrait with known head geometry for demonstrating framing.

Background removal uses a frame ten zoom percentage points wider than the export
(65% → 55%, including a removal-only 30% frame at the 40% slider minimum).
Preserve the current crop/headroom anchor, clamp context to source space, and map
the resulting mask back to the export crop before feathering and brush edits.
Both models use this margin; export geometry and brush coordinates stay unchanged.

Edge feather uses a live Geist Mono numeric readout in output pixels, with 0.5 px
steps from 0–10. Default to zero added blur; preserve explicit saved values.
Feather the automatic mask before manual Keep/Remove strokes, which retain firm
edges. Inspect masks without display smoothing or pre-enlargement.
Fast uses MODNet at 512 × 512 and reconstructs its prediction against a source crop bounded to
1600 px on the long edge before export resizing. Use image-guided refinement,
solid confidence endpoints, and replicated borders to avoid artificial edge haze.

Export includes a checked-by-default Sharpen for screen control, with Low, Standard,
and High levels; Standard is the default. Apply sharpening in the shared core
after resizing and brightness, before background composition and encoding.

## Appearance

Appearance contains theme, UI scale (80, 90, 100, 110, 125, 150%), and button
accent (Graphite or Blue). Persist these with QSettings, separate from image data.
Default to 100% and Graphite. Scale the logical QML workspace together, above Qt
display scaling; leave native file dialogs and export dimensions unchanged.
Blue uses `#315F86` in light mode and `#91B8D8` in dark mode. Section headings
remain graphite, bold Space Grotesk at 16px, with stronger separators. Primary
actions use solid accent fills; secondary actions have strong borders and visible
hover and keyboard-focus states. Keep both image panes visible at all scales.

## Review behavior

Never present estimated headroom as a professional identity-photo standard.
Show the reason when a crop needs review. An explicit per-photo approval or the
fully automatic policy releases the hold. Settings changes invalidate approval.

## High Quality model loading

Before the desktop first loads High Quality into memory, present a modal
Load High Quality? dialog describing its estimated RAM use, automatic CUDA selection, and CPU fallback.
Offer Cancel and Load High Quality. Gate every processing entry point, including
loaded presets/sessions and batch exports; do not start the pending operation
until confirmed. Keep the model session cached until app exit, with no repeated
prompts after a successful load. Failed loads may prompt again on retry. This
confirmation is separate from per-photo framing approval and is not persisted.

## Inference devices

Prefer NVIDIA CUDA when the runtime supports it, retaining CPU fallback. Show
actual session device status in Models. Keep device selection separate from
photo settings; `GIBBON_INFERENCE_DEVICE=cpu` forces CPU until app exit.
