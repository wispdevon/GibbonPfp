# Building and distributing GibbonPfp

## Platforms

| Target | Build environment | Distribution |
| --- | --- | --- |
| Windows 11 x64 | MSVC, Windows 2022 CI runner | NSIS installer and portable ZIP |
| Ubuntu 24.04+ x64 | Ubuntu 24.04 CI runner | AppImage |
| macOS 14+ Intel | macOS Intel CI runner | DMG |
| macOS 14+ Apple Silicon | macOS ARM CI runner | DMG |

Compatibility is bounded by the packaged dependencies, not just the C++ source.
See the actual [Actions runs](https://github.com/wispdevon/GibbonPfp/actions) for
current build/package results. A configuration alone is not proof of validation.

## Dependencies

Install Qt 6.8.3 with Qt Quick, Quick Controls, Network, and Concurrent, CMake 3.24+,
Ninja, Python 3.10+, and a C++20 compiler. For local builds, compatible system
OpenCV, ONNX Runtime, LibRaw, libheif, libtiff, and libwebp packages also work.

For release builds, clone vcpkg at the commit in `vcpkg.json`, bootstrap it, and set
`VCPKG_ROOT`. Use the checked-in dynamic triplet for your platform:

```bash
python3 scripts/fetch_assets.py
cmake --preset vcpkg -DGIBBON_BUNDLED_ORT=ON \
  -DVCPKG_TARGET_TRIPLET=x64-linux-dynamic \
  -DVCPKG_OVERLAY_TRIPLETS="$PWD/cmake/triplets"
cmake --build --preset vcpkg --parallel 3
ctest --test-dir build-release --output-on-failure
python3 scripts/test_cli.py build-release/gibbonpfp
```

Use `x64-windows`, `x64-osx-dynamic`, or `arm64-osx-dynamic` as appropriate.
The official CPU ONNX Runtime 1.23.2 archives are SHA-256 pinned; this avoids
requiring GPU drivers or compiling the entire runtime during every desktop build.
`GIBBON_BUNDLED_ORT=OFF` uses the system runtime for development.

Only face detection and Fast model weights are copied into packages. Run
`python3 scripts/fetch_assets.py --quality` for optional local quality testing.

## Packaging

```bash
cmake --install build-release --prefix "$PWD/dist/portable"
# Linux:
python3 scripts/prune_linux_runtime.py dist/portable
bash scripts/package_linux.sh build-release
# Windows (from build-release):
cpack -G ZIP
cpack -G NSIS
```

On macOS, use `hdiutil create` as in the workflow. Qt's deployment scripts collect
Qt/QML runtime modules. Windows packages also include app-local native DLLs.
Test the installed executable and its models, not only the build-directory copy.

`scripts/test_packaged.py dist/portable bin/gibbonpfp` copies the distribution to
a temporary location and removes SDK search paths before checking startup, RAW,
HEIC, crop review, batch exports, and large inputs. Use `bin/gibbonpfp.exe` on
Windows or `gibbonpfp.app/Contents/MacOS/gibbonpfp` on macOS. Windows validation
also checks DLL imports when MSVC's `dumpbin` is available. Linux packages use the
host's glibc/loader pair; do not add glibc to an AppImage.

Release artifacts must include font/model notices, native dependency copyright
files, and ONNX Runtime third-party notices. The vcpkg baseline identifies the
exact dependency source revisions and patches; retain that manifest alongside
the application source. Shared LGPL libraries remain replaceable. Do not enable
libheif's GPL HEVC encoder feature for this application; only decoding is needed.

## Signing

Unsigned packages are useful for testing but do not have a verified publisher.
macOS packages are ad-hoc signed to support local launch on Apple Silicon.

For public verified distribution, configure the optional signing scripts with:

- Windows: `WINDOWS_CERTIFICATE_BASE64`, `WINDOWS_CERTIFICATE_PASSWORD`.
- macOS: `APPLE_CERTIFICATE_BASE64`, `APPLE_CERTIFICATE_PASSWORD`,
  `APPLE_SIGNING_IDENTITY`, `APPLE_ID`, `APPLE_APP_PASSWORD`, and `APPLE_TEAM_ID`.

The workflow can use these repository secrets without putting credentials in
source control. Sign before packaging, then notarize and staple the macOS DMG.
Never describe an unsigned/ad-hoc package as notarized or publisher-verified.

## Release procedure

1. Run local core/controller tests, CLI integration, native UI inspection, and model checks.
2. Push a Conventional Commit to `main` and wait for all four build jobs.
3. Inspect packaged startup and codec checks. Fix failures before marking that
   platform validated.
4. Create a version tag and GitHub release, attaching only successful packages.
5. Include signing status, tested platforms, known limits, and package SHA-256 hashes.
