# Preset navigation unit tests

`PresetNavigationTests` compiles `src/deluge/gui/ui/browser/preset_navigation.cpp`,
the same source included by the firmware build. The browser and deferred loader
call these production policies:

- `performPresetLoad` consumes the pending preview, holds the busy UI mode across
  timer cancellation and loading, clears stale interruption state while retaining
  interruptions received during the load, records the result, and restores the old mode.
- `PresetNavigationContext` validates UI/selection identities, records validated
  pad releases, chooses clip, arranger, or whole-output resumption, cancels
  session navigation when arrangement recording starts, and waits for clip
  audition to end without accepting unrelated UI or target changes.
- `presetInputAction` resolves pending selections before press events, defers only
  those presses while the card or parser is busy, and preserves both edges
  while a forced load is in progress. Otherwise releases pass.
- `PresetEditGesture` suppresses the remaining events of a failed edit gesture
  but allows edits again after 150 ms without another rejected event.
- `PresetNavigationSteps` retains accumulated detents across catalog pages and
  wraps without counting unavailable candidates.
- `presetInstrumentAvailable` implements the shared availability rules, including
  the exemption that lets navigation return to the original loaded instrument.

Fixtures use distinct addresses for identity comparisons; they never dereference
or substitute implementations of Song, Clip, UI, or Instrument. Loader callbacks
supply success/failure results and observe the busy state. No hardware mocks or
allocator adapters are needed.

```sh
cmake -S tests -B build/host-tests -G Ninja
cmake --build build/host-tests --target UnitTests PresetNavigationTests
ctest --test-dir build/host-tests -R '^(UnitTests|PresetNavigationTests)$' --output-on-failure
build/host-tests/unit/preset_navigation/PresetNavigationTests -g PresetNavigationContextTests -v
# The CI workflow also collects the standard per-group JUnit reports:
mkdir -p build/host-tests/results
(cd build/host-tests/results && ../unit/preset_navigation/PresetNavigationTests -ojunit)
```

The x86 PR workflow builds the suite, runs it through CTest, and collects its
CppUTest JUnit reports alongside the other unit suites.

These tests check the production policies, not FAT/XML parsing, catalog sorting,
model lifetimes, scheduler timing, GUI input dispatch, or audio. The loader still
checks instrument/clip membership and names against the live song before using
saved pointers.
