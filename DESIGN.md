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

Queue → preview → adjustments follows the work sequence. Adjustment sections are
Frame, Refine, Export. Dense regions scroll independently. Minimum window size
is 1024 × 720, with narrower queue and controls below 1200px. Avoid nested cards,
decorative heroes, pills for ordinary commands, and distracting idle animation.

Output previews are decoded from the actual JPEG/PNG bytes. Original, crop, and
mask views must be clearly distinguished. Keyboard navigation, tool labels,
visible focus, and non-drag crop zoom controls remain available.

## Review behavior

Never present estimated headroom as a professional identity-photo standard.
Show the reason when a crop needs review. An explicit per-photo approval or the
fully automatic policy releases the hold. Settings changes invalidate approval.
