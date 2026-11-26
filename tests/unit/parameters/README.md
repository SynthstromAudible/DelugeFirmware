# Parameter ownership tests

`ParameterTests` compiles the production `ParamManager`, concrete parameter
collections, `AutoParam`, parameter vectors, containers, and sorter from
`src/deluge`. It also runs the existing lazy-vector tests using the same source
as `SmallPointerTests`. No manager or collection lifecycle algorithm is copied
into the tests.

The manager tests cover:

- One shared main allocation, correct concrete-type alignment, and non-overlapping collections.
- Destruction of populated collections, including automation nodes and patch destination tables.
- Setup replacement, MIDI setup, and preservation of separately allocated expression parameters.
- Independent cloning, shallow-copy repair with `beenCloned`, and cloning without automation.
- Ownership transfer, existing destination collections, both expression-transfer policies, and self-transfer.
- Failures allocating the main block or expression block, preserving existing ownership and cleaning up partial work.

Host adapters replace memory allocation and external playback/UI services.
The allocator tracks live blocks and frees, rejects interior/duplicate frees,
and supports allocation-failure injection. A CppUTest plugin checks for leaked
firmware allocations after **every** test, including the shared lazy-vector tests.
Unexpected playback/UI calls fail instead of silently succeeding.

The host defines `AudioEngine::bypassCulling` because the real `ResizeableArray`
writes it under allocation pressure; its firmware definition lives in the audio
engine, which is not linked into this test target. This is storage for an external
dependency, not a replacement for the container logic.

Unrelated firmware entry points are discarded by the linker, while virtual
collection methods remain linked. The mocks supply their external dependencies.
The allocator deliberately declines in-place growth and shrinking; these tests
do not validate firmware allocator size classes, voice stealing, or playback/UI
behavior. They also do not provide comprehensive AutoParam value/serialization coverage.

## Running

Use a compiler supporting C++23 and the repository's normal host test setup:

```sh
cmake -S tests -B build/tests -G Ninja
cmake --build build/tests --target ParameterTests
ctest --test-dir build/tests -R '^ParameterTests$' --output-on-failure
```

Run a specific group directly with `build/tests/unit/parameters/ParameterTests -g ParamManagerTests -v`.

For AddressSanitizer and UndefinedBehaviorSanitizer, use a separate build directory:

```sh
cmake -S tests -B build/parameter-sanitizers -G Ninja \
  -DCMAKE_C_FLAGS='-fsanitize=address,undefined -fno-omit-frame-pointer' \
  -DCMAKE_CXX_FLAGS='-fsanitize=address,undefined -fno-omit-frame-pointer' \
  -DCMAKE_EXE_LINKER_FLAGS='-fsanitize=address,undefined'
cmake --build build/parameter-sanitizers --target ParameterTests
ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 \
  ctest --test-dir build/parameter-sanitizers -R '^ParameterTests$' --output-on-failure
```

The top-level test configuration selects 32-bit builds on Linux and native builds
on macOS; Linux therefore also needs the usual multilib toolchain and sanitizer libraries.
