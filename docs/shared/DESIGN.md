# Shared workspace design

Use this guidance for workspaces with dense controls and visual content. Each
project’s design document defines its own tools, layout, and validation paths.

## Typography and color

Use Space Grotesk Bold for product identity and section headings, Inter for
work text and controls, and monospace numerals for dimensions and measurements.
Use medium weight for work text and semibold for commands. Give controls and
dialogs the same type hierarchy as the main workspace.

| Semantic role | Light | Dark |
| --- | --- | --- |
| Canvas | `#efece6` | `#151617` |
| Primary text | `#17181b` | `#f2f0ea` |
| Panel | `#f8f6f2` | `#202224` |
| Strong surface | `#e7e0d5` | `#2a2d30` |
| Secondary text | `#626970` | `#a6abb0` |
| Graphite accent | `#1d1f23` | `#bfc9d1` |

These are starting values. Measure contrast against the actual surface in each
theme and interaction state. Aim for at least 4.5:1 for normal text and 3:1 for
large text and essential control boundaries. Use an accent to mark a primary
command, selection, or focus. Pair status colors with text or a clear symbol.
Start with the OS theme and retain a user’s explicit choice. Keep photo colors
independent of the interface theme.

## Surfaces and controls

- Keep the working canvas continuous. Where an 18 px dot grid fits the workspace,
  keep it faint and on the canvas alone. Use panel surfaces for groups that need
  separation and thin rules within them.
- Use 8 px control corners, 12 px panel corners, and command targets of at least
  40 logical pixels where space allows. Give primary actions a solid accent fill
  and secondary actions a visible border.
- Let the content lead. Put frequent controls in a stable position and allow
  dense regions to scroll independently. Keep preview geometry stable while
  loading and place empty states or errors in the affected work region.
- Give adjacent work panels a draggable divider with a visible keyboard focus
  state. Set minimum widths from the controls each panel must show and limit
  expansion so the neighboring panel stays usable. Retain a user’s chosen
  widths when sidebars are hidden and reopened.
- Group related commands in one toolbar and move occasional actions into a
  named menu. Keep text beside icons for import and export. Familiar actions
  such as previous and next can use icons alone with a tooltip, an accessible
  name, and the same keyboard and pointer target as a labeled button.
- Make focus visible, label controls, and offer keyboard alternatives to pointer
  and drag interactions. Restore focus when dialogs close. Allow text to wrap and
  preserve essential controls at smaller sizes and larger interface scales.
- Use motion for immediate interaction feedback and honor reduced-motion settings
  wherever motion is enabled. Verify the actual interface in both themes, with
  keyboard operation and across supported sizes.

## Reference projects

Strider contributes the warm paper and graphite palette, faint grid, and command
geometry. Devon Labs reinforces open layouts and original-color media. OpenCull
contributes photograph-first surfaces, contrast checks, and scale verification.
HeadshotFlow’s native Qt adaptation is in its [project design](../../DESIGN.md).
