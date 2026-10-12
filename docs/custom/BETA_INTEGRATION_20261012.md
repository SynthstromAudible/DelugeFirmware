# Beta integration — 12 October 2026

This custom OLED firmware integrates published upstream beta `be3e993c4ae78719d3940f334848aa77973ea585` and reverb PR #4990, cherry-picked from `4087ca1ba2592dcda900fedfae0d3bc9aaa98269`. It retains MDD Snake and the three Plaits engines. Unrelated commits after this beta are not included.

## Retained custom changes

1. Holdontoentropy greeting for four seconds after loading.
2. Startup F4 failsafe cleanup and return.
3. OLED submenu navigation avoids an invalid iterator before begin.
4. SHIFT encoder acceleration uses safe 32-bit arithmetic.
5. Scrolling of selected long MDD Map/Scale text and WRITE headers, including during playback.
6. Delay copies its sync type correctly.
7. Granular buffer/setup handling before wrap and shutdown.
8. Granular size calculation after updating the grain rate.
9. Applying the saved Song reverb HPF on load.
10. Digital reverb Width: mono at zero, original stereo tank output at one.
11. Skipping unused Plaits AUX rendering.
12. Plaits optimization compilation flags.
13. Plaits output amplitude scaling (8x versus the early custom implementation).
14. Finite-output protection compatible with fast-math.
15. MDD WRITE services audio every 32 candidate iterations and limits work to 1,048,576 iterations; the 65,536-note cap remains.

## Upstream fixes take precedence

The new beta's voice-culling early return is used unchanged. MIDI/gate scheduling changes are retained unchanged. PR #4990 supplies the Digital reverb second damping filter and initialized bandwidth state, so the overlapping older custom fixes are replaced by upstream code. Its saturating float-to-Q31 conversion, independent LFO handling, parameter initialization and reverb test coverage are retained. Custom Width processing occurs before the upstream saturating output conversion.

Song HPF loading is an additional fix: model initialization in #4990 does not replace applying the value saved in a Song.

## Attribution and limits

PR #4990 is upstream work by Katherine Whitlock. Deluge copyright and GPL notices remain intact. Plaits/stmlib is Emilie Gillet's MIT-licensed source; original notices and the MIT license are retained. This is an unofficial modified release.

MDD, Plaits, startup, audio-culling and all eight effects/menu regression groups passed. The upstream reverb_spec passed (including delay-line bounds checking). The ARM Release build completed with DBT v22. The ARM ELF header was verified, and objcopy output matched the distributed BIN byte for byte. No physical-device validation or ARM CPU/polyphony measurement is claimed. Existing historical audit documents describe their dated snapshots; consult firmware/BUILD_INFO.json for the distributed binary's exact source and checksum.
