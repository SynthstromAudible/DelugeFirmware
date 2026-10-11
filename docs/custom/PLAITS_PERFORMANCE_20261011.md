# Plaits performance and level review - 11 October 2026

## Changes
- OUT-only rendering skips unused AUX synthesis in FM, Waveshaping and the compiled Virtual Analog variant 2. FM specializes the AUX decision once per block. OUT is preserved; AUX-capable renders retain the existing path.
- Static engine dispatch and Plaits-only Release -O3 / inlining. FM keeps its original 4x oversampling; no reduced sample rate or dropped voices.
- Float-to-oscillator scale corrected from Q22 to Q25. With the existing fixed-point mix, peak output becomes sourceAmplitude/2, matching the stock sine path. This raises existing Plaits presets by about 18.06 dB (8x amplitude), so reduce oscillator/clip level before auditioning previously compensated presets. It does not force all motor timbres to identical RMS.
- Restored toolchain/REQUIRED_VERSION (22) in source exports.

## Host benchmark
Same host, eight voices, 24-sample engine blocks, static and moving control cases. Median of three runs. Baseline -O2 / no automatic inlining; optimized -O3 / inlining. This measures the combined change, not only AUX removal. It is not the Deluge CPU meter or ARM cycle count.

| Engine | Controls | Before ns/sample | After ns/sample | Time reduction |
|---|---|---:|---:|---:|
| FM | Static | 105.856 | 99.256 | 6.2% |
| FM | Moving | 106.563 | 97.915 | 8.1% |
| Waveshaping | Static | 32.662 | 19.751 | 39.5% |
| Waveshaping | Moving | 33.347 | 17.688 | 47.0% |
| Virtual Analog | Static | 27.168 | 11.820 | 56.5% |
| Virtual Analog | Moving | 28.157 | 12.815 | 54.5% |

## Validation
- 144,000 changing-pitch/control primary samples matched the baseline exactly in the comparison build. This concerns DSP OUT before the intended mix-level increase.
- AUX-null vs AUX-enabled OUT equality over frequency/control corners and variable block lengths, with guard samples, ASan and UBSan.
- Existing adapter tests: parameter corners, deterministic initialization, live engine switching, zero-frequency silence and A4 tuning.
- Native MDD regression retained.
- Physical-device CPU, polyphony ceiling, downstream clipping and listening tests remain required.

No firmware performance percentage is promised from the host measurements.
