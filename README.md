# GibbonPfp

GibbonPfp is an offline Qt desktop app and command-line tool for turning full-size
portraits into consistent profile pictures. Import a single photo or a batch,
adjust framing and perceived brightness, optionally remove the background, and
export without changing the originals.

## Features

- Portrait **3:4**, capped at **360 × 480 pixels** by default (width × height).
- Face-aware auto-crop with adjustable headroom and a review queue for uncertain framing.
- Single-photo editing, folder import, selection-based batches, and fully automatic mode.
- Gentle perceptual brightness adjustments and reference-portrait brightness matching.
- Offline background removal: bundled Fast model and optional High Quality portrait model.
- Mask inspection, keep/remove brushes, feathering, and transparent PNG or solid-background JPEG.
- JPEG, PNG, WebP, TIFF, HEIC, and camera RAW input.
- RAW white-balance presets, temperature/tint, and highlight recovery.
- Original crop dimensions or custom 3:4 sizes when the default cap is disabled; no automatic upscaling.
- Reusable presets, editable sessions, undo/reset, filename prefixes, and collision-safe exports.
- Light and dark themes adapted from Strider's editorial workspace design.
- Shared processing engine for the desktop interface and CLI.

## Tech Stack

- C++20 and Qt 6 Quick/QML
- CMake and Ninja
- OpenCV core/imgproc
- ONNX Runtime CPU inference
- LibRaw, libheif/libde265, libtiff, and libwebp
- GitHub Actions and vcpkg for desktop builds

## Getting Started

Download a package for your operating system from
[Releases](https://github.com/wispdevon/GibbonPfp/releases).
Release targets are Windows 11 x64, Ubuntu 24.04+ x64, and macOS 14+ on Intel and
Apple Silicon. Packages contain their runtime dependencies and the small default
models. See [Building and distribution](docs/BUILDING.md) for platform validation
and signing details.

For development, install Qt 6.8+ and the native dependencies, then run:

```bash
python3 scripts/fetch_assets.py
cmake --preset dev
cmake --build --preset dev
ctest --preset dev
./build/gibbonpfp
```

On macOS, the executable is inside `build/gibbonpfp.app/Contents/MacOS/`.
On Windows, use `build/gibbonpfp.exe`. No Python interpreter is needed by the
installed application; Python is used only by development scripts.

### Desktop workflow

1. Add photos or a folder. Folder import includes subfolders.
2. Select a portrait. Auto-crop prepares a 3:4 frame with approximately 8% headroom.
3. Use **Crop** view to drag the frame; adjust zoom, brightness, or backgrounds.
4. Use **Apply settings to selected** to share adjustments. Individual crops stay
   with their original photos; mask strokes are not copied between subjects.
5. **Prepare selected** identifies exceptions. Adjust or approve photos marked
   **Needs review**, or enable **Fully automatic batch** and apply it to the selection.
6. Export the current photo or selected batch into an output folder.

The Output view displays the decoded export bytes. Original and Crop views show
the developed source. Mask brushes operate on the current crop; changing framing
clears the strokes. Brightness and RAW controls take effect after the
slider is released. **Refresh preview** explicitly recomputes the current settings.

## Commands

```bash
# Prepare a batch; uncertain photos are held and reported (exit code 2).
gibbonpfp process photos/ --output profiles/ --report report.json

# Fully automatic fallback: largest face, or center crop if none is found.
gibbonpfp process photos/ --output profiles/ --fully-automatic --recursive

# Fast, local background removal and transparency.
gibbonpfp process portrait.jpg --output profiles/ --background fast --format png

# Keep full crop resolution, or choose a custom 3:4 size.
gibbonpfp process portrait.jpg --output profiles/ --uncapped
gibbonpfp process portrait.jpg --output profiles/ --uncapped --size 720x960

# Gentle brightness and reference matching.
gibbonpfp process photos/ --output profiles/ --brightness 0.3
gibbonpfp process photos/ --output profiles/ --reference reference.jpg

# RAW development, followed by the same portrait pipeline.
gibbonpfp process portrait.NEF --output profiles/ --white-balance custom \
  --temperature 5500 --tint 1.05 --highlight 3

# Install the optional 973 MB model, then use it entirely offline.
gibbonpfp models list
gibbonpfp models install quality
gibbonpfp process portrait.jpg --output profiles/ --background quality --format png

# Saved desktop presets work with the CLI. Reports support JSON and CSV.
gibbonpfp process photos/ --output profiles/ --preset preset.json --report report.csv
gibbonpfp --help

# Development checks.
cmake --build --preset dev
ctest --preset dev
python3 scripts/test_cli.py build/gibbonpfp
```

Exit codes: **0** complete success; **2** held photos or per-file failures;
**1** invalid options or a fatal error; **130** interrupted processing. Each
processed file produces one JSON result line on stdout. Reports include source,
output, status, and warnings/errors. Existing output names receive numeric suffixes.

## Configuration

Presets are versioned JSON shared by the GUI and CLI. Save one from the desktop
to get the complete supported settings shape. Important options include:

| Setting | Default | Behavior |
| --- | --- | --- |
| `autoCrop` | `true` | Estimate a head-and-shoulders frame |
| `headroom` | `0.08` | Fraction above estimated head top; adjustable 0–0.25 |
| `capped` | `true` | Maximum 360 × 480 |
| `width`, `height` | `360`, `480` | Exact 3:4; use both `0` for source-sized uncapped output |
| `brightness` | `0` | Gentle lightness shift, from −1 to 1 |
| `background` | `off` | `off`, `fast`, or `quality` |
| `format`, `quality` | `jpeg`, `92` | JPEG or PNG export |
| `automatic` | `false` | Allow uncertain crop fallbacks to export |

`GIBBON_MODEL_DIR` adds a model lookup directory. Models are checked against the
bundled SHA-256 manifest. Missing or damaged models fail with a repair instruction.
Downloads use HTTPS and are committed only after checksum verification.

## Project Structure

```text
src/                 Shared image engine, codecs, model runtime, CLI, Qt controller
qml/                 Desktop workspace and theme tokens
assets/              Fonts, icon, model manifest, and notices
tests/               Core and controller regression tests
scripts/             Asset downloads, CLI tests, packaging, and license collection
cmake/               Pinned runtime setup and platform dependency triplets
docs/                Build, release, and validation notes
.github/workflows/   Desktop build/test/package matrix
```

## Data

Photos remain on disk and are never overwritten. Sessions contain source paths,
per-photo settings, selection, approvals, and mask strokes; they do not embed the
photos. Move the original files only after finishing a session. Presets omit
photo-specific crops, strokes, approvals, and reference paths.

The OS configuration directory stores the theme. Downloaded models live in the
Qt application-local data directory under `models/`. Exports remove source EXIF,
GPS, and other metadata and include an sRGB profile. Batch exports also write a
JSON report in the destination folder.

## Distribution

See [docs/BUILDING.md](docs/BUILDING.md) for reproducible builds, packaging,
dependency licenses, and optional signing. CI builds each platform natively;
Windows and macOS binaries are not produced by pretending a Linux build is portable.

## Design

The visual system is documented in [DESIGN.md](DESIGN.md). GibbonPfp uses Strider's
warm paper and graphite palette, titanium emphasis, dotted canvas, restrained
corners, and work-first layout. Fonts are bundled for consistent offline rendering.

## Development Notes

- 8% headroom and 60% head height are adjustable composition heuristics, not ID-photo standards.
- Cropping can be uncertain with hats, unusual poses, cropped heads, or multiple people.
- Fast background removal may miss fine hair or similar-colored backgrounds.
  High Quality is optional and can require about **7 GB RAM** and tens of seconds
  per image on CPU; it is deliberately excluded from default packages.
- Brightness uses a monotonic Oklab lightness curve with endpoint protection and
  chroma reduction for gamut mapping. JPEG quantization remains lossy; the app does
  not promise that every tonal distinction survives compression.
- RAW rendering uses LibRaw, not the camera manufacturer's picture style. Camera
  support depends on the pinned LibRaw build. Temperature/tint are approximate
  photographic controls, and highlight recovery cannot restore clipped sensor data.
- SDR HEIC is supported. HDR HEIC with PQ/HLG transfer requires SDR conversion
  before import; the app reports this rather than silently applying an incorrect curve.
- Animated/multipage image workflows, GPU inference, and cloud services are outside v1.
- Cancellation is checked between processing stages and output rows. An active
  native decoder or model inference finishes its current call before cancellation.

## Privacy

GibbonPfp has no accounts, analytics, uploads, or cloud image processing. Only
explicit model installation uses the network. Session files and reports contain
local file paths; share them only when you intend to share that information.

## License

GibbonPfp is licensed under the [Apache License 2.0](LICENSE). Dependencies, fonts,
and model weights retain their licenses; see [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).

Built by [Devon Labs](https://devonlabs.space).
