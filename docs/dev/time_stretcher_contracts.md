# The time stretcher's numeric contracts

The arithmetic the time stretcher's rewrite must keep for its output to stay bit-identical, one table per component of
[time_stretcher_design.md](time_stretcher_design.md), which defines the components and uses the words below.

Each contract is a computation whose result feeds output samples (or, for S3, S4, S5, B6, B8, E1 and P4, decides when
they change). It is written as its inputs, its exact arithmetic in order, and the legacy computation it must equal.
Each becomes a unit spec against values recorded from the legacy code before its component is written (the design
note's open question 4). A side that only a card that has not answered, a failed allocation, a cull or an unbuilt
source reaches (S4, H1's failure exits, B8's move, A4's abandoned search, C1's chunk-end limit, P1 and P5's third chunk,
and the reservations) gets its values as the design note's step 1 table says (its open question 11): fault injection in
the probe, a sweep, or accepted as unpinned. Integer types are the legacy's: where a contract says 32-bit unsigned, a
signed value is reinterpreted, and the wrap is part of the contract. A **quirk** is arithmetic a fresh design would not
choose; it is kept and named in the code. The goldens pin each quirk unless its row says no golden case is known to pin
it.

Words: a **frame** is one sample of every channel; a **pixel** is 128 source frames, the span one percussiveness byte
describes (pixel `p` is frames `[128p, 128p + 128)`); the **pre-margin** is the waveform before the start marker in
the play direction; a **fudge** is a stretcher an unstretched looping audio clip takes for its last frames before a
loop end, only to crossfade into the pre-margin; `dir` is +1 forwards, −1 reversed; the **ratio**, **increment** and
**combined** increment are UQ8.24 (2^24 is 1.0).

### Hop tuning

| # | Inputs | Arithmetic, in order | Must equal |
|---|---|---|---|
| T1 | ratio | `log = quick_log(ratio)` (`src/dsp/fixed/curves/quick_log.h`). Fine band when `800·2^20 ≤ log < 864·2^20`: `pos = log − 800·2^20`; each field is `kLinearTableInterpolator.interpolate_signed(pos, 26, fine_table, 4)`; widths and the fixed crossfade part `>> 16`; the share `<< 8` (32-bit, wrapping); the jitter share unshifted. | `getHopParams`, fine branch |
| T2 | ratio | Coarse band otherwise: clamp `log` to [768·2^20, 896·2^20]; `pos = log − 768·2^20`, then `pos = 2^27 − 1` if `pos ≥ 2^27`; each field is `interpolate_signed(pos, 27, coarse_table, 2)`, shifted as T1. | `getHopParams`, coarse branch |
| T3 | — | The ten tables, fine (17 entries, −12 to +12 semitones of speed in steps of 1.5) and coarse (5 entries, −2 to +2 octaves): minimum width, maximum width, crossfade share, fixed crossfade part, jitter. Values as the legacy's `minHopSizeFine` … `randomCoarse`. **Quirks:** the fixed part's fine entry at −1.5 semitones is 170 among 10s; share entries of 128 and over (fine 160 and 140, coarse 200 and 160) wrap negative at `<< 8`, which B5's unsigned cap turns into half the hop (pinned: `sample/ts_speed_slow` at a ratio of 0.5, and `RS_AUDIO_STRETCH`'s RSCRAWL and RSHALT tracks at 0.476 and 0.238). | the legacy tables |

### Hop scheduler (in the playback-side stretcher)

| # | Inputs | Arithmetic, in order | Must equal |
|---|---|---|---|
| S1 | noise | First hop: `200 + (int8_t(noise.next() >> 24) >> 2)` output frames (−32 to +31); ramp done (`progress = 2^24`, increment 0). Exactly one draw, at a normal start; none at a fudge. | `TimeStretcher::init`, normal start |
| S2 | frames to loop `n` (output), increment | If increment ≠ 2^24: `n' = (uint64(n) × increment + 2^23) >> 24`, else `n' = n`. The new head starts `n'` frames into the pre-margin; ramp `over_fudge(n')` (X1 c); the next hop never comes (INT32_MAX). **Quirk:** the crossfade lasts `n'` output frames, a source-frame count, so a pitched fudge's crossfade ends off the loop point (pinned: `RS_AUDIO_STRETCH_EDGES`'s tied tracks). | `TimeStretcher::init`, fudge |
| S3 | hops this routine `h`, times passed over `m`, frames to hop `t` | Per segment, only while the newer head reads: due when `t ≤ 0`; allowed when `h < 3` and `uint32(m) ≥ uint32(h) << 1`. Due and allowed: hop, `h += 1`, `m = 0`, culling bypassed. Due and not allowed: `m += 1`. When allowed, the segment is `min(rest of block, t)`; when not, the rest of the block (so `t` goes negative). After the segment `t −= length`. | `VoiceSample::render`'s hop check; `TimeStretcher::hopEnd`'s counter and culling bypass |
| S4 | — | A hop whose new head cannot start (its chunk not ready): the old head is put back as the newer, the older is marked stopped, the ramp's increment is 0 and its progress is left at 0 (set by the hop before the start failed), the next hop in 500 frames; the wanted chunk is reserved for the retry if it is not resident, else the retry reservation closes. | `TimeStretcher::hopEnd`, failed start |
| S5 | new position | Restart: the position is set; if the newer head has stopped, hop now. | `TimeStretcher::reInit` |
| S6 | start-marker byte, synced frames in `k` | Synced hop: `start = uint32(start_byte − data_start) / uint8(bytes_per_frame)`; `position_frames = start + k × direction`; position `= uint64(position_frames) << 24`. | `TimeStretcher::hopEnd`, synced branch |

### Hop planner

| # | Inputs | Arithmetic, in order | Must equal |
|---|---|---|---|
| B1 | shape, noise | One draw per hop, before any branch: `f = mul(Q31(noise.next()), Q31(jitter << 8))` (Q30, floor); `min += scale_shl_wrap<2>(mul(Q31::from_raw(min), f)).raw()`. The maximum is not jittered. | `TimeStretcher::hopEnd`, the random element |
| B2 | shape, increment | Repitch: `min = (uint64(uint32(min)) × increment) >> 24`, likewise `max`; default best `(min + max) >> 1`. | `hopEnd`, repitching |
| B3 | perc window, widths, ratio, position | `top = int32(position_frames >> 7)`. For `w` from `min`, while `uint32(w) < uint32(max)`, step 128: `back = top + int32((int64(w) × (ratio − 2^24)) >> 32) × dir`; `front = top + int32((uint64(w) × (ratio + 2^24)) >> 32) × dir`; `pw = (front − back) × dir`; skip if 0; stop if `front` passes `latest` or `back` passes `earliest`; keep a running total over pixels [back, front) (adding ahead; removing behind; re-adding, decrement first, when `back` moves back); best when `total × best_pw ≥ best_total × pw` (32-bit products; ties go to the wider). Starts `best_total = 0`, `best_pw = 1`. | `hopEnd`, the perc search |
| B4 | best width, ratio, position | `back = position_frames + int32((int64(best) × (ratio − 2^24)) >> 25) × dir`, clamped to the waveform's first frame in the direction (0, or length − 1). | `hopEnd`, the back edge |
| B5 | best width, increment, shape, ceiling | `hop = (uint64(best) << 24) / uint32(increment)`; `hop = 1` if under 1. `xf = uint32(mul_r(Q31::from_raw(hop), crossfade_share).raw() + fixed × 4)`; if `xf ≥ uint32(hop >> 1)`, `xf = hop >> 1`. `hop −= xf`; `hop = min(hop, ceiling)`; `xf = min(hop, xf)`; ramp `over_hop(xf)`. | `hopEnd`, hop and crossfade lengths |
| B6 | back edge | Past the end (`(back − end) × dir ≥ 0`, end = length or −1): the new head is silenced; no alignment, no new head. | `hopEnd`, past the end |
| B7 | loop facts, position, combined, ratio, increment | Only for the stretcher-made loop with a pre-margin: `src = int32(loop_end_frame − position_frames) × dir`; if `src ≥ 0`: `out = ((uint64(src) << 24) + (combined >> 1)) / combined`. If `out < 100`: `into = out`, or `((uint64(src) << 24) + (ratio >> 1)) / ratio` when increment ≠ 2^24; start `into` frames before the loop start; if inside the waveform: `xf = max(out, 10)`, `hop = max(min_jittered >> 2, xf)` (the width before repitching), ramp `over_loop(xf)`, align unless a cache is being written or read. Else `ceiling = out − 100 + 32`. | `hopEnd`, the pre-margin loop crossfade |
| B8 | start, markers, residency | A start before the start marker whose frame is not resident moves to the start marker; checked after B4 and again after alignment. | `hopEnd`'s `clampToStartIfNotLoaded` |

**The ratio's signedness.** The legacy passes the ratio to B3, B4, B7 and the hop-back reach as `int32_t`. Below
2^31 − 2^24 (a ratio of about 127, reached about 7 octaves below the root) none of their sums wraps, and each equals
the same arithmetic on the unsigned ratio widened to 64 bits; every golden is in that range. At and above it the
legacy's ratio reads negative or its sum `ratio + 2^24` wraps (7 to 8 octaves down, where the ratio has not yet
wrapped itself). The rewrite reads the ratio unsigned everywhere, so the saturated `2^32 − 1` of the design note's open
question 3 is a ratio of 256, and this range is part of that accepted difference. `reassessWhetherToBeFillingBuffer`
also compares the ratio, but only under `TIME_STRETCH_ENABLE_BUFFER`, which `src/definitions_cxx.hpp` defines as 0 and
no build overrides, so it is compiled out and reads no ratio.

### Waveform aligner

| # | Inputs | Arithmetic, in order | Must equal |
|---|---|---|---|
| A0 | the newer head's reader, about to become the older | The old head's reference byte: `(region_index << chunk_shift) + (cursor − payload_base) + 4 − byte_depth`; if the reader's last interpolation width `w` is not 0, minus `(w >> 1) × channels × byte_depth × dir` (half the interpolation window, against the play direction). A reader holding no region gives `end_byte + int32(cursor) × dir`. A1 and A2 use it; the search runs only if it is at or after the data's start. | `SampleLowLevelReader::getPlayByteLowLevel` with its interpolation compensation, as `hopEnd` calls it before the hop |
| A1 | increment, crossfade | `each = clamp(int32((uint64(increment) × 35) >> 24), 1, 70)`; `xf_src = int32((uint64(xf) × increment) >> 24)`. Only while the older head reads, and only if its byte is not before the data's start. | `hopEnd`, the search set-up |
| A2 | frames, start, `xf_src`, `each` | `mid = start + (xf_src >> 1) × dir`; `read = mid − ((each × 3) >> 1) × dir`. Forwards only: fail if `read` is within half the crossfade of either end. Fail if the window's end falls outside the data (one byte of slack before its start). Three totals, each the sum over `each` frames of `word >> 16` per channel (left plus right). The words are unmasked 32-bit reads ending at each sample's top byte. **Quirk:** the reversed direction has no half-crossfade margin; kept; no golden case is known to pin it. | `Sample::getAveragesForCrossfade` |
| A3 | hop frames, increment, rate | `limit = (hop × 40) >> 8`; `limit = int32((uint64(limit) × increment) >> 24)`; `limit = min(limit, (rate / 45) >> 1)`. `hop` is the value after B5 (or B7). | `hopEnd`, the search size |
| A4 | old and new averages | Best starts as `totalDifferenceAbs(old, new)` at offset 0. Search the play direction first. Each pass: the four window boundaries step one frame at a time (against play: the first boundary starts one frame back); each running total gains its upper boundary's value and loses its lower one, signed by search-against-play. At each step: if this is the first step of a first pass searching with play and it is worse, switch direction now (that pass does not count: the other direction is searched in full, then the play direction again); a strictly smaller difference is the new best; when the sign of `totalChange` flips, count a flip (four flips over both passes stop the search). A pass ends at the limit or the waveform's end; a frame not resident abandons the search. | `hopEnd`, the phase search |
| A5 | at a sign flip, increment ≠ 2^24 | If this step or the previous is the best: `phase = (uint64(abs(last)) << 24) / uint32(abs(last) + abs(this))`, `2^24 − phase` when searching against play; if (this step is best) differs from (searching against play), the best offset moves one frame back in play. At the end: `phase += older head's phase`; at or past 2^24, subtract 2^24 and move one frame on. | `hopEnd`, sub-sample alignment |
| A6 | offset | The start moves by the offset; clamped to the waveform's first frame; then B8. | `hopEnd`, after the search |

### Crossfader

| # | Inputs | Arithmetic, in order | Must equal |
|---|---|---|---|
| X1 | frames `n` | Four increments, all 32-bit unsigned: (a) a hop: `2^24 / n`; (b) a loop crossfade: `(2^24 − 1 + n) / n`; (c) a fudge: `(2^24 + n) / n`; (d) a cache tail: `2^24 / uint16(n) + 1`. Progress starts at 0. **Quirk:** (b) and (c) differ by one where `n` divides 2^24; kept; no golden case is known to pin it, and the probe logs each fudge's `n`. | `hopEnd` (a, b), `init` (c), `setupCrossfadeFromCache` (d) |
| X2 | ramp, segment `n`, level, step | `newer = as_wrap<Q31>(Q24::from_raw(progress))`; `older = 2147483647 − newer`; `progress += increment × n` (wrapping); `after = as_clamp_then_shift<Q31>(Q24::from_raw(progress))`; `newer_step = divide_by_small(after − newer, n)`; `change = mul_as_wrap<Q31>(level, newer_step)`; gains: newer `mul_as_wrap<Q31>(level, newer)` stepped `level_step + change`; older `mul_as_wrap<Q31>(level, older)` stepped `level_step − change`. | `VoiceSample::render`, the crossfade envelope |
| X3 | ramp, heads | The older head sounds while it reads and `progress < 2^24`; after it is read in a segment, if `progress ≥ 2^24` it stops reading. A plain start's older reader is a level copy of the newer with the ramp already done: its first segment reads it at level 0 and step 0 (it adds exactly 0: `mixed_in` rounds a zero product to 0), and the check above then stops it, so it reads in that one segment only. That read can fail on a card error, which cuts the voice. The stopped reader keeps its cursor, which E1 compares against. | `VoiceSample::render`, the older head |
| X4 | voice amplitude `a`, step `s` | `a <<= 3`; `<<= 1` more if increment ≠ 2^24; `<<= 1` more if stretching (likewise `s`). The level is `a >> 1`, step `s >> 1`; when writing a cache, level 2147483647, step 0. After each segment, when not writing a cache, `a += s × n`. | `VoiceSample::render`, amplitude set-up |

### Cache tail

| # | Inputs | Arithmetic, in order | Must equal |
|---|---|---|---|
| C1 | frames to hop, cache position, written end | `n = min(frames_to_hop, 255)`; nothing if the written end is within one cache frame (`3 × channels` bytes); `to_chunk_end = chunk_size − offset + (3 × channels − 1)`, nothing if `≤ 3 × channels`; `n = min(n, min(to_chunk_end, written_end − position) / (3 × channels))`. | `setupCrossfadeFromCache` |
| C2 | cache bytes | Each word is the unmasked 32-bit read whose top three bytes are the cache sample (the read origin of `frame_read_origin` in `src/deluge/storage/cluster/cluster.h`): its low byte is the byte before the sample. **Quirk:** that low byte reaches the mix. | `setupCrossfadeFromCache`, the copy |
| C3 | `n` | Ramp `over_cache_tail(n)`; the older head reads the tail from its start. | `setupCrossfadeFromCache` |
| C4 | tail, segment, level, step | Per frame: read (stereo, or mono); position `(p + 1) & 255`; to mono `(l >> 1) + (r >> 1)`; level `+= step` first; `out = mla(Q30 view of out, sample, level)`, the high word **floored**, not rounded. **Quirk:** the sample reader's heads round (`mixed_in` in `src/deluge/model/sample/sample_low_level_reader.h`); the tail floors. | `TimeStretcher::readFromBuffer` |

### Perc analysis

| # | Inputs | Arithmetic, in order | Must equal |
|---|---|---|---|
| P1 | length, chunk size | One byte per 128 frames per direction: `bytes = ((length − 1) >> 7) + 1`, at least 1. One block when `bytes < chunk_size / 2`, else `ceil(bytes / chunk_size)` chunks of `chunk_size` bytes (with 32 KB chunks: one block below 2,097,152 frames; a chunk covers 4,194,304 frames). Pixel `p` covers frames `[128p, 128p + 128)`. | `Sample::fillPercCache`, the store |
| P2 | frame words | `x = (left_word >> 2) + (right_word >> 2)` (unmasked words, as A2); `slope = abs(x − last)`; `last = x`; for each of two poles: `pole += (slope − pole) >> 9` (floor), `slope = pole`. **Quirk:** floor, where the pitch shifter's follower rounds (`SlopeFollower` in `src/dsp/fixed/pitch_shift/slope_follower.h`). | `fillPercCache`'s inner loop |
| P3 | slope, previous slope | When the fill reaches frame `128p + 64` forwards (`128p + 63` reversed), from the slope of the last frame read: `d = abs(slope − previous)`; `v = slope ? ((uint64(d) × 2^18) / slope) >> 1 : 0`; byte `= tanh<23>(Q31::from_raw(v)).raw()` (0 to 63). The previous slope is the frame before's. The same arithmetic as the pitch shifter's `percussiveness` (`src/dsp/fixed/pitch_shift/slope_follower.h`). | `fillPercCache`, the pixel byte |
| P4 | position, increment, block | Only while stretching (ratio ≠ 2^24): `end = position_frame + (increment >> 10) × dir`. Skip if the last fill was this direction, started at or before the position, and reached `end`. Else fill from the position to `end + block × 4 × dir`, at most `block × 32` frames when writing a cache, else `block × 6`. | `VoiceSample::weShouldBeTimeStretchingNow` |
| P5 | zones | A fill continues the zone it starts in, or one ending at most 2,048 frames short of it (restarting there); a new zone's first `max(2048, its length)` frames may be overwritten by a zone growing into it, which then merges. The slope state is the zone's. | `fillPercCache`, zones |

### Heads

| # | Inputs | Arithmetic, in order | Must equal |
|---|---|---|---|
| H1 | start byte, phase, loop type | Seating a reader at a hop's (or fudge's) start: its leases go first; the chunks for the start byte are set up and must be ready, then the reader moves on a chunk if it must (looping at the low level only for that loop type); its interpolation width is set to 0 and its phase to the planned phase (A5, or 0). Either step failing fails the start: a hop then takes S4; a fudge's begin fails, and the audio clip unassigns the voice. A start that succeeds but leaves the reader holding no region stops the newer head (B6's outcome, from the reader's side). A plain start's level copy of the newer reader also gets interpolation width 0. | `TimeStretcher::setupNewPlayHead`; `TimeStretcher::init`, normal start |

### Position, loop, exit and cache write

| # | Inputs | Arithmetic, in order | Must equal |
|---|---|---|---|
| Q1 | segment `n` | `position += int64(combined) × n × dir`, combined `= (uint64(uint32(ratio)) × uint32(increment)) >> 24`. | `VoiceSample::render`, after the heads |
| Q2 | position | The whole frame: `position >> 24` forwards, `(position + 2^24 − 1) >> 24` reversed. | `TimeStretcher::getSamplePos` |
| Q3 | play byte | Start: `position = floor((play_byte − data_start) / bytes_per_frame) << 24` (rounding toward −∞; the byte may be before the data). | `weShouldBeTimeStretchingNow`, `fudgeTimeStretchingToAvoidClick` |
| Q4 | position, block part `n`, loop end byte | Unsynced, per uncached read: `after = position + combined × n × dir` (`+ 2^24 − 1` reversed); `over = (int32(after >> 24) × bytes_per_frame + data_start − loop_end_byte) × dir`; if `over > 0`: `end = rel ≥ 0 ? rel / uint8(bytes_per_frame) : −1`; `left = (int64(end) << 24) − position`; if `left × dir < int64(combined)`: looping restarts at `(int64(start_frame) << 24) − left` (S5), at most 1,024 times in a block; not looping ends the note. Else `n = min(n, uint64(left × dir) / combined)`. | `VoiceSample::render`'s loop check |
| E1 | both readers' cursors, sync lag | Checked at a block's start when the ratio is back at 1.0 and the stretcher is not a fudge, never in the block a stretcher begins (every begin skips the check): stretching ends when the newer reader's cursor equals the older reader's, whether the older is reading or stopped, and the voice is not synced or lags by 0. By reading the code: after a plain begin's single first segment the shadow copy (X3) has read the same frames from the same cursor, so at an increment of 1.0 the cursors meet at the next block's start and a one-block stretch ends there, before any hop; pitched, the copy's interpolation width of 0 can leave its cursor elsewhere. Even at 1.0 the cursors part when the newer reader still has a nonzero interpolation width from an earlier resampled block: its next window check (`considerUpcomingWindow`) jumps back `(width >> 1) − (oscPos >> 23)` frames, and the width-0 copy does not. After a hop, E1 holds when the newer's cursor meets the stopped older's. The coverage run took this exit 10 times. | `VoiceSample::render`, leaving the stretcher |
| W1 | stretched mix | Writing a cache: the heads render at X4's cache level into a zeroed scratch block; per frame the cache takes the top three bytes of `word << 1`, left then right; the output mixes the word (mono: `(l >> 1) + (r >> 1)` after the left is cached) with `mixed_in`, the amplitude stepped first. | `VoiceSample::render`, the cache copy |

**Counts.** 44 contracts: hop tuning 3, scheduler 6, planner 8, aligner 7, crossfader 4, cache tail 4, perc analysis
5, heads 1, position, loop, exit and cache write 6. Seven are about when, not what (S3, S4, S5, B6, B8, E1, P4); the
rest are arithmetic.

**Noise draws.** The synth's noise stream is shared by every voice, so the draw count is a contract too: one at a
normal start (S1) and one per hop attempt, a failed one included, whatever the hop does after (B1). None at a fudge or
at a restart that does not hop.

