# Host checks
Run `bash tests/regression/plaits/run.sh`, `python3 tests/regression/plaits/menu_probe.py`, and `python3 tests/regression/startup/run.py`.

DSP tests compile actual upstream engines and adapter with AddressSanitizer/UBSan, 405 model/frequency/macro combinations over six block lengths, guard samples, deterministic reset, non-silent output and A4 tuning. LeakSanitizer is disabled because the managed environment cannot enumerate /proc tasks; DSP instances themselves do not allocate on the host heap.

Menu and startup tests compile verbatim source functions with minimal host scaffolding. These are not complete firmware/hardware simulations. No physical device, CPU/polyphony benchmark, complete song round-trip, or SD I/O test has been performed. The demo PCM uses a host-generated envelope and is not a recording of Deluge's complete signal chain.
