# Parameter unit tests

These tests use the same CppUTest `TEST_GROUP` / `TEST` fixtures, assertions and
`tests/unit/RunAllTests.cpp` runner as the existing unit tests.

- `../parameter_value_tests.cpp` runs in the existing `UnitTests` binary. It tests
  the production value classes, offset widths, rebinding, saturation and interpolation.
- `auto_param_tests.cpp` and `midi_param_tests.cpp` run in `ParameterTests`. They
  test the production AutoParamData node operations, attribute serialization/deserialization,
  cloning, automation deletion/recreation, undo/redo state capture and exchange (including saturated clip adjustments) and live
  interpolation-context preservation, undoable-trim replacement preparation, and MIDI
  parameter insertion/deletion, binding, and detachment before an output is destroyed.
- `param_render_values_tests.cpp` runs the production render-value cache over AutoParamData: first
  reads, reuse until a value changes, clip-value and baseline edits after the values are resolved,
  automated reads, playback updates which skip a refresh, and a clone resolving from its own layers.
  Every read goes through `ParamRenderValues`, so a stale resolved array fails.
- `parameter_io_tests.cpp` compiles the production JSON/XML deserializers and shared
  parameter-table reader. It checks full-width IDs, empty tables, unknown fields,
  and preservation of subsequent fields. Only the file byte transport is mocked.
- The lazy-vector cases from `tests/32bit_unit_tests/container/` also run in
  `ParameterTests`, using the same source in both targets. They cover curve
  ownership and preservation of the clip-binding tag.

The separate target keeps the allocator and IO adapters isolated from the existing
UnitTests mocks. Fixtures clean up automation storage after every test. Tests use
production headers and compile production source files directly; no parameter
implementation is copied. The mocks provide host allocation and file/attribute bytes.

The target compiles `auto_param_value.cpp` and `auto_param_storage.cpp`, the same
production files included by the firmware source list. Playback/model-stack wrappers
remain in `auto_param.cpp`; curve evaluation takes an explicit loop length. No
function-section or linker dead-stripping flags are needed. Shared scratch storage
and the voice-culling flag also use their production definitions. Only allocation,
file/attribute IO, diagnostic logging and fatal-error reporting have host adapters.

Run with the existing CI compiler dependencies installed:

```sh
cmake -S tests -B build/host-tests -G Ninja
cmake --build build/host-tests --target UnitTests ParameterTests
ctest --test-dir build/host-tests -R '^(UnitTests|ParameterTests)$' --output-on-failure
# Individual groups/cases use the standard CppUTest arguments:
build/host-tests/unit/parameters/ParameterTests -g AutoParamTests -v
build/host-tests/unit/UnitTests -g ParameterValueTests -v
```

The existing x86 PR workflow builds and runs both binaries through CTest.

`parameter_control_tests.cpp` tests the production layer-selection and
automation-transition policies used by the firmware callers, covering
expression/live input, recording/region exceptions, inactive MIDI automation
removal and tempo-clock refresh selection. Value tests exercise full-width detachment/rebinding
and MIDI backup destruction with the real storage classes. AutoParam tests cover
effective-tempo edits with positive and negative automation offsets.

These cases do not simulate physical encoder input, whole UI transactions or menu reset
commands. Changing a ClipParamValue directly tests that value object's behavior,
not those callers. Parser and attribute IO tests do not exercise whole-song loading or physical SD IO.

The trim regression cases call the production replacement-vector preparation used
by `AutoParam::trimToLength()`, then exercise real vector ownership and undo/redo
state exchange. They do not instantiate the UI action logger or a clip-length UI.

The save/reload regression cases exercise the production supplemental relative
serializer and reader with positive and negative overshoot, retained timeline
limits, and saturated cloning. Preset rebasing tests preserve another clip's
hidden overshoot and leave unrelated owners untouched. Tempo tests exercise the
clock-value boundary and the production undo transaction, including coalesced
edits and redo with a relative curve. Whole-song UI save routing is
validated separately in delugemu; these unit tests do not emulate those UIs.

Preset-save tests also verify that the scoped snapshot retains the actual
serialized value while automation advances and does not alter the baseline
itself. Bound-data tests round-trip relative data through the production cloning
algorithm with independently bound scalars. MIDI tests exercise real entry/vector
loading and relocation, and the control tests also check a real PatchCable.


`parameter_control_value_tests.cpp` compiles the production control-value reader
and scalar-edit operation. It verifies distinct preset/clip/live reads, scalar
reads with no AutoParam, unchanged curve storage and interpolation state after
editing, saturation, and patch-cable behavior. MIDI tests call the production
sparse-vector control reader, including absent CCs without materialization.
The tempo setup regression preserves the saved scalar and nodes while reconciling
the live value from the timer. The tests do not mock ParamCollection, the action logger, playback, or UI objects;
full dispatcher/caller behavior is exercised separately in delugemu.

C++ compile-time assertions verify that the live setter and internal scalar/curve
helpers are inaccessible to ordinary callers. These checks supplement the
production-code value tests.

AutoParam privately owns the AutoParamData implementation. Standalone data fixtures
can prepare interpolation/undo inputs without obtaining privileged access to a
live AutoParam. The production setup, ownership, playback and live-input interface
constructors are inaccessible to tests and ordinary callers. Compile-time checks
cover those constructors, private setters, mutable-base conversion and node access.
There is no test-only friend, alternate implementation, or access-check script.
