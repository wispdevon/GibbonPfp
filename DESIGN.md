# GibbonPfp Design

GibbonPfp adapts the portable visual language from Strider's `DESIGN.md` to a
native Qt Quick workspace. It is an editing tool, with no account or web service.

## Foundation

Use Inter for work, Space Grotesk for identity/headings, and Geist Mono for pixel
dimensions. Fonts are bundled under the SIL Open Font License.

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
visible focus, and non-drag crop zoom controls remain available.

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
