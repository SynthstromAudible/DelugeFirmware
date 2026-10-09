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

1. **Fuse before anything else.** Each kernel run as its own pass over a buffer pays a NEON store of every sample, and
   the store is the dearest step. A chain that keeps intermediate results in registers (`Chain` in the kernel form)
   pays it once. A gain, pan or mix stage belongs inside the kernel that produces or consumes the signal, as
   `Amplitude` multiplies and accumulates into its output in one pass. A standalone gain pass costs about 3.4 cycles
   per sample, almost all of it memory traffic.
2. **Unrolling (`U`) helps little for streaming kernels.** A gain unrolled two, three or four vectors per iteration
   gains at most about 12%. The loop is bound by the multiply's issue rate and the store, not by latency.
3. **Interleaving independent streams (`Lanes<K, N>`) is for recursive kernels.** A filter or envelope whose next
   sample depends on its last is latency-bound. N independent channels interleaved hide the latency when N is at least
   the critical operation's result latency divided by its issue cycles. For `VQDMULH.32` Q (4 issue, 7–9 latency) that
   is 2–3 streams; for `VQDMULH.16` (1 issue, about 6 latency) about 6. The bench and the trace confirm N on the device.
4. **Prefer 16-bit lanes where the precision allows.** Their multiplies run at full rate and a Q register holds eight
   samples, which also halves the store traffic per sample.
5. **Keep per-block glue out of the loop.** The app builds with `-fno-inline-functions`, so an accessor such as
   `std::span::size()` is a call. Once per block it costs a few cycles per block. Inside the loop it costs that per
   sample.

## Open questions

- Whether NEON-sourced stores overlap with arithmetic in kernels that do much more work per sample than a gain, such as
  filters. Measure when the first filter kernel lands.
- The `wavetable` bench case costs about 336 cycles per sample. Look at it when the oscillators move to the kernel form.
