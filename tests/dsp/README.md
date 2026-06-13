# dsp-next unit specs (dual-architecture, CTest)

Behavioural [CppSpec](https://github.com/toroidal-code/cppspec) specs for the
`dsp-next` DSP library, run as **CTest** tests on **both** architectures the
firmware targets:

- **x86** with the SIMDe NEON backend (the host-sim ABI, `-m32`), via the
  `tests/` project, and
- **ARM Cortex-A9** with real NEON under `qemu`, via the `tests/qemu/` project.

The same `*_spec.cpp` files are built by both projects (this directory's
`CMakeLists.txt` is `add_subdirectory`'d from each), so every assertion doubles
as an x86-vs-NEON cross-check of the vectorised kernel — the guarantee the
golden harness gives end-to-end, but fast and at the unit level. CI runs both in
`.github/workflows/tests.yml`.

This is the per-primitive safety net for the synth untangling
([../../docs/dev/synth_phase1_l0_plan.md](../../docs/dev/synth_phase1_l0_plan.md)):
a thorough spec is written for each kernel as it migrates (see
[../../docs/dev/synth_untangling_plan.md](../../docs/dev/synth_untangling_plan.md)),
locking its behaviour against future regressions.

## Running locally

x86 (needs `libsimde-dev` + a multilib `-m32` toolchain):

```
cmake -B build-tests -S tests -G Ninja
cmake --build build-tests
ctest --test-dir build-tests -R _spec --output-on-failure   # or -R sine_spec
```

ARM via QEMU (needs `arm-linux-gnueabihf-g++`, `qemu-user-static`,
`binfmt-support`):

```
cmake -B build-tests-arm -S tests/qemu -G Ninja
cmake --build build-tests-arm
ctest --test-dir build-tests-arm --output-on-failure
```

## Adding a spec

1. Write `tests/dsp/<name>_spec.cpp` with CppSpec (`describe`/`context`/`it`/
   `expect(...).to_equal(...)`, …), ending with `CPPSPEC_MAIN(<describe_obj>);`
   so it builds as a standalone CTest executable.
2. In `tests/dsp/CMakeLists.txt`, set `<name>_spec_DEPS` to the dsp-next source
   file(s) the spec exercises (compiled into the spec executable).

Prefer assertions that are **exact** where the maths allows (the sine spec uses
fraction-zero phases, where the result is exactly `table[index] << 16`) — exact
reference values are the strongest regression guard, and matching on both
backends proves x86 ≡ ARM.
