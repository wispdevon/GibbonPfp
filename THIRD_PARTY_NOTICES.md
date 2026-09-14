# Third-party software and assets

GibbonPfp application code is Apache-2.0. Dependencies and model weights retain
their own licenses. Distribution must retain the corresponding notices and
provide the source/relinking materials required by the selected licenses.

| Component | Upstream | License |
| --- | --- | --- |
| Qt 6 Core/Gui/Quick/Controls/Network/Concurrent | https://www.qt.io/ | LGPL-3.0 / commercial |
| OpenCV core/imgproc | https://github.com/opencv/opencv | Apache-2.0 |
| ONNX Runtime | https://github.com/microsoft/onnxruntime | MIT, with third-party notices |
| LibRaw | https://www.libraw.org/ | LGPL-2.1 or CDDL-1.0 |
| libheif | https://github.com/strukturag/libheif | LGPL-3.0 |
| libde265 decoder | https://github.com/strukturag/libde265 | LGPL-3.0 |
| YuNet 2023 model | https://github.com/opencv/opencv_zoo/tree/main/models/face_detection_yunet | MIT |
| PPHumanSeg 2023 model | https://github.com/opencv/opencv_zoo/tree/main/models/human_segmentation_pphumanseg | Apache-2.0 |
| BiRefNet portrait model | https://github.com/ZhengPeng7/BiRefNet | MIT |
| Inter, Space Grotesk, Geist Mono | https://github.com/google/fonts | SIL OFL-1.1 |

Model checksums and exact download URLs are in `assets/models/manifest.json`.
BiRefNet is distributed through the rembg release assets and installed only on
request. GibbonPfp does not embed Python, PyTorch, rembg, or cloud inference.
Fast model notices and font license texts are included in the source tree.

Release builds use shared dependencies. The vcpkg baseline fixes dependency
source revisions and includes their copyright files in release license bundles.
HEIC encoding is disabled; the GPL x265 encoder is not a dependency of the release
configuration. Qt image format plugins provide JPEG/PNG/WebP/TIFF decoding.
See `docs/BUILDING.md` for reproducible build and dependency source instructions.
