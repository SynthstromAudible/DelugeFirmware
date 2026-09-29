# The emulator on the Embassy host

Status: proposed 2026-09-29.
- Moves `dbt sim` off the C host BSP (`src/bsp/host`) onto the Rust BSP's threaded host build (`deluge-bsp-rust`
  with `host_app`), the one the device ships and every Embassy gate runs.
- Spans two repositories. The simulator itself (window, panel bridge, audio pacing, runtime, card) goes in
  deluge-sdk, for every project on the Deluge's hardware; this firmware keeps its glue (§4, Where it lives).

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

### Where it lives

A host-native simulator is useful to every project on the Deluge's hardware, not only this firmware. The firmware
builds on `deluge_bsp` directly, beneath deluge-sdk's capability layer, which is why deluge-sdk's own simulator
backend (`plat/sim.rs`) cannot serve it. Moving that backend down to `deluge_bsp` serves both. Everything general
therefore goes in deluge-sdk, and the firmware keeps only its glue:

| Piece | Repository |
|---|---|
| The window (`deluge-simulator`) and the panel/audio contract (`deluge-sim-link`) | deluge-sdk (exists) |
| The panel bridge in `deluge_bsp`'s host arms, behind `sim-link` | deluge-sdk; `plat/sim.rs` calls it |
| Audio pacing: render a block when the output ring has room, converting samples | deluge-sdk: `deluge_bsp::sim`, taking a render callback |
| The runtime: panel and rings set up, the program on a worker thread, the GUI on the main thread | deluge-sdk: `deluge_simulator::run_brain`, which the SDK's own host runtime now calls too |
| The card: an image file, or a directory loaded into a memory disk and written back on exit | deluge-sdk: `deluge_bsp::sd` (golden_vt_render's loader moves there) |
| MIDI queues | deluge-sdk (`deluge-sim-link`) |
| The firmware's boot on the threaded host, its libdeluge C-ABI providers mapped onto the SDK's callbacks (`audio_host.rs` feeding `deluge_app_render`, the DIN MIDI port) | DelugeFirmware |
| `dbt sim` | DelugeFirmware |

The sections below describe each piece where it lives.

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
(`deluge_bsp::sim::install(panel, audio_ends)`). The SDK keeps its panel there as well, so the SDK and programs on
the BSP share one copy. With it:

- **Outbound PIC commands** are applied as the PIC applies them, in `pic::tx`'s host arm. Every outbound helper
  funnels through `tx` with exactly one whole command, so a match on its first byte covers them all, future ones
  included, and each helper keeps a single device-identical body. `deluge_bsp::sim` keeps the PIC's pad framebuffer:
  column-pair writes update it, `DONE_SENDING_ROWS` publishes it, and the scroll commands shift it exactly as the
  app's own image is shifted (`pad_leds.cpp`). LEDs and gold-knob indicators go straight to the panel.
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

1. **Panel out.** In deluge-sdk: add `deluge_bsp`'s `sim-link` feature with the display and outbound PIC helpers
   writing the panel, and generalise `__rt::host::run` to take a BSP-level boot. `plat/sim.rs` moves onto both, and
   deluge-sdk's own simulator apps must still run. In this repository: `deluge-bsp-rust`'s `sim` feature hands the
   firmware's boot to the runtime. The app boots, and the OLED and pads show its screens.
   - [x] Built. deluge-sdk `feat/bsp-sim-link` (d8b987f and later): `deluge_bsp::sim`, the `pic::tx` and OLED host
         arms, and `deluge_simulator::run_brain`. The SDK's runtime runs through it; `oled_hello` headless renders
         byte-identically to `main`.
     - Here, `sim` runs `start_threaded_host` (the boot smoke's three executor threads, now a function both use) as
       `run_brain`'s brain.
     - Headless, the firmware boots to its own screen (`SYNTH 0 / SECTION 1`), with the LEDs and sidebar pads lit
       through the PIC path.
     - The boot smoke is unchanged: boot, audio routing and the soak pass.
     - The `sim` build needs deluge-sdk's branch through `local-sdk.toml` until the pin moves.
2. **Input.** PIC input bytes from panel events, and encoder deltas. Buttons, pads and encoders drive the UI.
   - [x] Built. deluge-sdk 3dbd3b6: pad and button events become the PIC's input bytes, and encoder turns land
         on `ENCODER_DELTAS` as two edges a detent.
     - Headless, a pad places a note, the tempo encoder shows its popup, and the session button reaches session
       view.
     - The session button exposed that no UI timer fired: the task table dropped "ui routine" (6c4ab8cfc, now 32
       slots), and firing them exposed a freed view pointer in the blank song's preset swap (fde3b3ea1).
3. **Audio.** The real-time sink, with output and input. A song plays through the speakers.
   - [x] Built. `deluge_bsp::sim::render_block` renders a block when the simulator's output has room; the SDK's
         `audio_run` waits around it, and here `deluge_audio_drive` calls it, at most two blocks a call as on the
         device.
     - Headless runs are paced by an audio clock that drains the output at 44.1 kHz of wall time, records it under
       `DELUGE_SIM_AUDIO_OUT`, and reports frames the app delivered late.
     - Headless, a clip of four notes plays back every 0.5 s at 120 BPM, with no late frames.
     - Input reaches the render as the simulator captures it, but headless feeds silence, so only the window's
       input device or `--audio-in` exercises it.
4. **MIDI.** The DIN providers on the panel's queues. A keyboard on the virtual port plays a synth.
   - [x] Built (e3d8b1fd3; deluge-sdk 17dd2db adds headless `midi <hex bytes>` steps and lists the app's MIDI
         output in each snapshot).
     - Headless, a note on channel 1 plays the default synth for as long as it is held, and PLAY sends Start and 24
       clocks a beat.
     - A note played nothing at first: MIDI follow's channel-1 default was set only by the startup settings, which
       wait for a card (6d7f63710 sets it at construction, which a card-less device needed too).
5. **Card.** In deluge-sdk: `deluge_bsp::sd` loads a directory into the memory disk and writes it back, with
   golden_vt_render's loader moved there. Here: `--card` with an image and with a directory. A song saved in the
   emulator appears in the directory.
   - [x] Built, with one change from the design: the loader and write-back stay in this repository, in
         `src/bsp/rust/src/memory_card.rs`, which golden_vt_render now includes from there. They are built on our
         own fork of `embedded-fatfs` and the block-device crates (`crates/`), which deluge-sdk does not have;
         `deluge_bsp::sd` keeps the memory disk underneath. Moving them means moving the fork.
     - `DELUGE_SIM_CARD` names the card: an image file, a directory, or unset for an empty formatted card.
     - Write-back mirrors every file that is new or changed on the card, and removes a loaded file deleted on the
       card unless the directory's copy changed meanwhile. It prints each change.
     - Headless, a song saved in the emulator appears in the directory with its note, and the other files stay as
       they were; the same save into an image file lands in the image. The removal path is not yet exercised.
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
