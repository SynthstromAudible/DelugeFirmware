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
   the loop to within 2% ([Compressor](#compressor)).

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
(`DELUGE_DSP_BENCH_CASES=compressor`), measured on 2026-10-11. The after figures are the mean of two runs, which agree
within 0.02% on every compressor case. The unchanged `delay_feedback_chain_*` controls move by at most 0.8% between
the two images, so a difference inside ±0.9% is noise (≈).

| case | n=32 before → after (Δ) | n=64 before → after (Δ) | n=128 before → after (Δ) | inst/sample @128 |
|---|---:|---:|---:|---:|
| `wet_render` | 75.63 → 78.99 (+4.4%) | 68.74 → 70.81 (+3.0%) | 65.42 → 66.94 (+2.3%) | 57.8 → 54.3 |
| `wet_neutral` | 75.36 → 79.12 (+5.0%) | 68.53 → 70.91 (+3.5%) | 65.26 → 66.95 (+2.6%) | 57.8 → 54.3 |
| `blend_render` | 84.14 → 84.91 (+0.9% ≈) | 77.20 → 76.90 (-0.4% ≈) | 73.58 → 72.98 (-0.8% ≈) | 70.0 → 61.3 |
| `blend_neutral` | 83.99 → 85.16 (+1.4%) | 76.88 → 76.93 (+0.1% ≈) | 73.44 → 73.00 (-0.6% ≈) | 70.0 → 61.3 |
| control `delay_feedback_chain_analog` | 171.97 → 171.75 | 165.87 → 165.89 | 163.00 → 162.91 | 183.8 → 183.8 |
| control `delay_feedback_chain_digital` | 19.93 → 20.09 | 14.21 → 14.29 | 11.35 → 11.39 | 12.8 → 12.8 |

Fitted as a + C/2n, and confirmed by a PTM trace of the after image's loops:

| | loop, cycles per frame | per block, cycles |
|---|---:|---:|
| wet, before (two loops) | 124.0 | 871 |
| wet, after | 125.8 (traced: 126) | 1028 |
| blend, before (three loops and a copy) | 140.1 | 901 |
| blend, after | 138.0 (traced: 138) | 1018 |

- **The blend is as fast as before, the wet compressor 2–5% slower.** The fused blend loop drops the scratch copy and
  the separate blend pass and runs about 2 cycles per frame faster. The wet loop runs about 2 cycles per frame slower
  than the two loops it replaced, and every block costs about 150 cycles more, so the wet cases lose most at small
  blocks.
- **The per-block cost is the State's copy and the events.** In the trace, writing the State back after the loop is
  a 57-cycle block, and the copy in shares a 142-cycle block with the gain arithmetic: about 90 cycles for the copy.
  The out-of-line `set_levels` event costs about 30–40 cycles with its call, and `finish_level` measuring the block's
  last frame about 15. Stepped in place, the State needs no copy, but the loop then stores eight State words every
  frame (a codegen probe at the shipping flags), so the copy stays.
- **Measured on the output it had just made, the level measurement cost 12%.** That form (before the measurement ran
  one frame behind) ran the wet cases 11.7–13.5% and the blend cases 1.3–3.7% slower than before, with 4 fewer
  instructions per channel-sample but 8.6 more issue-empty cycles per channel-sample on wet. Each frame was one
  dependency chain: the gain multiply, the table lookup, the interpolation, then the detector, the square and the
  64-bit add. Measuring the frame before takes the measurement off that chain, and the wet loop fell from about 138 to
  126 cycles per frame. The 64-bit sum's conversion to float, through two words and an exact double rather than the
  library's 64-bit integer conversion, saves about 65 cycles more per block.
- **The loops call nothing.** With the shipping flags, neither loop has a call, a `muls` or an out-of-line accessor;
  each stores only its output frame (one `strd`) and its spills: 21 stack loads and stores per frame in wet (104
  instructions), 26 in blend (118). The level's 64-bit sum is among the spills.

## Open questions

- Whether NEON-sourced stores overlap with arithmetic in kernels that do much more work per sample than a gain. The
  filter kernels do not answer it: they run on the integer core and the VFP, with no NEON in their loops. Measure it
  with the first NEON kernel of that weight.
- The `wavetable` bench case costs about 336 cycles per sample. Look at it when the oscillators move to the kernel form.
- `Mono::renderBlock` calls `std::span::size()` and `data()` per block, and both are out of line at the shipping flags.
