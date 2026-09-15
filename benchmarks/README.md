# Public portrait benchmark

Run `python3 scripts/benchmark.py --download` from the repository. See the root
README for options. `portraits.json` is the source/author/license/checksum record;
photos and generated reports remain outside Git. Generated masks and composites
are adaptations with the corresponding source license. Keep ATTRIBUTIONS.md with
shared output. No photos are used as model training data by this runner.

## Visual review, 2026-09-16

Fast, automatic CUDA on the local Linux/NVIDIA system, fixed 65% crop request,
55% removal context, 360 × 480, PNG, zero feather, Standard sharpening:

- Jimmy Wales: head/shoulder silhouette retained; check the soft hair boundary.
- Tim Berners-Lee: substantial jacket area near the left arm is removed. This is
  a visible segmentation failure against a dark, low-contrast background.
- Alexander Noll: glasses and clothing retained at contact-sheet scale; pale hair
  has a soft edge, particularly visible against black.
- Donna Strickland: fine flyaway hair is partly retained; gray fringe is visible
  against white and the microphone remains part of the foreground.
- Julianne Moore: main silhouette retained; inspect stray hair and shoulder edges.
- Mary Beard: pale hair has a soft/gray boundary against black; the source already
  clips parts of the subject and cannot supply missing framing context.

Most sources hit crop/headroom limits; the JSON warnings record this. The 65/55
values are requested settings, not a promise that every original supplies that
much surrounding space. There are no ground-truth masks and no accuracy scores.
High Quality was not run. Windows and macOS remain unverified.

## Timing observations

One cold session/cache run and three warm runs per photo, milliseconds. Cold does
not flush OS disk caches. Warm times include decoding, cache lookup, edits and
encoding. These are local wall-clock samples, not portable performance promises.

| Portrait | CUDA cold | CUDA warm median | CPU cold | CPU warm median |
| --- | ---: | ---: | ---: | ---: |
| Jimmy Wales | 1332 | 593 | 1050 | 476 |
| Tim Berners-Lee | 284 | 83 | 610 | 68 |
| Alexander Noll | 244 | 31 | 515 | 34 |
| Donna Strickland | 625 | 383 | 790 | 285 |
| Julianne Moore | 323 | 90 | 631 | 95 |
| Mary Beard | 250 | 31 | 542 | 42 |

All warm runs reported cache hits with zero misses and no inference span. Initial
CUDA runtime startup is included in the first image's cold run. Reports retain
stage timings and model checksums. RSS is sampled at run boundaries; peak memory
is a process-lifetime high-water mark. GPU memory is marked unavailable.
