# Measuring on the device

How to answer "is this faster, and where does the time go" on the J-Trace rig, with the tools in
`tools/`. Each tool's `--help` and module docstring has the details; this is the workflow.

## The rig

A Raspberry Pi (`pi-top`) holds the J-Trace PRO and runs `JLinkRemoteServerCLExe`; the tools reach
the probe through it (`--select IP=pi-top`). `tools/rig.py restart` recovers a wedged probe and starts
the server; `tools/rig.py park` leaves the panel dark when you are done; `tools/rig.py stop` frees the
probe. A dropped connection is usually the network to the Pi, not the rig: retry.

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
