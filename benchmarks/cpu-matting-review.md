# CPU portrait matting review

November 2026. **PP-MattingV2 at 384 × 512 is the strongest new model to
evaluate.** It took 120 ms per warmed removal mask versus 248 ms for the
bundled MODNet in the same run. MODNet at 384 × 512 took 178 ms using the
existing weights. Continued evaluation should cover source and output aspect
ratios, framing, larger exports, and supported runtimes.

## Method

The C++ comparison used ONNX Runtime 1.29.0 with the CPU provider on an AMD
Ryzen 5 7500F, pinned to two physical cores. It used two inference threads,
one inter-op thread, all graph optimizations, and disabled intra-op spinning.
The corpus included the three bundled fictional portraits and three checksummed
public portraits with flyaway hair, gray hair, and a low-contrast jacket.
[Source photos and attribution](portraits.json)

Each model received the same app-derived crop and surrounding source context.
The app's 1600 px guide cap, guided refinement, and crop mapping to 360 × 480
were applied consistently. The reported median pools seven warmed repetitions
per portrait, with candidate order rotated between repetitions. Timings include
tensor preparation, inference, refinement, and mask mapping. Image decoding,
framing, model initialization, cache lookup, and export encoding were outside
the timed region. Composites were reviewed on light and dark backgrounds. The
local fixtures have no reference mattes. High Quality and GPU were outside
this comparison.

## CPU timings

| Run | Model input | Inference, ms | Removal mask, ms |
| --- | --- | ---: | ---: |
| A | Bundled MODNet FP32 512 × 512 | 208 | 221 |
| A | MODNet FP32 384 × 512 | 154 | 165 |
| A | MODNet FP32 384 × 384 | 115 | 125 |
| A | MODNet FP32 288 × 384 | 85 | 96 |
| A | MODNet FP32 320 × 320 | 79 | 90 |
| A | MODNet Int8 and UInt8 512 × 512 | 237 / 258 | 252 / 271 |
| A | PP-HumanSeg 192 × 192 | 7 | 18 |
| B | Bundled MODNet FP32 512 × 512 | 211 | 248 |
| B | MODNet FP32 384 × 512 | 155 | 178 |
| B | PP-MattingV2, native preprocessing | 163 | 193 |
| B | PP-MattingV2 384 × 512 | 100 | 120 |

PP-MattingV2 native preprocessing capped the short side at 512 px, then
rounded both sides down to multiples of 32. The fixed 384 × 512 candidate
used the same 3:4 grid and app preprocessing as the corresponding MODNet run.
[Conversion preprocessing](https://github.com/yakhyo/ppmatting/blob/b812d27/model/ppmatting_onnx.py)

## Visual review

- PP-MattingV2 retained more of the low-contrast jacket than the bundled
  MODNet. The fixed 384 × 512 configuration kept the pale hair near the
  microphone and left less visible background spill at the curly hair edge.
- MODNet 384 × 512 kept a fuller jacket than its 512-square baseline and
  retained hair detail on the test portraits. The 288 × 384 and 384 × 384
  configurations lost a large part of the same jacket.
- PP-HumanSeg softened the subject boundary on curls and shoulders and removed
  a visible section of pale hair beside the microphone. The smaller MODNet
  quantized weights increased CPU latency in this runtime.

## Weight provenance

- Bundled MODNet is the 25.89 MB FP32 graph pinned in the
  [model manifest](../assets/models/manifest.json). The 6.63 MB quantized
  variants came from the Apache-2.0 Xenova conversion at revision
  `7aaa8a27c987ae9452a60443a7afeb6b2a52843a`.
  [Original MODNet](https://github.com/ZHKKKe/MODNet) ·
  [Quantized weights](https://huggingface.co/Xenova/modnet/tree/7aaa8a27c987ae9452a60443a7afeb6b2a52843a/onnx)
- PP-HumanSeg is the prior 6.16 MB OpenCV Zoo pack-in, SHA-256
  `552d8a984054e59b5d773d24b9b12022b22046ceb2bbc4c9aaeaceb36a9ddf24`.
  Its model directory specifies Apache-2.0 for all files.
  [OpenCV Zoo model](https://github.com/opencv/opencv_zoo/tree/47534e27c9851bb1128ccc0102f1145e27f23f98/models/human_segmentation_pphumanseg)
- PP-MattingV2 used Yakhyokhuja Valikhujaev's 35.82 MB ONNX conversion
  of PaddleSeg's human-512 weights, SHA-256
  `7846bea9cd2ad94a81b44deeddfa1521bbcfd15bb297c181116eecb25c6f0335`.
  PaddleSeg reports SAD 40.59 and MSE 0.0038 on PPM-AIM-195 for its
  PP-MattingV2, compared with 50.07 and 0.0053 for its MODNet-MobileNetV2.
  These figures are from PaddleSeg's own evaluation.
  [Upstream results](https://github.com/PaddlePaddle/PaddleSeg/blob/release/2.10/Matting/README.md#models) ·
  [ONNX release](https://github.com/yakhyo/ppmatting/releases/tag/weights)

Both PP-Matting repositories carry Apache-2.0 licenses. A PaddleSeg
collaborator confirmed commercial use terms for PP-Matting, and a question
specifically about redistribution of the V2 weights was open during this
review. Confirm the exact weight terms before bundling that conversion.
[PP-Matting license response](https://github.com/PaddlePaddle/PaddleSeg/issues/3355) ·
[V2 redistribution question](https://github.com/PaddlePaddle/PaddleSeg/issues/3955)
