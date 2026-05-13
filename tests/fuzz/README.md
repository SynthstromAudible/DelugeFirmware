# Fuzz harnesses

libFuzzer-based harnesses for attacker-reachable parsers in the
firmware. All targets link against the real source under
`src/deluge/`, so a crash here is a crash on the device.

## Build

libFuzzer requires clang. From the repository root:

```
CC=clang CXX=clang++ cmake -B build/fuzz -S tests/fuzz
cmake --build build/fuzz
```

## Run

Each binary accepts the usual libFuzzer flags:

```
./build/fuzz/pack_fuzz   -max_len=8192   -runs=1000000
./build/fuzz/semver_fuzz -max_len=128    -runs=1000000
./build/fuzz/memmove_fuzz -max_len=512   -runs=1000000
```

For a longer corpus-building run, point libFuzzer at a directory:

```
mkdir -p corpus/pack
./build/fuzz/pack_fuzz corpus/pack -max_len=8192
```

## Current targets

| Target | Source under test | Why it matters |
| --- | --- | --- |
| `pack_fuzz` | `src/deluge/util/pack.c` (`unpack_7bit_to_8bit`, `unpack_7to8_rle`, `pack_8bit_to_7bit`, `pack_8to7_rle`) | Every SysEx parsing path on the device runs input through these functions first. An out-of-bounds write here is reachable over USB-MIDI without any feature gating. |
| `semver_fuzz` | `src/deluge/util/semver.cpp` (`SemVer::Parser`) | Firmware version strings come from files on the SD card. A crash on a malformed string would brick song-load. |
| `memmove_fuzz` | In-harness reference `memmove` | Pre-existing harness, retained. |

## Wanted follow-ups

The four highest-value remaining targets all need mock infrastructure
before they can link, so they are not included in this PR:

1. **`MidiEngine::midiSysexReceived`** in `src/deluge/io/midi/midi_engine.cpp`.
   Highest priority. Reachable directly from USB-MIDI. Needs mocks for
   `MIDICable`, `display`, `l10n`, and the SysEx command dispatchers
   (`HIDSysex`, `Debug`, `smSysex`).

2. **`JsonDeserializer`** in `src/deluge/storage/JsonDeserializer.cpp`,
   memory-backed mode. Reachable via the JSON-over-SysEx file API.
   Needs `FileDeserializer`, `String`, and a no-op `Cluster` mock so
   the deserializer skips its SD-cluster prefetch path.

3. **`XMLDeserializer`** in `src/deluge/storage/Deserializer.cpp`.
   Reachable via every preset and song load. Same mock surface as
   `JsonDeserializer`.

4. **`AudioFile::loadFile`** in `src/deluge/storage/audio/audio_file.cpp`.
   Reachable via every sample and wavetable load. Needs an in-memory
   `AudioFileReader` and a no-op `Sample` / `WaveTable` shim. Two
   integer-overflow shapes in the WAV/AIFF chunk loop are the main
   targets here.

When adding any of these, follow the pattern in `pack_fuzz.c`:
multiplex multiple entrypoints behind the first byte of the input so a
single binary covers the whole parser surface and libFuzzer's coverage
feedback works across all of them.
