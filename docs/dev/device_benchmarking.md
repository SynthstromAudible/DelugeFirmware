# Measuring on the device

How to answer "is this faster, and where does the time go" on the J-Trace rig, with the tools in
`tools/`. Each tool's `--help` and module docstring has the details; this is the workflow.

## The rig

A Raspberry Pi holds the J-Trace PRO and runs `JLinkRemoteServerCLExe`; the tools reach the probe
through it (`--select IP=<host>`, and `--host`, which defaults to `$DELUGE_PROBE_HOST`). `tools/rig.py restart` recovers a wedged probe and starts
the server; `tools/rig.py park` leaves the panel dark when you are done; `tools/rig.py stop` frees the
probe. A dropped connection is usually the network to the Pi, not the rig: retry.

## Does it boot? `tools/device_check.py`

`tools/device_check.py [ELF]` boots an image once and reports, as JSON with exit code 0/1: the times of
"going into main loop" and the first OLED frame, boot errors, the `.sdram_init` inflate, the settings reads,
and memory. It paints the program stack before the core starts; the image's memory-report job sees the paint
and logs `MEM` lines with each heap's free bytes, their low-water mark, and the stack's high-water mark. It
parks the panel and stops the server afterwards unless `--no-park`.

## Driving the panel from a script: the rig harness

A `rig-harness` image takes level-3 UI scripts' gestures and reports what its panel shows, over RTT, so a
script runs on the real Deluge with nobody at it:

1. Build it: `tools/bench_image.py --out harness.elf --no-bench --features rig-harness`. The feature is never in
   a shipping image; its images link with a 96 KiB SRAM heap floor instead of 100 KiB.
2. Check it boots: `tools/device_check.py harness.elf --expect-harness`.
3. Compare it with delugemu: `tools/device_parity.py --emu-ref <elf> --device harness.elf --sd <the rig's card>
   scripts…`. It reads the rig's settings sector for delugemu's starting flash (read only), runs delugemu in
   parallel and the device one script at a time, and prints emu_parity's MATCH/DIFF lines. With
   `--record-only` there is no delugemu side; scripts with `midi` steps (sent with `amidi` on the probe host,
   `--midi-port`) need it.

The protocol: the device prints numbered `@` lines between its log lines on RTT channel 0 (the OLED, pad
columns, LEDs, gold rings, flashes and PIC settings as the app sent them; `peripherals/rig_harness/wire.rs`)
and reads command lines from down channel 0 (`input.rs`). `tools/rig_session.py`'s `DeviceSession` turns that
into the session `tools/emu_parity.py` drives, with its clock counted from when it attached. A gap in the
numbering, a refused command or a silent device stops the run. What it cannot show as delugemu does: pads the
PIC moves itself during a scroll animation. A run of the 18 `parity/` scripts takes about 12 minutes; a script whose run fails on the link (the probe
server not restarting, the device going silent) is retried once.

## 1. Build the images

```sh
tools/bench_image.py --out .superpowers/stream-bench-before.elf
tools/bench_image.py --out .superpowers/stream-bench-after.elf --sdk ~/path/to/sdk-worktree
```

Each builds RelWithDebInfo with `DELUGE_STREAM_BENCH=ON`. `--sdk` builds against another deluge-sdk
checkout for that build only. Afterwards the tree is as it was: `local-sdk.toml`, `Cargo.lock` and
the bench option are restored. The songs the bench plays are CMake cache values
(`--define DELUGE_STREAM_BENCH_SONG_A=SONGS/...`), paths as on the card.

The bench (`src/deluge/bench/stream_bench.cpp`) loads song A, idles, plays it, then loads song B while
it plays, and prints `STREAM-BENCH-*` lines per phase over RTT.

## 2. Run them, several times, interleaved

```sh
tools/stream_bench_matrix.py run --name readahead --rounds 3 \
    --image before=.superpowers/stream-bench-before.elf \
    --image after=.superpowers/stream-bench-after.elf
```

One run of each image per round, in turn, so drift on the rig hits both alike. Each run's capture
and tables land in `.superpowers/estate/bench/matrix-<date>-<sha>-<name>/<label>-<round>/`, and the
rig is parked at the end. One run is not evidence: differences under a few percent are inside the
run-to-run spread.

## 3. Compare

```sh
tools/stream_bench_matrix.py summarise DIR --compare before after --match 'armed|fill\.mean'
```

Per image, each metric's mean ± standard deviation [min–max]; with `--compare`, the difference and
whether the two images' ranges are **apart** or overlap. Metrics come from the phase histograms, the
underruns, every `STREAM-BENCH-<TAG> phase=<p> key=value` line, the load phases and the arm time,
and any firmware log line of the form `TAG key=value ...` (so a temporary `log::info!` or
`D_PRINTLN` with that shape is summarised with no parser change).

`tools/stream_bench.py --from-capture FILE --out DIR` re-reads a saved capture.

## 4. Find where the time goes

The bench's own lines time what they name; add a temporary `TAG key=value` line where you need a
finer split, and compare images as above.

For a profile, the PTM trace:

```sh
tools/deluge_trace/deluge_trace.py run --elf IMAGE.elf --text --profile \
    --until 'STREAM-BENCH-LOADSTEP phase=begin' --focus 'LoadSongUI::'
tools/deluge_trace/deluge_trace.py profile RUN_DIR --elf IMAGE.elf --focus REGEX
```

It traces until the RTT line matches and keeps the newest part of the trace, then reports self and
inclusive cycles per function and, under `--focus`, the time by direct callee. Self times are exact
for what the trace saw. Call stacks are rebuilt from the trace's calls and returns, so they need a
trace that keeps sync: while audio plays, its tight loops overflow the trace port, and the report
then says how much of the time ran after a gap. Trace a narrower `--range` (the code in question)
for the stacks to hold; `--function NAME` without `--profile` gives per-instruction timing of one
function instead.

Main's own A/B bench lives in a shared clone of `main` (see the memory notes); its lines use the same
format, so its captures summarise alongside ours.
