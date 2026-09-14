# Synthetic codec fixtures

`synthetic.dng` is a generated 120 × 160 Bayer RGGB image. Its four repeating
16-bit samples are R=25000, G=20000, G=20000, B=12000, with an sRGB-like XYZ-to-camera
matrix, neutral white balance, D65 illuminant, and white level 65535.

It contains no photograph or personal data. It is licensed with the application's
Apache-2.0 code and tests full sensor development without distributing a third-party
photograph. It is not a test of any particular manufacturer's camera support.

`synthetic.heic` is a generated 120 × 160 SDR gradient from `#b9b4ac` to
`#565a5d`, encoded as 8-bit HEVC. It has the same Apache-2.0 license and checks
that packaged HEIC decoders work without development-machine codec plugins.
