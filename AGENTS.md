# GibbonPfp

C++20 / Qt Quick desktop application with a shared CLI image engine. Follow
[DESIGN.md](DESIGN.md) for UI changes and [README.md](README.md) for build commands.

- Keep GUI and CLI processing in `gibbon_core`; never duplicate image math in QML.
- Default dimensions are width 360 × height 480, portrait 3:4.
- Preserve source photos and review gates; manual approval is per image.
- Keep model URLs and SHA-256 hashes pinned in the manifest. Do not commit ONNX files.
- Validate with CTest, CLI integration tests, and native Qt screenshots. Browser tools
  do not inspect this native desktop interface.
- Never claim clipping-free JPEG detail, perfect segmentation, universal RAW camera
  compatibility, or platform validation without evidence.
- Use Conventional Commit subjects. Do not commit build products or private photos.

## Local test installation

After each implemented update, run `scripts/install-local.sh` to build, validate,
and install the current workspace under `~/.local` for the user to test. This is
authorized by the user; do not ask again. Report failures rather than claiming the
installed copy was updated. Do not publish to AUR or push changes implicitly.
Keep `packaging/aur/PKGBUILD` and `.SRCINFO` in sync when packaging inputs change.
