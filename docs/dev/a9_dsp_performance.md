# DSP performance on the Deluge's Cortex-A9

What the RZ/A1L's Cortex-A9 r3p0 and its NEON MPE charge for the kind of code dsp-next runs, and what follows for how
kernels are written. Every number here was measured on a Deluge with the tools in [Measuring](#measuring). The A9
references are in the ARM manuals: the Cortex-A9 TRM (DDI 0388I) and the Cortex-A9 NEON MPE TRM (DDI 0409I).

## Measuring

- **The bench** (`DELUGE_DSP_BENCH`, `tools/dsp_bench.py`): it times components with the A9 PMU at boot, one RTT line per
  case and block size. It reports cycles per sample, plus L1 refills, renamed instructions, data-cache stalls,
  issue-empty cycles and NEON instructions. See the "Measuring" section of
  [dsp_next_component_architecture.md](dsp_next_component_architecture.md).
- **The PTM trace** (`tools/deluge_trace/deluge_trace.py run --function NAME --host pi-top`): it gives cycle-accurate
  instruction trace of one function through a J-Trace PRO, decoded with OpenCSD into cycles per loop block. The PTM
  times branch to branch, so it gives a block's cycles, not how they divide between the instructions inside it.
- **Throwaway experiments** are hand-written bench cases, usually inline asm loops. They separate a loop into its
  parts (loads, arithmetic, stores) so that each cost is measured on its own.

The trace port is 4 bits at 32.5 MHz DDR, about 32 MB/s. A loop tighter than about 12 cycles per branch outruns it. The
decoder resynchronises, and the report counts the overflows.

## Measured costs

Steady state, buffers resident in L1, cycles per 16 bytes (one Q register, four q31 samples):

| Operation | Cycles | MPE TRM issue cycles |
|---|---:|---:|
| NEON load, `vld1` Q (any register count, aligned or not) | 2.9–3.0 | 1–2 |
| NEON store, `vst1` Q (any register count, aligned or not) | **6.5** | 1–2 |
| Integer load, `ldm` (8 registers) | 2.7 | — |
| Integer store, `stm` (8 registers) | 2.8 | — |
| `vqdmulh.s32` Q (no memory) | 3.9–4.6 | 4 |
| Load + `vqdmulh.s32` Q | 5.7–6.5 | — |
| Load + `vqdmulh.s32` + store, Q | 13.2–14 | — |

These numbers explain the cost of a streaming q31 loop:

- **A 32-bit NEON multiply runs at half rate.** The MPE's datapath is 64 bits wide: `VQDMULH.32` (and `VMUL.32`,
  `VMLA.32`) on a Q register occupies it for 4 issue cycles, with a result latency of 7–9 (MPE TRM Table 3.5). The
  16-bit forms take 1–2 issue cycles.
- **A store from NEON registers costs about 6.5 cycles per 16 bytes, and nothing overlaps it.** Integer stores of the
  same bytes cost 2.8, so the L1 store path is not the limit. The cost is NEON-sourced store data reaching the integer
  load/store unit. The instruction form doesn't change it: one, two or four D registers per `vst1`, with or without an
  alignment hint, all cost the same per byte. In a trace, loops without stores run ahead of the integer core for their
  first iterations while the NEON queue fills; loops with stores never do.
- **Loads and arithmetic overlap; the store follows them.** A load-multiply-store loop costs about load+multiply plus the
  store: 13–14 cycles per Q vector, about 3.4 cycles per sample.

## What follows for kernels

1. **Fuse before anything else.** Each kernel run as its own pass over a buffer pays a load and a store of every
   sample, and a NEON store is the dearest step. `Chain<K1, …, Kn>` (`core/chain.h`) steps the kernels one after
   another on each sample in one pass, keeping the intermediate values in registers, so the block is loaded and stored
   once. A gain, pan or mix stage belongs in the chain beside the kernel that produces or consumes the signal, as
   `Amplitude` multiplies and accumulates into its output in one pass. A standalone gain pass costs about 3.4 cycles
   per sample, almost all of it memory traffic. On scalar integer kernels a removed pass saves one to four cycles per
   sample ([Chain](#chain)); fusing never turns one kernel's slow loop into a fast one.
2. **Unrolling (`U`) helps little for streaming kernels.** A gain unrolled two, three or four vectors per iteration
   gains at most about 12%. The loop is bound by the multiply's issue rate and the store, not by latency.
3. **Interleaving independent streams (`Lanes<K, N>`) is for recursive kernels.** A filter or envelope whose next
   sample depends on its last is latency-bound. N independent streams interleaved hide the latency when N is at least
   the recurrence latency divided by the whole per-lane issue cost: N ≥ latency ÷ (the lane's instructions ÷ about
   1.5, counting 2 cycles per multiply). The divisor is everything a lane issues, not the critical operation's issue
   cycles alone, since the other lanes' whole steps are what fills the wait. `OnePole` LOW_PASS recurs through a sub,
   an `smmulr` and two shifted adds, about 9 cycles, and issues about 4.5 cycles per lane (six instructions, one of them
   the multiply), so N = 2 hides it and the gain flattens beyond N = 2–3 (see [Lanes](#lanes)). On the integer core
   the knee is N = 2–3; a NEON kernel needs 2–3 quadwords in flight. The bench and the trace confirm N on the device,
   for every use.
4. **Prefer 16-bit lanes where the precision allows.** Their multiplies run at full rate and a Q register holds eight
   samples, which also halves the store traffic per sample.
5. **Keep per-block glue out of the loop.** The app builds with `-fno-inline-functions`, so an accessor such as
   `std::span::size()` is a call. Once per block it costs a few cycles per block. Inside the loop it costs that per
   sample.
6. **A fused frame is one dependency chain; take a stage that only consumes the output off it.** The core overlaps
   little of one frame with the next, so a stage appended to the end of a frame's chain adds its latency to every
   frame. The compressor's level measurement, fused onto the output it had just made, ran 12% slower than the two
   passes it replaced; measuring the frame before instead, independent of the frame's own gain and saturation, took
   about 12 cycles per frame off the wet loop ([Compressor](#compressor)).
7. **Copy only what the loop steps.** The wrappers copy a kernel's State around the loop so that scalar state stays
   in registers, and the compiler copies a large State with NEON loads and stores. A kernel whose State is mostly
   touched once per block (by `prepare` and the events) names its loop's part with `kLoopState`; the compressor's
   per-block cost fell by 100–150 cycles that way. The loop's register allocation moves with it, so bench the whole
   kernel: the compressor's blend loop got 5 cycles per frame faster and its wet loop 4 slower, which nets the wet
   cases −0.4% at n = 32 but +1.2% at 64 and +2.1% at 128. A State the loop cannot hold in registers (a ring, a
   history) is stepped in place with `kStateInPlace` instead ([Delay](#delay)).

## Shared kernel costs

Cycles per sample on the device, `Mono` wrapper, one channel. The bench ran on 5c4df244f (2026-10-09), with the
shared blocks padded off 1 KB multiples:

| Kernel (case) | n=32 | n=64 | n=128 |
|---|---:|---:|---:|
| `ShiftTracker` (`shift_tracker`) | 6.86 | 6.16 | 5.83 |
| `OnePole` LOW_PASS (`one_pole_lp`) | 10.22 | 9.60 | 9.31 |
| `OnePole` ALL_PASS (`one_pole_ap`) | 12.13 | 11.08 | 10.58 |
| `AntialiasedTanh` (`antialiased_tanh`) | 43.42 | 42.69 | 42.38 |
| `Tanh<3>` (`tanh3`) | 17.19 | 16.57 | 16.29 |
| `DryWetBlend` BLEND, `Mono` mixer (`dry_wet_blend`) | 9.02 | 8.24 | 7.87 |

- A single-channel `Mono` run measures a recursive kernel's scalar recursion with its dependency chain; it says
  nothing about the gain from interleaving channels. [Lanes](#lanes) measures that.
- Neither tanh kernel makes a call per sample. `BilinearTableInterpolator::interpolateSigned<Table>` takes its table
  (a built-in `int16_t[R][W]`) at compile time, so the base, the row stride and both log2 sizes are constants: the
  stride folds into the `ldrsh` offsets and every read stays inside one row. `AntialiasedTanh` clamps the input to
  bounds its coefficients precompute, two compares in place of `lshiftAndSaturateUnknown`'s jump table. `tanh_lookup`
  reads `kTanhSmallTable` through a `constexpr` base pointer.
- What remains is one dependency chain per sample: `AntialiasedTanh` is 40 instructions in about 42 cycles (load,
  clamp, shift, index, four `ldrsh`, `mul`/`mla`, two `smmul`, shift, store), `Tanh<3>` 15 in about 16. Removing
  instructions gains little more. The next lever is interleaving independent chains: adjacent samples of
  `AntialiasedTanh` depend only on their inputs, so two samples or two channels stepped together can overlap. A plain
  loop unroll does not do it: clang lays the copies out back to back, and the A9 does not overlap them itself. Two
  channels through `Stereo` gain 8% ([Lanes](#lanes)).

## Lanes

Cycles per channel-sample on the device for `Lanes<K, N>` (`one_pole_x2` and on), each beside its kernel's `Mono` case
from the same image. The bench ran on 5c4df244f (2026-10-09), `DELUGE_DSP_BENCH_CASES=lanes`; the `kernels` list from
the same tree gives the same `Mono` figures to within 0.01.

| Case | Kernel, form | N | n=32 | n=64 | n=128 | against `Mono` at n=128 |
|---|---|---:|---:|---:|---:|---:|
| `one_pole_lp` | `OnePole` LOW_PASS, `Mono` | 1 | 10.20 | 9.59 | 9.30 | |
| `one_pole_x2` | `OnePole` LOW_PASS, op-major | 2 | 6.58 | 6.28 | 6.14 | −34% |
| `one_pole_x4` | `OnePole` LOW_PASS, op-major | 4 | 5.77 | 5.50 | 5.37 | −42% |
| `shift_tracker` | `ShiftTracker<11>` REMOVE, `Mono` | 1 | 6.85 | 6.16 | 5.83 | |
| `shift_tracker_x4` | `ShiftTracker<11>` REMOVE, op-major | 4 | 4.55 | 4.40 | 4.32 | −26% |
| `shift_tracker_x8` | `ShiftTracker<11>` REMOVE, op-major | 8 | 3.63 | 3.50 | 3.44 | −41% |
| `antialiased_tanh` | `AntialiasedTanh` drive 3, `Mono` | 1 | 43.42 | 42.70 | 42.38 | |
| `stereo_antialiased_tanh` | `AntialiasedTanh` drive 3, `Stereo` (lane by lane) | 2 | 39.66 | 39.33 | 39.17 | −8% |
| `mono_gain` | gain, `Mono` | 1 | 7.45 | 6.71 | 6.36 | |
| `stereo_gain` | gain, `Stereo` | 2 | 4.64 | 4.31 | 4.16 | −35% |

- **Every figure here is per channel-sample**, `stereo_gain`'s included: cycles ÷ (K·n·N). A table without the
  `lanes=` field gives `stereo_gain` per frame, twice the figure here.
- **"Op-major" names the form the source asks for, not a different loop.** Over interleaved frames clang compiles the
  op-major and lane-by-lane forms to the same instructions at the shipping flags (`OnePole` at N = 2 and 4,
  `ShiftTracker` at N = 4), so these are the figures of `Lanes` at each N, whichever form a kernel takes. Opting in is
  a shape and test guarantee, not a speed-up; another kernel opts in only when a probe shows its op-major loop beating
  lane by lane, or a `LanePack` specialisation lane by lane cannot reach.
- **`OnePole` at N = 2 is issue-bound.** The loop is 12 instructions per frame (two `smmulr`, `ldrd`/`strd`, no calls,
  no `muls`), and the PTM trace gives 12 cycles per iteration in 95% of them: one instruction per cycle, with the two
  multiplies' latency hidden. What remains is issue: a loop of the same 12 instructions per frame has measured 11.1
  cycles, with some dual issue. N = 4 (24 instructions, 1.14 per cycle) gains a further 13%. A hand-scheduled asm loop
  reaches 4.4 at N = 4, each multiply placed between ALU operations, where clang's N = 2 loop puts each lane's
  dependent add right behind its own multiply.
- **`OnePole` HIGH_PASS and ALL_PASS run out of registers at N = 4.** Their loop is 34 instructions per frame,
  reloading the coefficient from the stack four times and the end pointer once every frame. LOW_PASS at N = 4 keeps
  everything in registers (24 instructions). Measure HIGH_PASS and ALL_PASS before choosing N = 4 for them.
- **`ShiftTracker` from N = 4 is NEON.** clang vectorises the interleaved frames (`vld1`, `vsub`, `vsra`, `vsub`,
  `vst1`) without being asked, bit for bit. N = 8 keeps two quadwords in flight. The step was issue-bound at 7
  instructions in the scalar loop; one quadword is latency-bound, so the gain comes from the second.
- **`Stereo<AntialiasedTanh>` gains 8%, not more.** Each lane's step is about 40 instructions with six multiplies and
  five loads, and clang lays the two channels' chains out one after the other in an 83-instruction loop that spills
  to the stack, so the core overlaps only the ends of the chains (IPC about 1.06). Software-pipelining adjacent
  samples of one channel, whose steps depend only on their inputs, is the lever for this kernel, not a wider N.

## Chain

Cycles per channel-sample on the device, each pair the same arithmetic as separate passes over the block and as one
`Chain`, bit for bit the same output. The bench ran on 176d20e6b (2026-10-10), `DELUGE_DSP_BENCH_CASES=chain`
(`.superpowers/estate/bench/2026-10-10-176d20e6b.md`). The delay cases build with the flags the delay ships with
(`deluge_dsp_fixed`: ARM mode, `-finline-functions`, no LTO), the others with the app's. The delay cases render in
place, as the delay renders its work block: each block over the one before's output. Out of place is one case, beside
the in-place digital chain.

| Case | Form | n=32 | n=64 | n=128 |
|---|---|---:|---:|---:|
| `delay_feedback_legacy_analog` | stereo response (partial sums, both channels per tap), then a tanh loop, then a high-pass loop | 261.41 | 261.03 | 260.85 |
| `delay_feedback_passes_analog` | `Stereo<FeedbackShaper>`, then `Stereo<Pinned<ShiftTracker<11>, REMOVE>>` | 171.96 | 166.85 | 164.41 |
| `delay_feedback_chain_analog` | `Stereo<Chain<FeedbackShaper, Pinned<ShiftTracker<11>, REMOVE>>>` | 170.23 | 165.12 | 162.56 |
| `delay_feedback_passes_digital` | the two passes, DIGITAL | 21.97 | 16.99 | 14.49 |
| `delay_feedback_chain_digital` | the chain, DIGITAL | 18.25 | 13.38 | 10.94 |
| `delay_feedback_chain_digital_out_of_place` | the chain, DIGITAL, from the input block into the output block | 18.63 | 13.55 | 11.02 |
| `chain3_passes` | `Tanh<3>`, `Pinned<OnePole, LOW_PASS>`, `Pinned<ShiftTracker<11>, REMOVE>`: three `Mono` passes | 34.05 | 32.27 | 31.39 |
| `chain3_fused` | `Mono<Chain<…>>` of the same three | 31.16 | 30.60 | 30.29 |
| `tanh3`, `one_pole_lp`, `shift_tracker` | each stage alone, `Mono` (sum) | 34.15 | 32.32 | 31.41 |

**What a removed pass saves.** Per channel-sample, the passes less the chain, divided by the passes removed:

| Pair | Passes removed | n=32 | n=64 | n=128 | Instructions removed (n=128) |
|---|---:|---:|---:|---:|---:|
| delay, DIGITAL | 1 | 3.72 | 3.61 | 3.55 | 3.5 |
| delay, ANALOG | 1 | 1.73 | 1.73 | 1.85 | 1.5 |
| chain3 | 2 | 1.45 | 0.84 | 0.55 | 4.7 per pass |

- **A removed pass saves its load, its store and its loop overhead**, one to four cycles per sample on the integer
  core. The chain3 passes step independent samples, and the core overlaps them; the fused loop runs each sample's
  stages as one longer dependency chain, so its issue-empty cycles rise (7.86 to 10.10 per sample). The digital delay
  saves more, 3.6 cycles for 3.5 instructions, because its in-place shaper pass is the slow one: its pointer merges into
  a post-indexed store (`str r0, [r5], #8`, after a separate `str` of the right channel), and the next frame's `ldrd`
  is addressed from that store's writeback; the passes' issue-empty cycles are 4.49 per sample against the chain's
  2.68. The NEON-sourced store (6.5 cycles per 16 bytes, [Measured costs](#measured-costs)) is where fusion should pay
  most; no NEON chain is measured yet.
- **The digital feedback chain is one 15-instruction loop per frame**, 11.92 instructions per channel-sample on the
  bench with the block's overhead, with both channels' high-pass memories in registers and one store, the output
  frame's `strd`. In place its one frame pointer is stepped after that store, so the next frame's load address follows
  a pending store; it measures 10.94 against 11.02 out of place (where clang steps both pointers after the store too),
  so the order costs nothing measurable here. Its small-n rows carry the per-block copy of the chain's State in and
  out (two 416-byte `memcpy`s, since `Stereo` runs on a local copy of every lane's State), which the analog
  response's history makes large: 50 words and an index per channel.
- **The analog delay as a chain runs 38% under the stereo form, and the response's direct form is why.**
  `AnalogImpulseResponse` sums its 26 products straight from a mirrored history of the last 25 inputs, so its tap loop
  stores nothing: two `smmlar`s into an even-tap and an odd-tap accumulator, with the next pair's history samples and
  taps loaded behind them, 11 instructions per two taps (10 on the right channel, whose taps load as one `ldrd`). A
  store inside a recurrence's tap loop is itself the cost: in a form that keeps running partial sums, each next load
  waits behind its load→multiply→add→store, about 14 cycles a tap. The stereo form keeps partial sums for both
  channels per tap and stores two per tap; in place its index is stepped between the two stores, and it measures
  260.9, where out of place, with the index stepped before both, it measures 216.9
  (`.superpowers/estate/bench/2026-10-09-81d159c2a.md`). Stepping the pointers before a store helps such a loop, but
  removing the store is the lever. The direct form is bit for bit the partial sums': the same 26 rounded products,
  summed by adds that wrap modulo 2^32, so their order does not matter. `.superpowers/findings/fir-form-spike/`
  measures every form tried, the response alone and end to end.
- **Fusing the analog delay saves about 1.8 cycles per channel-sample** in place. The chain high-passes each shaped
  sample in registers where the passes load, high-pass and store it in a second loop.
- **In place, the analog chain costs about 2 cycles per channel-sample more than out of place, from register
  allocation around its tap loops.** In one image it measures 162.46 in place against 160.50 out of place, with 7.0
  fewer instructions and 8.0 more issue-empty cycles per sample. A PTM trace of both loops puts the difference in the
  code around the tap loops, not in the loops or in the order of a pointer step and a store. Per frame (both
  channels), the tap loops run 18.7 cycles an iteration in place against 18.9 out of place, 2 cycles a frame faster,
  because in place the left channel loads its history and its taps by `ldrd`; the code around them takes 133 cycles
  against 127. In place, clang merges the input and output pointers into one register and spends the register that
  frees differently: it spills the merged pointer and the right channel's input across the left tap loop, reloads
  that input only to write it into the history (a load feeding two stores), and reloads the pointer to store the
  frame and to step it. Out of place it keeps the right input in a register and spills the two pointers. The net is
  3.65 cycles a frame. The trace, disassemblies and bench are in
  `.superpowers/estate/ledgers/dsp-next-chain-minors-evidence/`.
- **The fused analog loop stores the response's history and index and the high-pass memory per channel per frame,
  and spills four values.** Per frame: the input written twice into each channel's history and the advanced index
  (`Stereo` runs on a stack copy of the State, so these are stores to it), each channel's high-pass memory, the output
  frame's `strd`, and four spills: the frame pointer and the next one, the right input, and the left shaped sample,
  which the left high-pass output needs after the right channel's tap loop. Nothing else is stored between the two
  stages; the spills are register pressure from the tap loop's two accumulators, four pipelined loads and two
  pointers. The digital loop keeps both memories in registers. Likewise `Stereo<Chain<OnePole, Pinned<ShiftTracker<11>,
  REMOVE>>>` in HIGH_PASS and ALL_PASS stores the two channels' outputs separately (`str [r10, #4]`, then
  `str [r10], #8`) where LOW_PASS and a bare `Stereo<OnePole>` store one `strd`: one store more per frame, from the
  chain's register pressure (four state registers and a coefficient across both channels).
- **The next lever is the stereo direct form.** One tap loop stepping both channels, each tap loaded once for both,
  measures 144.41 cycles per channel-sample end to end in C, in place as the delay renders, 11% under the in-place
  chain's 162.46 in the same image; out of place it measures 139.67. In place, clang loads each history frame with
  two `ldr`s rather than one `ldrd`, 9 instructions per tap-frame against 8. A chain cannot express it yet: it needs a chain that steps a stereo frame, with a `Packed`/`PerChannel` adaptor for its per-channel
  parts. It is deferred to the mod-FX rewrite (`.superpowers/findings/fir-form-spike/`).
- **The in-place loop's remedy class.** Where a bench shows the in-place stall matters, the remedy is a one-frame
  software pipeline in the `Lanes`/`Mono` loop: load frame i + 1 before storing frame i, so the next load's address
  never follows the store. It is not done: the digital delay shows no cost from the order, and the analog delay's
  in-place cost is register allocation around its tap loops (above), which the pipeline would not remove.
- **Bench a component with the flags it ships with, and in place if it ships in place.** Under the app's flags
  (Thumb, `-fno-inline-functions`) a `std::array` member function is an out-of-line call per use: a response reading
  its taps and history through `std::array` makes calls in every frame, and all three analog forms measure about 600
  cycles per channel-sample that way (`.superpowers/estate/bench/2026-10-09-a202884d7-appflags.md`).
  `AnalogImpulseResponse` reads built-in arrays through plain pointers, so its step makes no call under either flag
  set. A kernel's `step` is `[[gnu::always_inline]]`, but what it
  calls inherits the translation unit's flags.

## Delay

The whole delay block, `DelayProcessor::process`, as the fused `Mono<Delay>` frame loop (read the tape, shape and
high-pass the feedback, mix, write the tapes, per frame) against the delay as separate passes over the block (read,
feedback chain, mix, writes). Cycles per channel-sample, in place over real tapes in SDRAM, with the delay library's
flags (`DELUGE_DSP_BENCH_CASES=delay`). The fused figures are the mean of two runs, all measured on 2026-10-10. The
two images differ in the delay code and in which bench lists they link: the baseline image linked all four case lists
and the fused image links only the delay list, so everything is placed differently. The unchanged
`delay_feedback_chain_*` controls bound that placement effect: they move by at most 0.3% between the images, and the
two fused runs differ by at most 1.7%, so a difference inside ±2% is noise (≈).

| configuration | n=32 before → after (Δ) | n=64 before → after (Δ) | n=128 before → after (Δ) |
|---|---:|---:|---:|
| `native_straight_digital` | 41.88 → 31.43 (-25.0%) | 35.99 → 32.61 (-9.4%) | 35.94 → 31.59 (-12.1%) |
| `native_straight_analog` | 189.02 → 190.88 (+1.0% ≈) | 185.43 → 190.60 (+2.8%) | 182.87 → 189.37 (+3.6%) |
| `native_cross_digital` | 36.25 → 31.70 (-12.6%) | 33.41 → 32.77 (-1.9% ≈) | 31.46 → 31.81 (+1.1% ≈) |
| `native_cross_analog` | 187.99 → 187.87 (-0.1% ≈) | 184.83 → 189.90 (+2.7%) | 182.27 → 188.72 (+3.5%) |
| `res_lo_straight_digital` | 82.22 → 68.46 (-16.7%) | 78.81 → 68.41 (-13.2%) | 74.48 → 66.81 (-10.3%) |
| `res_lo_straight_analog` | 231.78 → 225.75 (-2.6%) | 230.08 → 226.87 (-1.4% ≈) | 226.63 → 224.78 (-0.8% ≈) |
| `res_lo_cross_digital` | 82.13 → 71.92 (-12.4%) | 78.14 → 71.00 (-9.1%) | 75.55 → 71.06 (-5.9%) |
| `res_lo_cross_analog` | 232.08 → 223.93 (-3.5%) | 230.00 → 222.87 (-3.1%) | 227.38 → 223.18 (-1.8% ≈) |
| `res_hi_straight_digital` | 127.62 → 108.06 (-15.3%) | 121.82 → 107.19 (-12.0%) | 119.32 → 107.61 (-9.8%) |
| `res_hi_straight_analog` | 282.20 → 268.34 (-4.9%) | 275.69 → 268.32 (-2.7%) | 269.72 → 263.45 (-2.3%) |
| `res_hi_cross_digital` | 130.67 → 117.79 (-9.9%) | 124.60 → 116.59 (-6.4%) | 118.80 → 113.70 (-4.3%) |
| `res_hi_cross_analog` | 282.09 → 266.00 (-5.7%) | 276.27 → 265.52 (-3.9%) | 270.31 → 260.82 (-3.5%) |
| `growing` | 142.96 → 125.46 (-12.2%) | 139.31 → 120.75 (-13.3%) | 142.45 → 118.28 (-17.0%) |

- **Removing the tape passes saves 4–17% wherever the feedback is cheap**: digital, resampled and growing. Each pass
  removed is a load, a store and a loop over the block; the fused frame reads the tape and writes it back once. At
  n = 32 the passes' per-block overhead weighs more, and native digital drops by a quarter.
- **Native analog is 3–4% slower at n = 64 and 128.** The analog frame is dominated by its two 26-tap response loops,
  whose two accumulators, four pipelined loads and two pointers take most of the registers. The tape pointers, the
  block pointer and index, the read and grow flags and the drive then live on the stack and are reloaded around the
  tap loops every frame. A PTM trace of the native analog loop puts it at ≈ 378 cycles a frame (189 per channel-sample,
  the bench figure): ≈ 185 in the tap loops (9.0–9.3 cycles a two-tap iteration, faster than the chain alone) and
  ≈ 193 around them, where the passes spent ≈ 119 around the chain's tap loops plus ≈ 40 in the tape passes
  (a PTM trace of `DelayProcessor::process`). Issue-empty cycles rise from 35.4 to 38.7
  per channel-sample at n = 128. The lever is the stereo direct form (one tap loop for both channels, see
  [Chain](#chain)), which frees the registers; it is deferred to the mod-FX rewrite.
- **Echoes shorter than a block are correct in every form.** The fused order is the delay line's order, so a delay of
  fewer samples than the block keeps all its echoes, where the separate passes drop those that come round within the
  block (pinned by `delay_frame_order_spec` and `delay_echo_spec`). The two orders agree bit for bit wherever no block
  laps the tape.

**`kStateInPlace`.** The same image with `Delay::kStateInPlace` false (one run, a throwaway build): the 424-byte State
is copied to the stack and back every block, and the high-pass memories stay in registers through the loop. In place
against copied, cycles per channel-sample:

| configuration | n=32 in place / copied | n=64 | n=128 |
|---|---:|---:|---:|
| `native_straight_digital` | 31.43 / 40.42 (+28.6%) | 32.61 / 36.83 (+13.0%) | 31.59 / 33.44 (+5.9%) |
| `native_straight_analog` | 190.88 / 202.19 (+5.9%) | 190.60 / 197.81 (+3.8%) | 189.37 / 194.44 (+2.7%) |
| `native_cross_digital` | 31.70 / 41.38 (+30.5%) | 32.77 / 37.49 (+14.4%) | 31.81 / 34.06 (+7.1%) |
| `native_cross_analog` | 187.87 / 201.45 (+7.2%) | 189.90 / 198.53 (+4.5%) | 188.72 / 195.41 (+3.5%) |
| `res_lo_straight_digital` | 68.46 / 81.08 (+18.4%) | 68.41 / 76.38 (+11.6%) | 66.81 / 72.42 (+8.4%) |
| `res_lo_straight_analog` | 225.75 / 236.86 (+4.9%) | 226.87 / 233.08 (+2.7%) | 224.78 / 228.75 (+1.8%) |
| `res_lo_cross_digital` | 71.92 / 82.52 (+14.7%) | 71.00 / 76.84 (+8.2%) | 71.06 / 74.30 (+4.6%) |
| `res_lo_cross_analog` | 223.93 / 232.08 (+3.6%) | 222.87 / 229.03 (+2.8%) | 223.18 / 227.13 (+1.8%) |
| `res_hi_straight_digital` | 108.06 / 116.69 (+8.0%) | 107.19 / 111.09 (+3.6%) | 107.61 / 109.31 (+1.6%) |
| `res_hi_straight_analog` | 268.34 / 275.53 (+2.7%) | 268.32 / 269.09 (+0.3%) | 263.45 / 263.77 (+0.1%) |
| `res_hi_cross_digital` | 117.79 / 125.66 (+6.7%) | 116.59 / 118.86 (+1.9%) | 113.70 / 112.81 (-0.8%) |
| `res_hi_cross_analog` | 266.00 / 273.04 (+2.6%) | 265.52 / 268.16 (+1.0%) | 260.82 / 262.27 (+0.6%) |
| `growing` | 125.46 / 135.73 (+8.2%) | 120.75 / 126.34 (+4.6%) | 118.28 / 120.76 (+2.1%) |

The A/B is one run, and about 8 of its 39 cells are inside ±2%, so those differences are not distinguishable from
noise. The copy costs more than the in-place loop's per-frame high-pass stores in the rest, and no cell shows it
cheaper outside noise: 6–7% of the native digital loop at n = 128 and 29–31% at n = 32, 3–4% of native analog at
n = 128. A State of hundreds of bytes is stepped in place.

## Filters

Each filter as a voice runs it, and the filter set, as kernels (`Stereo<LowPassLadder<CongNoise>>`,
`Stereo<HighPassLadder>`, `Stereo<Svf>`, mono blocks through `renderLane<0>(…)`) against the hand-written classes
(before). Cycles per channel-sample, `FilterSet::configure` plus the render per block, in place, with the filter
library's flags (`DELUGE_DSP_BENCH_CASES=filters`), all measured on 2026-10-10; the kernel figures are the mean of two
runs. The unchanged `delay_feedback_chain_*` controls move by at most 1.9% between the two images (0.0% for the analog
chain, 0.08–0.34 cps on the digital one), and the two runs agree within 0.09%, so a difference inside ±1.9% is noise
(≈).

| case | n=32 before → after (Δ) | n=64 before → after (Δ) | n=128 before → after (Δ) |
|---|---:|---:|---:|
| `lp12_mono` | 102.67 → 96.52 (-6.0%) | 94.79 → 87.25 (-8.0%) | 90.89 → 82.61 (-9.1%) |
| `lp12_stereo` | 98.52 → 88.89 (-9.8%) | 94.50 → 84.37 (-10.7%) | 92.50 → 81.84 (-11.5%) |
| `lp24_mono` | 106.23 → 100.08 (-5.8%) | 102.12 → 94.52 (-7.4%) | 100.05 → 91.77 (-8.3%) |
| `lp24_stereo` | 101.93 → 94.23 (-7.6%) | 99.72 → 91.37 (-8.4%) | 98.61 → 89.94 (-8.8%) |
| `lpdrive_mono` | 139.27 → 133.63 (-4.0%) | 134.61 → 128.28 (-4.7%) | 132.32 → 125.65 (-5.0%) |
| `lpdrive_stereo` | 132.26 → 129.93 (-1.8% ≈) | 129.89 → 127.00 (-2.2%) | 128.70 → 125.50 (-2.5%) |
| `lpdrive_os_mono` | 248.47 → 225.95 (-9.1%) | 243.22 → 219.94 (-9.6%) | 240.61 → 216.98 (-9.8%) |
| `lpdrive_os_stereo` | 247.16 → 229.34 (-7.2%) | 244.58 → 226.05 (-7.6%) | 243.29 → 224.37 (-7.8%) |
| `hp_mono` | 71.10 → 55.96 (-21.3%) | 66.53 → 51.96 (-21.9%) | 64.27 → 50.00 (-22.2%) |
| `hp_stereo` | 64.66 → 54.41 (-15.8%) | 62.33 → 52.22 (-16.2%) | 61.16 → 51.11 (-16.4%) |
| `svf_band_mono` | 86.06 → 83.76 (-2.7%) | 83.01 → 79.86 (-3.8%) | 81.49 → 77.92 (-4.4%) |
| `svf_band_stereo` | 81.24 → 81.99 (+0.9% ≈) | 79.36 → 79.99 (+0.8% ≈) | 78.43 → 78.99 (+0.7% ≈) |
| `svf_notch_mono` | 81.86 → 80.73 (-1.4% ≈) | 78.92 → 76.85 (-2.6%) | 77.46 → 74.92 (-3.3%) |
| `svf_notch_stereo` | 78.16 → 78.09 (-0.1% ≈) | 76.06 → 76.04 (0.0% ≈) | 75.04 → 75.01 (0.0% ≈) |
| `filterset_hl_stereo` | 165.44 → 147.27 (-11.0%) | 161.48 → 142.86 (-11.5%) | 159.54 → 140.68 (-11.8%) |
| `filterset_lh_stereo` | 165.41 → 146.91 (-11.2%) | 161.46 → 142.73 (-11.6%) | 159.48 → 140.61 (-11.8%) |
| `filterset_par_stereo` | 171.74 → 153.73 (-10.5%) | 167.19 → 148.22 (-11.3%) | 164.97 → 145.85 (-11.6%) |

- **Nothing is slower outside the noise.** The 12 dB and 24 dB ladders gain 5.8–11.5%, the oversampled drive ladder
  7.2–9.8% and the drive ladder without oversampling 1.8–5.0%; the high-pass ladder gains 15.8–22.2%, the SVF up to
  4.4%, and the set on every route 10.5–11.8%.
- **A mode chosen per block is the largest gain.** In the hand-written class the high-pass ladder's saturation is a
  per-sample test of a value that never changes within a block; as a mode (BYPASS, TANH, ANTIALIASED), the
  unsaturated loop runs 39.2 instructions per sample against 55.9, mono.
- **Spills can cost nothing.** The stereo drive ladder's loop is 7–10 instructions per frame longer than the old one
  (the two channels' states spill), yet it runs 2.2–2.5% faster at n ≥ 64: the extra loads and stores issue into
  slots the old loop left empty (issue-empty cycles per sample at n = 128: 44.05 before, 35.04 after). Count cycles,
  not instructions.
- **One lane of a `Stereo` costs a mono block nothing.** Every mono case is as fast as or faster than the dedicated
  mono loops of the hand-written classes; `renderLane<0>(…)` compiles to the same kind of loop on lane 0's State.
- **The stereo SVF is unchanged within the noise**, band sitting at +0.7–0.9% in both runs with 3.7 fewer
  instructions per sample and the same empty issue cycles; unexplained, inside the bound, so not traced.

**The fusion question.** Whether to fuse the filter set into one loop (low-pass, high-pass and fades per sample) rests
on what the passes around the filters cost, since fusion removes those and leaves each filter's own step as it is.

| n | series route, set − its filters alone | parallel route, set − its filters alone | the fade pass while active |
| ---: | ---: | ---: | ---: |
| 32 | −1.4 to −1.7 | +5.1 | 35.2 |
| 64 | −0.7 to −0.9 | +4.6 | 34.4 |
| 128 | −0.4 | +4.8 | 34.0 |

Cycles per channel-sample, stereo: the 24 dB ladder and the high-pass ladder, each alone and as a set; the fade is
`filter_lp24_fading_stereo` (the 24 dB ladder switched off and on each block, so its fade runs over every block) less
`filter_lp24_stereo`.

- **A series route has no pass between its slots to remove.** The set costs less than its filters alone, by one block's
  glue that it pays once instead of twice (the difference halves as n doubles). Fused, it could save the second
  filter's load and store of the block: the one to four cycles per sample a removed integer pass saves
  ([Chain](#chain)), against about 140 for the set.
- **The parallel route's copy and wrapping sum cost 4.6–5.1** per channel-sample. Fusing it would remove them.
- **The fade costs about 33.5 cycles per channel-sample while active**, plus about 107 cycles per block (fitted as
  a + C/2n). Its loop is 16 instructions a sample with no calls: the dry level steps in double (`vcvt.f64.f32`,
  `vmul.f64`, `vcvt.f32.f64`) and the wet level is formed from it in float and converted to Q31, every sample's level
  depending on the last, one serial VFP chain at about two cycles an instruction. Fusing would remove the dry copy and
  the blend's load and store, not the chain, which bit-exactness fixes. The fade runs for 688 channel-sample steps
  after a slot switches on: about 344 stereo frames or 688 mono samples.
- **So fusion buys little.** On the blocks every voice renders, at most a few cycles per sample on the series routes
  and 5 on the parallel route; on a fading block, the copy and a pass, out of the 33.5 the level chain costs. The
  filters' own loops (50–225 cycles per channel-sample) are where the cost is.

## Compressor

`Mono<Compressor>` as `RmsFeedbackCompressor` renders it (after) against the hand-written class it replaced (before):
a saturation loop, a copy of the block for the blend, a blend loop and a level loop. Cycles per channel-sample (cycles
per frame ÷ 2), in place over a loud stereo block past the threshold, fully wet (`wet`) or blended at half (`blend`),
through `render` (the master's channel gains) or `renderVolumeNeutral`, with the library's flags
(`DELUGE_DSP_BENCH_CASES=compressor`), measured on 2026-10-11 with `set_levels` inlined into the owner's render and the
kernel declaring `kLoopState`. The after figures are the mean of two runs, which agree within 0.12 cycles per
channel-sample (0.15%) on every compressor case and 0.21 (0.13%) on the controls. The unchanged
`delay_feedback_chain_*` controls move by up to 1.6% between the two images (the digital one at n = 32), so a
difference inside ±1.7% is noise (≈).

| case | n=32 before → after (Δ) | n=64 before → after (Δ) | n=128 before → after (Δ) | inst/sample @128 |
|---|---:|---:|---:|---:|
| `wet_render` | 75.63 → 78.27 (+3.5%) | 68.74 → 71.18 (+3.5%) | 65.42 → 67.87 (+3.7%) | 57.8 → 56.6 |
| `wet_neutral` | 75.36 → 78.45 (+4.1%) | 68.53 → 71.31 (+4.1%) | 65.26 → 67.90 (+4.0%) | 57.8 → 56.6 |
| `blend_render` | 84.14 → 80.73 (-4.1%) | 77.20 → 73.81 (-4.4%) | 73.58 → 70.44 (-4.3%) | 70.0 → 63.1 |
| `blend_neutral` | 83.99 → 81.13 (-3.4%) | 76.88 → 73.84 (-4.0%) | 73.44 → 70.46 (-4.1%) | 70.0 → 63.1 |
| control `delay_feedback_chain_analog` | 171.97 → 172.22 | 165.87 → 166.00 | 163.00 → 163.05 | 183.8 → 183.8 |
| control `delay_feedback_chain_digital` | 19.93 → 20.25 | 14.21 → 14.36 | 11.35 → 11.43 | 12.8 → 12.8 |

Fitted as a + C/2n from n = 32 and 128 (n = 64 lands within 0.16):

| | loop, cycles per frame | per block, cycles |
|---|---:|---:|
| wet, before (two loops) | 124.0 | 871 |
| wet, after | 128.8 | 887 |
| blend, before (three loops and a copy) | 140.1 | 901 |
| blend, after | 134.0 | 878 |

- **The blend is 3–4% faster, the wet compressor 3.5–4% slower.** The fused blend loop drops the scratch copy and the
  separate blend pass and runs about 6 cycles per frame faster. The wet loop runs about 5 cycles per frame slower than
  the two loops it replaced. Per block, both now cost about what the hand-written class did.
- **The per-block cost is the State's copy, so only the loop's part is copied.** Copied whole around the loop, the
  compressor's State goes through NEON loads and stores, about 90 cycles per block in a PTM trace; stepped in place,
  the loop stores eight State words every frame instead. The kernel declares `kLoopState`, so the wrapper copies only
  `Compressor::Loop` (gains, saturators, detectors, `pending`, the 64-bit sum) with integer loads and stores: 887
  cycles per block against 1033 for the whole copy (wet), 878 against 980 (blend). `finish_level` measuring the
  block's last frame costs about 15 cycles.
- **The loops' register allocation moves with the code around them.** With the partial copy the blend loop runs about 5
  cycles per frame faster than with the whole copy, and the wet loop about 4 slower: its arithmetic is unchanged, but it
  carries 4 more `mov`s and stores its output frame as two `str` where it had one `strd` (113 instructions against 109,
  counted the same way). Inlining `set_levels` against calling it shifts them too: the wet loop gains an instruction but
  spills 4 fewer words per frame, and the blend loop gains 4 instructions. `seed` runs only after a reset, and
  `finish_level` calls `logf` and `sqrtf`; the render tail-calls it.
- **Measured on the output it had just made, the level measurement cost 12%.** That form (before the measurement ran
  one frame behind) ran the wet cases 11.7–13.5% and the blend cases 1.3–3.7% slower than before, with 4 fewer
  instructions per channel-sample but 8.6 more issue-empty cycles per channel-sample on wet. Each frame was one
  dependency chain: the gain multiply, the table lookup, the interpolation, then the detector, the square and the
  64-bit add. Measuring the frame before takes the measurement off that chain, and the wet loop fell from about 138 to
  126 cycles per frame. The 64-bit sum's conversion to float, through two words and an exact double rather than the
  library's 64-bit integer conversion, saves about 65 cycles more per block.
- **The loops call nothing.** With the shipping flags, neither loop has a call, a `muls` or an out-of-line accessor;
  each stores only its output frame (two `str`) and its spills: 17 stack loads and stores per frame in wet, 27 in
  blend. The level's 64-bit sum is among the spills.

## Mod FX

`effects::ModFx` (`Mono<ModFx<CongNoise>>`) as the app renders it (after) against `ModFXProcessor`, which rendered each
frame through out-of-line calls (before): `processModFXBuffer<type>` calling `processOneModFXSample<type, stereo>`,
`processModLFOs` and `LFO::render`, a `std::variant` visit with a call per wave. Cycles per channel-sample, a sounding
block with the knobs at rate `0x00a00000`, depth `0x40000000`, offset and feedback `0x20000000`
(`DELUGE_DSP_BENCH_CASES=mod_fx`), on 2026-10-11. The figures are the minimum of the runs of each image: two before,
four after. Before, some cases land on one of two levels 10–20% apart run to run (chorus_mono, chorus_stereo_mono, the
warblers and the phaser). With two runs the minimum is the lowest figure seen, not necessarily the lower level:
`chorus_stereo_mono` at n = 128 sat on the higher level in both runs (91.94), and the warblers' baselines may too. The
after runs agree within 0.9% on every mod FX case at every size, and the `delay_feedback_chain_*` controls, which the
change does not touch, within 0.1% of the baseline.

| case | n=32 before → after (Δ) | n=64 before → after (Δ) | n=128 before → after (Δ) | after, run to run |
|---|---:|---:|---:|---:|
| `chorus_stereo` | 82.70 → 40.85 (-50.6%) | 81.27 → 37.65 (-53.7%) | 80.52 → 36.08 (-55.2%) | 0.6% |
| `chorus_stereo_stereo` | 82.68 → 40.84 (-50.6%) | 81.25 → 37.66 (-53.6%) | 80.51 → 36.08 (-55.2%) | 0.6% |
| `chorus_mono` | 78.64 → 30.87 (-60.7%) | 76.51 → 27.67 (-63.8%) | 75.79 → 26.08 (-65.6%) | 0.9% |
| `chorus_stereo_mono` | 78.73 → 30.88 (-60.8%) | 76.48 → 27.68 (-63.8%) | 91.94 → 26.08 (-71.6%) | 0.7% |
| `dimension_stereo` | 86.50 → 37.77 (-56.3%) | 84.21 → 34.56 (-59.0%) | 83.24 → 33.07 (-60.3%) | 0.5% |
| `dimension_mono` | 86.56 → 37.92 (-56.2%) | 83.68 → 34.56 (-58.7%) | 83.14 → 33.08 (-60.2%) | 0.3% |
| `flanger_stereo` | 90.21 → 39.03 (-56.7%) | 88.48 → 35.83 (-59.5%) | 87.39 → 34.22 (-60.8%) | 0.7% |
| `flanger_mono` | 80.66 → 35.26 (-56.3%) | 78.65 → 32.06 (-59.2%) | 77.35 → 30.47 (-60.6%) | 0.4% |
| `warble_stereo` | 181.64 → 51.84 (-71.5%) | 164.44 → 48.53 (-70.5%) | 180.81 → 46.87 (-74.1%) | 0.4% |
| `warble_mono` | 150.01 → 52.47 (-65.0%) | 165.01 → 49.14 (-70.2%) | 162.73 → 47.48 (-70.8%) | 0.5% |
| `phaser` | 119.85 → 107.81 (-10.0%) | 117.78 → 105.40 (-10.5%) | 117.27 → 104.20 (-11.1%) | 0.1% |
| `control delay_feedback_chain_analog` | 171.48 → 171.46 (-0.0%) | 165.73 → 165.72 (-0.0%) | 162.86 → 162.86 (+0.0%) | 0.0% |
| `control delay_feedback_chain_digital` | 19.93 → 19.92 (-0.1%) | 14.21 → 14.20 (-0.1%) | 11.35 → 11.35 (+0.0%) | 0.1% |

Fitted as a + C/2n from n = 32 and 128, the stereo chorus costs about 69 cycles per frame and 405 cycles per block.

- **The saving is the calls.** Before, `processModFXBuffer`'s loop alone was 69 instructions with 7 calls, ahead of the
  sample's 38–55 instructions and the LFO's visit. In `effects::ModFx::render` a ring mode's loop is 56–72
  instructions, the warblers' 114 (a second warbler and the noise refresh), and the phaser's 65 around an
  18-instruction stage loop. The table's chorus, dimension and flanger cases run 50.6–60.8% faster, except chorus_mono
  and chorus_stereo_mono at 60.7–71.6% (the top of that range is against a baseline on its higher level); the warblers
  run 65.0–74.1% faster.
- **The phaser gains least (10–11%).** Its six allpass stages are a dependent chain per frame that removing calls does
  not shorten.
- **The loops' shape.** At the shipping flags minus `-flto=thin` (Thumb-2), no loop has a `muls` or an out-of-line
  accessor; the sine LFO's table interpolation is `mul.w` and `mla`. Stores go to the frame, the ring slot, the phaser's
  memories, the warblers' noise state and spills. No loop has a call, `Mode::WARBLE_MONO` included: its second
  warbler's value goes to an unused local, not to `std::ignore`, whose assignment is a call at `-fno-inline-functions`.
- **The block's fixed cost is about 405 cycles, over half of it the State copy.** For the stereo chorus the fit from
  n = 32 and 128 gives 407 cycles per block and 69 per frame. About 590 instructions run outside the loop: `Mono` copies
  the 100-byte State in and out with `__aeabi_memcpy4`, which `compiler_builtins` implements as six instructions per
  word (about 185 each way, not NEON), and about 215 instructions of out-of-line glue remain (`is_ring_type`,
  `mod_fx_mode`, the settings compare, `coefficients`, `holdsRing`, the span accessors, `prepare`, the mode dispatch).
  With the State stepped in place the fixed cost is 194, so the copies are about 213 cycles and the glue about 194.
- **The traits do not pay here.** `kStateInPlace` removes the copies, but the loops then store `write`, `phase` and
  the warbler words every frame and the output may alias them: chorus_mono and flanger_stereo gain 9 and 16 cycles per
  frame and are 14% and 20% slower at n = 128 (chorus_stereo_mono, chorus_mono's twin, 14%), against 2 to 15% faster
  elsewhere. `kLoopState` leaves out only `wave` (a 96-byte copy still through `memcpy4`) and moves the fixed cost up
  (432), as does a 16-byte-aligned State copied with `vld1`/`vst1` (430). A loop's cost per frame moves by up to 14%
  with any change to its inputs, so none of the three is kept. The lever left, unmeasured, is a kernel that holds in
  registers only the few words its loop updates.
- **Code size.** `effects::ModFx::render` is 2,720 bytes in the device image, one function for all eight loops. Against
  the old code, measured as the summed extents of the functions in the two non-LTO objects at the shipping flags minus
  `-flto=thin`: `ModFXProcessor::processModFX` (391 B) and its six `processModFXBuffer<type>` instantiations (115–155 B
  each) are 1,281 B, but they call the per-sample, setup and LFO functions, so the whole of `ModFXProcessor` in that
  object is 3,298 B. The new `render` is 2,703 B there, and 2,930 B with `coefficients` (227 B), which does the old
  setup's work. The old LFO visit machinery (`LFO::render`'s `std::variant` visit) is outside both figures.

## Granular

`effects::Granular` (`Mono<Granular<CongNoise>>`, one frame kernel) as the app renders it (after) against
`GranularProcessor`, which rendered each frame through `processOneGrainSample` (after `processGrainFX` called it, with
`GrainBuffer::operator[]` calls and a division) (before). Cycles per channel-sample at rate `0x10000` (a grain every
1,242 frames), mix `0x40000000`, density `-/+0x60000000` (sparse: two or three of the eight grains sound; dense: nearly
all), pitch randomness 0 or `0x7fffffff`, 120 BPM, ring run past 66,000 frames (`DELUGE_DSP_BENCH_CASES=granular`), on
2026-10-11. The figures are the minimum of four runs of each image; the last column is the after spread (max - min) in
cycles, worst size. The `delay_feedback_chain_*` and `calibration_loop` controls, which the change does not touch, agree
with the baseline to 0.1%.

| case | n=32 before -> after (delta) | n=64 before -> after (delta) | n=128 before -> after (delta) | after spread |
|---|---:|---:|---:|---:|
| `sparse_unison` | 148.76 → 130.26 (-12.4%) | 145.69 → 121.46 (-16.6%) | 151.97 → 123.92 (-18.5%) | 1.46 |
| `sparse_random` | 179.05 → 160.03 (-10.6%) | 178.23 → 155.35 (-12.8%) | 177.97 → 152.20 (-14.5%) | 1.66 |
| `dense_unison` | 333.43 → 329.47 (-1.2%) | 346.42 → 336.02 (-3.0%) | 341.92 → 329.27 (-3.7%) | 8.54 |
| `dense_random` | 346.66 → 345.10 (-0.5%) | 337.32 → 333.56 (-1.1%) | 360.52 → 352.17 (-2.3%) | 12.10 |
| `control delay_feedback_chain_analog` | 171.45 → 171.44 (-0.0%) | 165.71 → 165.72 (+0.0%) | 162.86 → 162.86 (+0.0%) | 0.03 |
| `control delay_feedback_chain_digital` | 19.92 → 19.91 (-0.1%) | 14.19 → 14.20 (+0.1%) | 11.35 → 11.35 (+0.0%) | 0.03 |

- **Sparse grains gain 11-19%; dense ones are about level.** The saving is the per-frame calls. A sparse
  case runs 116 instructions per channel-sample against 142 before; a dense one runs 222 against 221, since eight taps
  of the same arithmetic dominate. The dense cases carry about 125-150 data-cache stall cycles per sample and about
  1.1-1.4 L1 refills, the same before and after, so the kernel does not remove them; the cause is inferred, not
  isolated: each tap reads the 512 KB ring, far larger than the L1. The dense spread (8-12 cycles after, against 1-3.4
  before) is wider than the sparse (1.5), and the 0.5-4% dense deltas are within it: dense is about level.
- **The loop's shape.** At the shipping flags minus `-flto=thin` (Thumb-2), the frame loop of
  `effects::Granular::render` is 149 instructions plus a 5-instruction branch to the grain start, with no call, no
  `muls`, 3 `mul.w`/`mla`, 16 stack loads and 6 stack stores. An active tap is 53 instructions and a silent grain 5. The
  one call is `start_grain` (`[[gnu::noinline]]`, 636 B): a grain starts once per `period` frames, 0.08% of frames at
  the benchmark's rate, about one block in ten at n = 128. Before, `processOneGrainSample` was 112 instructions with 6
  calls (`__aeabi_uidivmod`, four `GrainBuffer::operator[]`, `setupGrainsIfNeeded`) and `processGrainFX` called it and
  `AudioEngine::feedReverbBackdoorForGrain` for every frame. The high-water mark is a comparison, not `std::max`, which
  is an out-of-line call at `-fno-inline-functions`.
- **`kStateInPlace` is kept.** The kernel steps its 330-byte State where it lives. The copied form (the trait removed)
  moves the State in and out through two `__aeabi_memcpy4` calls and the loop addresses a stack copy (24 stack loads
  and 14 stores a frame loop against 16 and 6); against in place it is slower in every case, two runs:

  | case | n=32 in place → copied | n=64 | n=128 | spread |
  |---|---:|---:|---:|---:|
  | `sparse_unison` | 130.26 → 139.20 (+6.9%) | 121.46 → 127.24 (+4.8%) | 123.92 → 126.61 (+2.2%) | 1.43 |
  | `sparse_random` | 160.03 → 170.25 (+6.4%) | 155.35 → 160.52 (+3.3%) | 152.20 → 155.40 (+2.1%) | 0.43 |
  | `dense_unison` | 329.47 → 340.44 (+3.3%) | 336.02 → 343.41 (+2.2%) | 329.27 → 332.15 (+0.9%) | 1.27 |
  | `dense_random` | 345.10 → 356.78 (+3.4%) | 333.56 → 339.66 (+1.8%) | 352.17 → 355.90 (+1.1%) | 0.27 |
  | `control analog chain` | 171.44 → 171.45 (+0.0%) | 165.72 → 165.71 (-0.0%) | 162.86 → 162.85 (-0.0%) | 0.01 |
  | `control digital chain` | 19.91 → 19.91 (+0.0%) | 14.20 → 14.20 (+0.0%) | 11.35 → 11.35 (+0.0%) | 0.01 |

  The copy costs 530-690 cycles a block (the fixed-cost gap of two-point fits).
- **Code size.** Measured as the summed extents of the functions in the two non-LTO objects at the shipping flags minus
  `-flto=thin`: `GranularProcessor.cpp.o` is 2,130 B of text (`setupGrainsIfNeeded` 610, `setupGrainFX` 342,
  `processOneGrainSample` 322, `processGrainFX` 274); `effects::Granular`'s object is 2,962 B (`render` 716,
  `start_grain` 636, the kernel's `prepare` 360, `Mono<Granular>`'s constructor 272, the component's `commit` 166,
  `finish` 84 and `prepare` 68). The after object holds the allocation and type-change paths that lived in
  `ModControllableAudio` and the owners before, so the two are not like for like.

## Stutter

`effects::Stutter` (`Mono<Stutter>`, one frame kernel over a tape the component owns, four modes) as the app renders it
(after) against the global `Stutterer::processStutter`, one loop per phase with the native/resampled and direction
choices made per frame (before). Each block also computes the rate from the stutter-rate param, as the owner does, in
both. Cycles per channel-sample; recording cases begin at the stutter-rate param `-0x40000000` (a 14,700-frame tape,
recording throughout the run), playing cases at `0` (a 918-frame tape recorded untimed), resampled cases with the param
moved `0x08000000` off the tape's native rate; 18,700,000 tick inverse, magnitude 1 (`DELUGE_DSP_BENCH_CASES=stutter`),
on 2026-10-12. The figures are the minimum of four runs of each image; the last column is the after spread (max - min)
in cycles, worst size.

| case | n=32 before → after (delta) | n=64 before → after (delta) | n=128 before → after (delta) | after spread |
|---|---:|---:|---:|---:|
| `control calibration` | 0.59 → 0.58 (-1.7%) | 0.30 → 0.30 (+0.0%) | 0.15 → 0.15 (+0.0%) | 0.01 |
| `control calibration_loop` | 32.10 → 32.10 (+0.0%) | 32.04 → 32.03 (-0.0%) | 32.02 → 32.02 (+0.0%) | 0.02 |
| `control delay_feedback_chain_analog` | 171.47 → 171.64 (+0.1%) | 165.73 → 165.81 (+0.0%) | 162.87 → 162.90 (+0.0%) | 0.02 |
| `control delay_feedback_chain_digital` | 20.05 → 20.10 (+0.2%) | 14.27 → 14.29 (+0.1%) | 11.38 → 11.39 (+0.1%) | 0.01 |
| `stutter_play_native` | 27.92 → 15.62 (-44.1%) | 25.70 → 13.04 (-49.3%) | 24.59 → 11.77 (-52.1%) | 0.03 |
| `stutter_play_resampled` | 59.33 → 40.59 (-31.6%) | 56.25 → 36.08 (-35.9%) | 54.79 → 33.94 (-38.1%) | 0.37 |
| `stutter_record_native` | 37.72 → 25.50 (-32.4%) | 35.86 → 23.13 (-35.5%) | 34.95 → 21.92 (-37.3%) | 0.25 |
| `stutter_record_resampled` | 177.73 → 183.67 (+3.3%) | 176.64 → 178.28 (+0.9%) | 175.08 → 176.01 (+0.5%) | 0.34 |

- **Play and native record gain 32-52%.** The kernel's loop is chosen once per block, so a native play step is the
  head's move and a copy (840 instructions a block of 128 frames against 1,394 before), and a resampled play no longer
  branches on the tape's form each frame. Native recording drops the same per-frame form test and the strengths'
  dispatch.
- **Resampled recording is level.** Its cost is the tape's triangle write (`DelayTape::writeSlowedDown`), which both
  run unchanged: 4% fewer instructions, a little more data-cache stall, and +0.5% at n=128 to +3.3% at n=32, a
  per-block cost of about 400 cycles in the component's configure and finish check against a 23,000-cycle block.
- **The first baseline measured nothing.** The bench runs before the app's start-up fills the params' neutral values,
  so the stutter rate was about 0 and every case recorded a resampled tape of the largest size; the cases now fill them
  (`functionsInit`) and begin at the rates above, and a probe on the device confirmed each case's phase, form and tape
  size before these runs.

## Chains

Each home's effect chain runtime (`effects::SoundChain`, `ClipChain`, `MasterChain`) against the same sequence written
out over the same components (`bench/reference_chains.h`), in one image (`DELUGE_DSP_BENCH_CASES=fx_chains`), on
2026-10-13. Settings: chorus, a ringing digital delay (feedback `0x30000000`, unsynced), EQ off flat, bitcrush and
sample-rate reduction on, compressor on (threshold `0x40000000`); a clip chain's saturation and both filters and the
master's filters on; the owner's delay working state set up in each block. The figures are cycles per channel-sample,
the minimum of four runs; the last column is the spread (max - min), runtime / reference, worst size.

| case | n=32 reference → runtime | n=64 reference → runtime | n=128 reference → runtime | spread |
|---|---:|---:|---:|---:|
| `control calibration_loop` | 32.10 | 32.05 | 32.02 | 0.01 |
| `fx_chain_sound` | 240.71 → 242.51 (+0.7%) | 214.02 → 214.33 (+0.1%) | 200.36 → 200.58 (+0.1%) | 0.61 / 0.27 |
| `fx_chain_clip` | 443.33 → 442.97 (-0.1%) | 405.86 → 405.57 (-0.1%) | 386.81 → 386.62 (-0.0%) | 0.46 / 0.39 |
| `fx_chain_master` | 398.95 → 398.60 (-0.1%) | 364.20 → 363.77 (-0.1%) | 346.14 → 346.28 (+0.0%) | 0.59 / 0.42 |

- **The call layer costs nothing measurable.** The clip chain's tap and the master's between are template parameters,
  inlined; the runtimes make the same component calls as the sequence.
- **The sound chain is +0.7% at n=32** (about 115 cycles a block), beyond its spread: it copies its 100-byte
  `FxInputs` each block to put the setting's mod FX type in. At n=128 it is level.

## Reverb

`effects::Reverb` (`Mono<Freeverb>` and `Mono<Plate>` over one buffer, the model chosen per block) as the app renders it
(after) against the engine's old reverb, a `std::variant` of three classes behind virtual knobs and `std::visit`
(before). Each case is the song reverb's block as `renderReverb` makes it: the model built afresh at the boot song's
knobs, both return levels 2^29, over the lanes output in place (`DELUGE_DSP_BENCH_CASES=reverb`), on 2026-10-29, after
image d068b23c3. Cycles per channel-sample, the minimum of four runs of each image; the last column is the after spread
(max - min), worst size.

| case | n=32 before → after (delta) | n=64 before → after (delta) | n=128 before → after (delta) | after spread |
|---|---:|---:|---:|---:|
| `control calibration_loop` | 32.11 → 32.11 (+0.0%) | 32.04 → 32.03 (-0.0%) | 32.02 → 32.01 (-0.0%) | 0.02 |
| `control delay_feedback_chain_analog` | 171.94 → 171.46 (-0.3%) | 165.96 → 165.71 (-0.2%) | 162.98 → 162.86 (-0.1%) | 0.01 |
| `control delay_feedback_chain_digital` | 20.34 → 19.89 (-2.2%) | 14.41 → 14.18 (-1.6%) | 11.45 → 11.34 (-1.0%) | 0.01 |
| `reverb_freeverb` | 276.94 → 252.33 (-8.9%) | 275.15 → 249.88 (-9.2%) | 274.78 → 249.02 (-9.4%) | 0.29 |
| `reverb_mutable` | 567.58 → 175.92 (-69.0%) | 564.00 → 174.91 (-69.0%) | 562.18 → 174.52 (-69.0%) | 0.32 |
| `reverb_digital` | 849.13 → 214.68 (-74.7%) | 843.80 → 213.74 (-74.7%) | 841.48 → 213.45 (-74.6%) | 0.19 |

- **The plates gain 69-75%.** The old Mutable and Digital loops called `toQ31Saturating` and the `FxEngine` line and
  context helpers out of line every frame under `-fno-inline-functions` (eight to nine calls per frame); the plate
  kernel's helpers are `[[gnu::always_inline]]`, so each frame loop has no call and no `muls`, and the LFO's step is
  `vmul`/`vsub`/`vadd.f32` on `d` registers. Mutable reads 169.4 at n=128 in an image built two commits earlier with
  the same plate code: a placement effect of about 3%.
- **Freeverb gains 9%, from two measured choices.** As first written, with its combs and allpasses in loops over the
  layout table and its State stepped in place, it ran at 332 cycles (+21%): the old code was fully unrolled. Unrolling
  them at compile time (each line's base an immediate) brought it to 277 in place, and copying the State into registers
  for the block to 249. Stepping the plate's State in place instead costs it 9-19%, so both States are copied.
- **The controls move by 0.1-2.2%**, the bound on placement noise between the two images.

## DX7

The `dx7` list (`DELUGE_DSP_BENCH_CASES=dx7`) times one voice's block through the voice adapter, as a sound renders it:
the patch's LFO, then pitch, control and the six operators. Before: the engine at 63a6eefda. After: a2e44c774, where
every operator is a dsp-next kernel walked by `dx7::Graph`, and the device renders the modern ops with no input or a
bus input through the same NEON kernel as before (`fixed/dx7/neon_fm_kernel.s`), every other build through the Argon
step bit-exact with it (`dx7::ModernVectorOperator`). Cycles per sample, one run of each image.

| case | n=32 before → after (delta) | n=64 before → after (delta) | n=128 before → after (delta) |
|---|---:|---:|---:|
| `control calibration_loop` | 32.11 → 32.12 (+0.0%) | 32.04 → 32.04 (+0.0%) | 32.02 → 32.02 (+0.0%) |
| `control delay_feedback_chain_analog` | 171.46 → 171.73 (+0.2%) | 165.71 → 165.87 (+0.1%) | 162.86 → 162.94 (+0.0%) |
| `control delay_feedback_chain_digital` | 19.87 → 20.15 (+1.4%) | 14.17 → 14.31 (+1.0%) | 11.34 → 11.39 (+0.4%) |
| `dx7_modern_alg1` | 153.38 → 168.34 (+9.8%) | 113.71 → 118.89 (+4.6%) | 95.74 → 97.42 (+1.8%) |
| `dx7_modern_alg32` | 162.33 → 178.80 (+10.1%) | 109.70 → 117.28 (+6.9%) | 85.61 → 89.13 (+4.1%) |
| `dx7_mki_alg1_fb` | 244.18 → 263.24 (+7.8%) | 208.41 → 216.43 (+3.8%) | 189.44 → 191.73 (+1.2%) |
| `dx7_mki_alg4_fb3` | 235.89 → 261.12 (+10.7%) | 204.96 → 218.77 (+6.7%) | 186.61 → 195.11 (+4.6%) |

- **What remains is per block.** Every case, modern and MkI, pays about 15 cycles per sample at n=32 and 2-5 at
  n=128: roughly 500 cycles per voice-block, from the structure around the operators (the patch's 156-byte snapshot,
  each op's gate and ramp computed through its kernel, the six envelope advances out of line). Replacing the gain
  ramp's 2^30 / n with a table read moved nothing: the A9's software divide is not where it goes.
- **The Argon step against the hand kernel.** Stepped one four-lane chain at a time, the Argon step ran 28-37% slower
  than the kernel at n=128: issue-empty cycles rose from 34 to 57 per sample at the same NEON count, each step waiting
  on the last's float latency. Three chains interleaved instruction by instruction (`kInterleave = 3`,
  `step_interleaved`), as the kernel computes twelve samples, brought the ops with no input level with it (algorithm
  32 without feedback +7%, inside the images' placement spread). The ops with a bus input stayed 29-36% slower
  (algorithm 1 without feedback: 76.4 against 104.1 at n=128): each group's first instruction adds the bus's fresh
  load, and the kernel issues those loads a block ahead. Loading the next group early in `Mono` made every case 25-30%
  slower: holding the next group's vectors takes the registers the three chains need, and clang spills. So the device
  keeps the kernel for those ops, and the graph spec holds the two bit for bit on ARM.
- **Placement moves these images by up to 8%.** Adding three bench cases to an image, with no change to the DX7 code,
  moved algorithm 32 from 85.0 to 92.4 at n=128. Compare cases within one image where the delta is small.
- **The voice's flags matter.** `dx7::Voice<CongNoise>` is instantiated once in `fixed/dx7/voice.cpp`, built ARM with
  `-finline-functions`. Instantiated in the voice adapter's app file instead, it built Thumb under
  `-fno-inline-functions` with fifteen out-of-line `std::array::operator[]` calls among others, and every case ran
  8-16% slower at n=32.
- **The kernel's arithmetic** is `vmla.f32`/`vmul.f32` + `vadd.f32` (no fused multiply-add on the A9's NEON),
  `vcvt.f32.s32 #22` and `#24`, `vcvt.s32.f32 #24`; the Argon step compiles to the same operations with no `vfma`.
  Its interleaved gain ramp is held in order by an empty `asm` with a `"+w"` operand on each intermediate gain: under
  `-funsafe-math-optimizations` clang otherwise folds `(g + d) + d` into `g + 2d`, which the vector spec built with
  that flag caught.
- **The controls move by 0.0-1.4%**, the bound on placement noise between the two images.

## Open questions

- Whether NEON-sourced stores overlap with arithmetic in kernels that do much more work per sample than a gain. The
  filter kernels do not answer it: they run on the integer core and the VFP, with no NEON in their loops. Measure it
  with the first NEON kernel of that weight.
- The `wavetable` bench case costs about 336 cycles per sample. Look at it when the oscillators move to the kernel form.
- `Mono::renderBlock` calls `std::span::size()` and `data()` per block, and both are out of line at the shipping flags.
- The DX7 voice's per-block cost (about 500 cycles per block over the old engine): trace one block to place it. The
  six operators are also walked one `Mono` at a time per block; fusing them into one step is a further bench question.
