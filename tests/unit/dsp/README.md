# dsp specs

[CppSpec](https://github.com/toroidal-code/cppspec) specs for dsp-next (`src/dsp`), the synth library
(`src/synth`) and the effect kernels they share, run as CTest tests in three trees: the x86 host, where NEON is
SIMDe's (`unit`); the A9 under qemu, with real NEON (`unit-arm`); and the host under UndefinedBehaviorSanitizer
(`dsp-ubsan`). Every assertion that holds on both architectures is also an x86-against-NEON check of a vector kernel.

## Drivers

Each directory is one executable (`deluge_specs_driver`, `tests/support/cmake/specs.cmake`) holding its specs, and
one ctest test per spec file, named after the file. `core/` is the fixed-point core's A9 ISA spec and codegen probe
(`dsp_next_core_specs`, ARM tree), outside this table's flag sets.

| Directory | Driver | Trees | Flags |
| --- | --- | --- | --- |
| `.` | `dsp_specs` | host, arm, ubsan (`triangle_distribution_spec`: host) | the tree's |
| `fused/` | `dsp_fused_specs` | host, ubsan (x86 hosts) | `-mfma`: the fused policy an FMA target runs |
| `unsafe_math/` | `dsp_unsafe_math_specs` | host, arm, ubsan | `-O2 -funsafe-math-optimizations`: the device's float flags |
| `arm/` | `dsp_arm_specs` | arm | the A9's own assembly |

The drivers link the libraries as each tree builds them (`tests/support/cmake/libs.cmake`): `deluge_dsp_fixed`,
`deluge_dsp_float`, `deluge_synth`, `deluge_effects`, and the archives of app code they reach
(`deluge_test_dsp_closure`, `deluge_test_synth_closure`). `fused/` and `unsafe_math/` link variants of dsp-next built
under their own flags (`deluge_dsp_float_fma`, `deluge_dsp_fixed_unsafe_math`, `deluge_dsp_float_unsafe_math`), so
the library code a spec reaches is held to the same flags as the spec. On the A9, dsp-next is built as the device
builds it: the DX7's modern operators run `fixed/dx7/neon_fm_kernel.s`.

A spec in `fused/` or `unsafe_math/` that `#include`s a spec from this directory runs that spec again under its
driver's flags (`float_voice_fused_spec.cpp` is `float_voice_spec.cpp` at `-mfma`). It defines `DELUGE_SPEC_TWIN` as 1
around the `#include`, so the included spec's `DELUGE_SPEC` defines no entry point and the twin's file defines only its
own.

`synth_link_test` links the synth core's sources, listed by hand, with `-Wl,--no-undefined`: the proof that the synth
library's link closure holds nothing of the app.

## Adding a spec

1. Write `<name>_spec.cpp` in the directory whose flags it needs, with CppSpec (`// clang-format off` before the
   first `describe`), and end it with `DELUGE_SPEC(<name>_spec, <describe>...)` from `"support/spec_main.h"`, included
   after every project header (CppSpec's `context`, `_` and `$` macros break headers such as `kernel.h`). A `describe`
   cannot be named `<name>_spec` (that is the entry point); `<name>_spec_body` is the convention.
2. Nothing to list: the driver picks the file up on the next build. A spec that reaches app code outside the linked
   libraries adds that source to its driver's `SOURCES`.

Prefer exact expectations where the maths allows (the sine spec uses fraction-zero phases, where the result is
exactly `table[index] << 16`): exact references are the strongest regression guard, and matching on both backends
proves x86 ≡ ARM.

## Running

`./dbt test unit -- -R <name>_spec`, `./dbt test unit-arm -- -R <name>_spec`, `./dbt test dsp-ubsan`. The record
headers (`*_records.h`) are re-recorded by `./dbt test --update unit` (and `--update unit-arm` for
`dx7_asm_pin_records.h`); `tests/README.md` describes the update protocol.
