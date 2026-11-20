# HeadshotFlow Design

HeadshotFlow adapts the [shared workspace design](docs/shared/DESIGN.md) to a
native Qt Quick portrait editor. This file specifies the desktop adaptation.

## Foundation

Use Inter for work, Space Grotesk for identity/headings, and Geist Mono for pixel
dimensions and numeric zoom readouts. Fonts are bundled under the SIL Open Font License.
Use Medium (500) for work text and controls, DemiBold (600) for actions, and Bold
(700) for headings. Explicitly apply these fonts to dropdown delegates, menus,
fields, checkboxes, tooltips, and app dialogs. Validate bundled font loading at
startup. Native operating-system file dialogs retain their platform typography.

Use the shared palette, dot grid, and control geometry. Use `#d3cec5` for light
separators and `#414447` for dark separators. Keep status labels legible at the
smallest supported UI scale.

## Work surface

Queue → Preview / Crop → Result → adjustments follows the work sequence.
The two image panes start at equal widths, with a draggable divider. Queue and
adjustments collapse independently and initially hide when logical space is limited.
Narrow workspaces show at most one sidebar. Adjustment sections are Frame, Refine,
Export. Dense regions scroll independently. Minimum window size is 1024 × 720. Avoid nested cards,
decorative heroes, pills for ordinary commands, and distracting idle animation.
Place **Open photos…** and **Load sample** in the empty crop canvas.

Output previews are decoded from the actual JPEG/PNG bytes. The left pane shows
the finished export inside the editable crop, over a dimmed
full source. The right pane offers hold-to-view mask inspection and crop-coordinate
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
Keep the reason beside the approval action when a crop needs review. Allow the
reason to wrap in a compact workspace. An explicit per-photo approval or the
fully automatic policy releases the hold. Settings changes invalidate approval.

## High Quality model loading

Before the desktop first loads High Quality into memory, present a modal
Load High Quality? dialog describing its estimated RAM use, automatic CUDA selection, and CPU fallback.
Offer Cancel and Load High Quality. Gate every processing entry point, including
loaded presets/sessions and batch exports; do not start the pending operation
until confirmed. Keep the model session cached until release or app exit, with no repeated
prompts after a successful load. Failed loads may prompt again on retry. This
confirmation is separate from per-photo framing approval and is not persisted.

## Inference devices

Prefer NVIDIA CUDA when the runtime supports it, retaining CPU fallback. Show
actual session device status in Models. Keep device selection separate from
photo settings; `GIBBON_INFERENCE_DEVICE=cpu` forces CPU until app exit.

Models offers Automatic / CPU with a persistent local preference. A CPU launch
override has priority and is explained in place. Show per-model devices and readable
fallback reasons with expandable technical details. Release loaded models and
device changes clear sessions/caches, preserve previews and edits, and reset High
Quality consent. Disable these controls during work.

## Workspace recovery

Offer Restore / Start fresh on startup whenever recovery files exist, including
after clean exit. Explain fallback to the previous valid snapshot and disable
Restore if both are corrupt. Restore metadata before processing; missing files
stay in the queue and changed source fingerprints invalidate approval. Display
a persistent autosave error when the lock cannot be acquired or a write fails.
High Quality consent and processing caches never persist.

## Queue application and ZIP export

Factory settings use a 360 × 480 cap, 70% crop zoom, 8% headroom, Fast removal,
white JPEG at quality 80, and Standard screen sharpening. Edits affect the open
photo. **Apply to all** confirms its scope before copying the open photo’s
controls to every queued photo, clearing their manual framing and strokes,
invalidating approvals, and setting defaults for future imports. Refresh the
open photo immediately and other photos when viewed or processed. Undo remains
per photo.

Opening a photo and checking it are independent. The queue filter controls
visibility. The selected count and **Prepare selected**, **Remove selected**, and
**Export selected** include checked photos hidden by the filter. Show the hidden
count when a filter hides checked photos. Disable the commands when
the checked set is empty. **Export current** uses the open photo.
Folder exports write each successful photo as it completes and report holds or
failures. Completed output stays on disk after cancellation.

Hold to view mask works with pointer or keyboard Space, returning to the composite
on release, focus loss, or photo change. Selecting brushes does not latch mask view.
**Export queue to ZIP** lives in Export controls and uses the native save dialog.
It includes all queue entries. Every photo must pass processing and review before
the archive is atomically published. Cancellation and failures leave the existing
destination intact. Include an export report.
