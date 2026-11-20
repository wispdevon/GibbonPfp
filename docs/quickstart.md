# HeadshotFlow quickstart

Install HeadshotFlow from [Releases](https://github.com/wispdevon/GibbonPfp/releases)
or follow the [build instructions](BUILDING.md). The desktop launcher is named
**HeadshotFlow**. The terminal command is `gibbonpfp`.

## Export the built-in sample

1. Open HeadshotFlow and choose **Load sample** on the empty canvas or in the
   toolbar. The illustrated portrait opens in **Preview / Crop** on the left,
   with the encoded output in **Result** on the right.
2. Open **Adjustments** if it is hidden. In **Frame**, try **12% Headroom** and
   **60% Crop zoom** to reproduce the framing shown below. Lower zoom values
   include more of the source. You can drag the crop or focus it with **Tab**
   and use the arrow keys to position it.
3. Wait for the updated **Result**. Press and hold **Hold to view mask** with
   the pointer or focus the button and hold **Space**. Release to return to the
   output. **Fast · portrait** is the factory removal mode. Choose
   **Off · keep original** under **Refine → Background removal** to show the
   intact illustration as seen below.
4. Click **Export current** (shown as **Export** in compact layouts), then
   choose an output folder. Open the exported JPEG from that folder. Factory
   settings use a 360 × 480 portrait JPEG, quality 80, a white backdrop, and
   Standard screen sharpening. The folder also contains a JSON report.

![Native Linux HeadshotFlow workspace with the built-in sample and the adjustments panel open](images/workspace.png)

## Review and export your own photos

Choose **Open photos…** on the empty canvas or **Add photos** in the toolbar.
**Add folder** also imports photos within subfolders. Open a photo to edit it.
If the app shows **Needs review**, read the warning near the approval action and
adjust the frame as needed. When you are satisfied, choose **Approve current
framing** (or **Approve framing** in compact layouts).
Approval applies to one photo. Further edits to its settings clear approval.

| To do this | Use this command |
| --- | --- |
| Save the photo open in the editor | **Export current** |
| Check whether a set of photos is ready | Check their queue boxes and choose **Prepare selected** |
| Save that set to a folder | **Export selected** |
| Save every queued photo together | **Export queue to ZIP** under **Adjustments → Export** |

The selected count includes checked photos hidden by a filter. The queue reports
how many selected photos the filter hides. Use **All photos** to see the full set.
A folder export keeps any successfully saved photos if another photo is held or
fails, or if you cancel. A ZIP contains the whole queue and its report. It is
saved only when every photo can be exported and the archive is complete.

Each photo keeps its own settings. Confirm **Apply to all** to copy the open
photo’s controls to the entire queue and set defaults for future imports. This
clears manual framing, brush strokes, and approvals for every queued photo.

## Keyboard controls

| Key | Action |
| --- | --- |
| **Tab** / **Shift+Tab** | Move focus through controls |
| **Arrow keys** in Preview / Crop | Move the focused crop frame |
| **Space** on Hold to view mask | Inspect the mask until release |
| **Ctrl+Return** | Refresh the preview |
| **Ctrl+O** / **⌘O** | Open the Add photos dialog |
| **Ctrl+Z** / **⌘Z** | Undo the last change to the current photo |

See the [README](../README.md#desktop-usage) for mask refinement, appearance,
saved sessions, and model options.
