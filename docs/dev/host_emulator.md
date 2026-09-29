# The emulator

`dbt sim` runs the firmware on your computer, in a window that looks and plays like a Deluge: the OLED, the pad
grid, the buttons, encoders and LEDs, audio through your speakers, and DIN MIDI over virtual ports.

```sh
./dbt sim                          # an empty card
./dbt sim --card ~/Deluge/card     # a card directory: loaded at boot, written back when the window closes
./dbt sim --card card.img          # a FAT image file, used in place
```

## What runs

One process. The firmware is the same C++ app and Rust BSP the device runs, built for the host: the app runs on
the Rust BSP's Embassy executors (the audio task on its own preemptive `deluge-audio` thread, as the audio interrupt
is on the device), and the BSP's host arms talk to deluge-sdk's desktop simulator instead of peripherals.

| On the device | In the emulator |
|---|---|
| The PIC (pads, buttons, LEDs, gold knobs) | `deluge_bsp::sim`'s virtual PIC: the pad framebuffer, scrolls and LEDs the simulator draws; pad and button presses come back as PIC bytes |
| The OLED | Each frame goes to the window |
| The encoders | Turns land as quadrature edges, two a detent |
| The codec | `deluge_bsp::sim::render_block`: a block is rendered whenever the simulator's audio output has room, so the audio device paces the app in real time |
| DIN MIDI | The simulator's virtual ports, "Deluge Sim In" and "Deluge Sim Out" |
| The SD card | A memory disk (`deluge_bsp::sd`), formatted FAT32, or an image file |

The window, the panel contract and the audio bridge live in deluge-sdk (`deluge-simulator`, `deluge-sim-link`,
`deluge_bsp::sim`), so any program on the Deluge's hardware can use them. This repository keeps its glue: the `sim`
feature of `src/bsp/rust` (`main.rs`'s `insert_sim_card` and `start_threaded_host`, `audio_host.rs`'s
`drive_simulator`, `host_link_stubs.rs`'s DIN MIDI) and the card loader, `src/bsp/rust/src/memory_card.rs`, which
golden_vt_render uses too. The design is `docs/superpowers/specs/2026-09-29-embassy-emulator-design.md`.

## Building

`dbt sim` builds the C++ app's host objects in `build/host-app` (clang, the tree the gates use), then
`cargo build --release --features sim` in `src/bsp/rust`. `--no-build` runs the last build.

While the deluge-sdk revision this repository pins lacks the simulator, the build needs a sibling `../deluge-sdk`
checkout on that branch and `src/bsp/rust/local-sdk.toml` pointing at it; `dbt sim` uses the override when the file
exists and leaves `Cargo.lock` as it found it.

## The card

- `--card <directory>`: the directory's files are copied onto a memory disk at boot. When the window closes, every
  file that is new or changed on the card is written back, and a file deleted on the card is removed from the
  directory, unless the directory's copy changed while the emulator ran, which is kept. Each change is printed.
- `--card <file>`: a FAT image, read and written in place.
- No `--card`: an empty formatted card, so settings and saves work, and nothing is kept.

## Options

| Option | |
|---|---|
| `--audio-out <wav>` | Record the output |
| `--audio-in <wav>` | Feed a WAV (looped) as the input instead of the input device |
| `--hardware [port]` | Mirror the panel onto a Deluge connected over USB |
| `--headless <script> --out <dir>` | Run without a window, for tests: see below |

## Headless

`--headless` replays a script against the panel and writes snapshots; the audio is paced by a clock at the codec's
rate, recorded with `--audio-out`, and frames the app delivered late are reported at exit.

```text
# <ms> <command> <args>
800   pad 0 3 down          # x y down|up
850   pad 0 3 up
1500  button 35 down        # the PIC button id: 9 * y + x (PLAY is 35, SAVE 33, session view 12)
1550  button 35 up
1600  encoder 1 +1          # BSP encoder index, detents
1700  midi 90 3c 64         # bytes on the DIN input
3000  snapshot playing      # playing.png (the OLED) and playing.state (LEDs, pads, MIDI sent since the last one)
3500  quit
```
