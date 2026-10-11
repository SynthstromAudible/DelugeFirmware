# FX/menu host regressions

Run `python3 tests/regression/fx_menu/run.py`. Requires Python 3 and GCC with C++23 support.

Eight groups cover missing/reclaimed granular buffers and allocation failure, grain-size independence from parameter history, delay sync-type copying, song HPF restoration, Digital reverb damping state and stereo width, all SHIFT/SELECT int8 inputs, and submenu navigation/drawing at list boundaries.

The tests compile production reverb headers using scalar hardware adapters. Other methods are extracted verbatim into isolated scaffolds. UBSan and libstdc++ checked iterators are enabled. The scaffolds do not model the full firmware scheduler or hardware. No test adapters are linked into firmware.

`--source-root /path/to/checkout` tests another revision. The original 2026-10-06 beta fails all eight groups; the fixed revision passes them. Hardware listening and stress tests remain necessary.
