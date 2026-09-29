# The emulator on the Embassy host

Status: proposed 2026-09-29.
- Moves `dbt sim` off the C host BSP (`src/bsp/host`) onto the Rust BSP's threaded host build (`deluge-bsp-rust`
  with `host_app`), the one the device ships and every Embassy gate runs.
- Spans two repositories: the panel bridge belongs in deluge-sdk's `deluge_bsp`, and the rest in this one.

## 1. Why

`dbt sim` builds `deluge_host`: the app on the C host BSP (`host_bsp.c`, `host_audio.c`, `host_link.c`,
`host_efatfs_passthrough.cpp`), with the app's own C++ TaskManager, single-threaded. It is the only thing left
that runs the firmware on that BSP by hand, and it has fallen behind `next`:
- It runs none of what the device runs underneath the app: no Embassy executors, no preemptive audio tier, and none
  of the storage tiers or the channels the storage work built.
- On `next`, a preset on its card directory (`DELUGE_SD_ROOT`) is not loaded by the blank boot song: the synth
  golden masters rendered silence there (render_preset spec, §1). The emulator's card loading is suspect with it.
- Its default 32-bit build does not compile on `next`, with either compiler. The layout assertions added in
  ffd2ee5ec for `DelugeAudioLoadResult` assume an 8-byte-aligned `uint64_t`, which i386 does not give.

The Rust BSP's host build already runs the real stack threaded, and deluge-sdk already has an in-process desktop
panel for its own apps. This spec joins the two.

## 2. Goals and non-goals

Goals:
- `dbt sim` runs the firmware on `deluge-bsp-rust`'s threaded host build. That means the Embassy executors, the
  preemptive `deluge-audio` thread and the storage tiers, as on the device.
- The panel is deluge-sdk's `deluge-simulator`, in-process: display, pads, LEDs, buttons, encoders, audio through
  the host's sound device, and MIDI through its virtual ports.
- The firmware's BSP runs its device code for the panel. `control.rs` and `display.rs` are unchanged, and the host
  differs only below `deluge_bsp`'s transport.
- A card that persists: a FAT image file, and a host directory.

Non-goals:
- Retiring the C host BSP. `deluge_loadcheck` (the `audio-loadcheck` gate) still builds on it; porting that is
  separate work.
- Fixing the C host's 32-bit build. Once `dbt sim` leaves it, nothing runs it.
- The socket protocol (`host_link`, `deluge-simulator --connect`). In-process removes it, and it stays in deluge-sdk
  for whatever else speaks it.
- A physical Deluge as the panel (`deluge-simulator`'s hardware mirror). It comes with `run_in_process` and is
  worth trying, but it is not a goal.

## 3. What exists

- **deluge-sdk's in-process simulator.**
  - `deluge_simulator::run_in_process(panel, gui_audio)` hands the main thread to the iced GUI.
  - It renders a `deluge_sim_link::SharedPanel` (display bytes, pad RGB, LEDs, knob indicators, MIDI queues) and
    pushes input events into it.
  - It plays and records audio over lock-free rings (`deluge_sim_link::audio`). The GUI's cpal callback paces the
    app's DSP loop.
  - It bridges MIDI through virtual ports ("Deluge Sim In/Out").
  - deluge-sdk's own apps bind to it in `crates/deluge-sdk/src/plat/sim.rs`.
- **`deluge_bsp`'s host arms are placeholders.**
  - PIC: `pic::tx` logs and drops each outbound command, and `pic::read_byte` parks forever.
  - OLED: frames go to a capture surface (`oled::captured_frame`).
  - Encoders: `encoder::irq_init` does nothing.
  - So on the host, the firmware's `pic_pump`, `pad_render` and `oled_render` run and go nowhere.
- **The threaded host build.** `deluge-bsp-rust` with `host_app` (`src/bsp/rust/src/main.rs`) boots the whole app
  on a host executor thread, and routes the audio task to a preemptive `deluge-audio` executor thread
  (`scheduler::set_audio_spawner`). `audio_host.rs` renders into a null sink. Today it serves as a boot smoke test
  and for scenarios.
- **Cards on the host.** `DELUGE_SD_IMAGE` names a FAT image file, which reads and writes go to (`sd_image.rs`).
  golden_vt_render fills a memory disk from a directory (`memory_card.rs`); nothing writes it back.

## 4. Design

### The binary

A `sim` feature of `deluge-bsp-rust`, on top of `host_app`:
- The app boots exactly as `host_app` boots it, on its executor thread plus the `deluge-audio` thread.
- The main thread creates the `SharedPanel` and the audio rings, hands the BSP its ends, and calls
  `deluge_simulator::run_in_process`.
- Closing the window stops the app (§ Card: it writes the card back first).
- `deluge-simulator` comes from the sibling deluge-sdk checkout the BSP already builds against. It is optional and
  behind the feature, so neither the device build nor the test harnesses pull in iced.

### The panel bridge, in `deluge_bsp`

A `sim-link` feature of `deluge_bsp` gives its host arms a `SharedPanel`, installed once at start
(`deluge_bsp::sim::install(panel, audio_ends)`). It takes over the arms from `deluge-sdk`'s `plat/sim.rs`, which
then calls it rather than keeping its own copy. With it:

- **Outbound PIC commands** write the panel: the host arm of each outbound helper (pad colours and columns, LED
  on/off, knob indicators, refresh and dimmer settings). This is not done in the byte-level `tx`, so no outbound
  command needs decoding.
- **Inbound:** `pic::read_byte` pops the panel's input events, encoded as the PIC bytes the device's PIC sends
  (pad and button press/release codes). The firmware's `pic_pump` and `Parser` handle them as on the device.
- **OLED:** `oled::send_frame`'s host arm writes the frame to the panel as well as the capture surface.
- **Encoders:** panel deltas advance the encoder counts and fire `ENCODER_WAKER`, as the quadrature interrupt does
  on the device.

Without the feature, the host arms stay the placeholders they are now. So the harnesses and host tests that rely on
a quiescent PIC keep it.

### Audio

`audio_host.rs` gets a real-time sink beside the null sink. Under `sim`, the `deluge-audio` thread renders a block
whenever the panel's output ring has room for one, and waits otherwise. The loop has the same shape as
`plat/sim.rs`'s `audio_run`.
- **Output:** q31 samples convert to the ring's `f32` frames.
- **Input:** the input ring feeds the app's input block (line in, and the mic through the GUI's input device).
- **Pacing:** the GUI's audio callback is the clock, so the app renders at the device's rate in wall time. The
  virtual-cadence override the gates use (`set_audio_period_override_us`) stays off.

### MIDI

Under `sim`, the host MIDI providers (`host_link_stubs.rs`'s `deluge_midi_*`) read and write the panel's MIDI
queues as the DIN port. The simulator exposes them as its virtual ports. USB MIDI stays absent, as on the host now.
The note-script source added for render_preset keeps priority when a scenario arms one.

### Card

`dbt sim --card <path>`:
- **An image file:** `DELUGE_SD_IMAGE`, read and written in place, as now.
- **A directory:** a memory disk filled from it at boot. This is golden_vt_render's `memory_card.rs`, moved into the
  BSP so both use one copy. Closing the emulator writes changes back: every file whose bytes differ, created or
  removed, is mirrored to the directory, with the FAT read by `embedded-fatfs`.

  Everything underneath (FAT, sectors, clusters, the streaming loader's sector reads) is then the device's path, and
  the user sees a folder. This is also the first half of the directory passthrough wanted for rendering songs
  locally. A live passthrough, which would serve the directory without copying it, is a later step if write-back on
  exit turns out not to be enough.
- **No card:** an empty formatted memory disk, as the gates use.

### `dbt sim`

`dbt sim` builds `deluge-bsp-rust` with `host_app,sim` against the clang x86-64 host-app tree (`build/host-app`,
through `test_builds`' product, so the tree is shared with the gates), and runs it. The `build-sim` C host path,
`--x64`, the socket and the `cargo install` of the simulator are removed. `docs/dev/host_emulator.md` is rewritten
for the new shape.

## 5. Steps

1. **Panel out.** Add `deluge_bsp`'s `sim-link` feature with the display and outbound PIC helpers writing the panel,
   and `deluge-bsp-rust`'s `sim` feature opening the window. The app boots, and the OLED and pads show its screens.
2. **Input.** PIC input bytes from panel events, and encoder deltas. Buttons, pads and encoders drive the UI.
3. **Audio.** The real-time sink, with output and input. A song plays through the speakers.
4. **MIDI.** The DIN providers on the panel's queues. A keyboard on the virtual port plays a synth.
5. **Card.** `--card` with an image and with a directory (memory disk plus write-back). A song saved in the emulator
   appears in the directory.
6. **`dbt sim`.** Switch the task and retire its C-host path, and rewrite the doc.

Each step is checked by running it. A host test covers what can be tested headlessly: panel input reaching
`deluge_control_poll_event`, and write-back reproducing a saved file.

## 6. Open questions

- **The deluge-sdk pin.** Step 1 changes deluge-sdk, so the firmware's pin moves with it. The pin moves after Kate
  pushes deluge-sdk.
- **Real-time audio and the tiers.** On the device, the audio interrupt preempts everything. Here the `deluge-audio`
  thread competes with the app thread for the host CPU. An underrun on a loaded host would show the tiers' timing,
  not a bug, but the emulator should report underruns (the counters exist) rather than hide them.
- **Directory write-back and concurrent edits.** The copy is taken at boot, so edits to the directory while the
  emulator runs are overwritten on exit for the files the emulator changed. A warning at exit that names each file
  written may be enough; a live passthrough would remove the question.
