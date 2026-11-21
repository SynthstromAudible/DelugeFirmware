# Parameter unit tests

These tests use the same CppUTest `TEST_GROUP` / `TEST` fixtures, assertions and
`tests/unit/RunAllTests.cpp` runner as the existing unit tests.

- `../parameter_value_tests.cpp` runs in the existing `UnitTests` binary. It tests
  the production value classes, offset widths, rebinding, saturation and interpolation.
- `auto_param_tests.cpp` runs in `ParameterTests`. These cases
  test actual AutoParamData node operations, attribute serialization/deserialization,
  cloning, automation deletion/recreation, undo/redo state capture and exchange (including saturated clip adjustments) and live
  interpolation-context preservation and undoable-trim replacement preparation.
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

`parameter_control_tests.cpp` tests the production layer-selection policies, including live expression controls. Value tests cover full-width detachment and rebinding.

These cases do not simulate physical encoder input, whole UI transactions or menu reset
commands. Changing a ClipParamValue directly tests that value object's behavior,
not those callers. Parser and attribute IO tests do not exercise whole-song loading or physical SD IO.

The trim regression cases call the production replacement-vector preparation used
by `AutoParam::trimToLength()`, then exercise real vector ownership and undo/redo
state exchange. They do not instantiate the UI action logger or a clip-length UI.

Relative-value persistence tests retain positive and negative overshoot, truncated timelines, wide scalars and clones using the production serializer and reader.

Preset-save tests exercise the production scoped snapshot and scalar rebasing helper, preserving hidden overshoot and relative curves. The snapshot retains actual serialized values while automation advances; song serialization does not change the baseline. Whole UI save transactions require emulator validation.


`parameter_control_value_tests.cpp` compiles the production control-value reader
and scalar-edit operation. It checks scalar reads without AutoParam, preservation
of curve storage and interpolation state, and saturation. Compile-time assertions
verify that live setters and internal representation helpers are not public.

Access checks and explicit-editor tests use live AutoParam through its production interface.
Standalone data fixtures use the same production value and storage implementations.
