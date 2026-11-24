# The time stretcher: design and numeric contracts

The sample time stretcher is rewritten as new C++23 code: pure DSP components in `src/dsp/fixed/timestretch/`, beside
`src/dsp/fixed/timestretch/crossfade.h`, a read head in `src/dsp/fixed/sample_read/`, and the playback side in
`src/synth/sources/sample/`. Its output stays bit-identical but for the differences accepted by name below. This note
defines the components; the arithmetic they must keep, contract by contract, is in
[time_stretcher_contracts.md](time_stretcher_contracts.md), which also defines the words used here. The legacy stretcher
(`src/deluge/dsp_legacy/timestretch/time_stretcher.cpp` and the stretcher paths of its five users) and the sample reader
it reads through (`SampleLowLevelReader`) appear here only as the computation each contract must equal; nothing is
moved, split or copied from them.

## Decisions

The decisions (section 7), one line each:
1. Where the stretcher reaches the sample: its own `SampleStretchPort` (14 operations); `SamplePlaybackPort` unchanged.
2. The cache tail's buffer: eight tail buffers the engine makes at start, lent through the `StretchContext`.
3. The ratio far below the root: read it unsigned everywhere, saturate it at `2^32 − 1`, floor B5's hop at 2.
4. Recording the contracts' values: a temporary probe of the legacy code, only its records header committed.
5. The plain begin's shadow reader: kept, as the `Shadowed` phase, and deleted once the rewrite is proven (`TODO.md`).
6. The slope follower and percussiveness byte: the pitch shifter's own `SlopeFollower` and `percussiveness`, shared.
7. The aligner's abandoned search: start the head at phase 0.
8. The read heads: a new read head (2.8) in the DSP library, its leases app-side; unstretched playback switches to it.
9. The aligner's search: one core shared with the pitch shifter's `WaveformAligner`, parameterised by its flip
   count, its first direction and whether it turns.
10. The reachable sides no gate scripts: build render-song cases for them (marker moves, clip-length edits, late starts,
    the stopped-clock tail) before step 7.
11. The sides no golden reaches: fault injection in the probe, a sweep for C1's chunk-end limit, and the third
    perc-cache chunk accepted as unpinned (step 1's table).
12. When the read head lands: after the playback side, as step 7a.
13. The unison clone's stop at its next chunk: fixed; the clone opens its own cursor, a difference accepted by name.

The differences accepted by name; everything else stays bit-identical, and the contracts file's quirks are kept:
- **The shared slope follower** (6): each pole's step rounds where the legacy's floors, so the perc bytes, and with them
  where hops land, can change. Step 4 re-records the `sample/ts_*`, `sample/timestretch` and `RS_AUDIO_STRETCH*` goldens
  whose output changes; every other step is bit-identical to that baseline.
- **Far below the root** (3): from about 7 octaves down the ratio reads unsigned and saturates at 256 instead of reading
  negative or wrapping, a one-frame hop gets a one-frame crossfade instead of dividing by zero, and a search limit
  of 0 (a hop of a few frames, or the increment that far down) searches nothing instead of reading on without bound.
- **An abandoned alignment** (7) starts the head at phase 0, not at a phase found for an offset it did not take.
- **A ninth crossfade out of a cache** (2) while all eight tail buffers are lent gets no crossfade, where the legacy
  allocates one.
- **A unison part added to a sounding sample voice** (13) plays on past its next chunk boundary, where the legacy's
  clone, which has no region-port cursor, stops there. Step 7a re-records any golden this changes.
- **Left unpinned:** the perc store's third-chunk rotation (11), which decides residency only; the reversed-while-cached
  and auditioned edit sides, and the recording and stem-export sides (10).

## Coverage

The goldens that pin the stretcher are the `sample/ts_*` and `sample/timestretch` render-preset cases and the
`RS_AUDIO_STRETCH*` render-song gates (`tests/integration/checks/render_song_cases.md`). A host coverage build of them
(`scripts/tasks/stretcher_coverage.py`) reaches 1,295 of the 1,650 branch sides of the stretcher, the time-stretch and
sample-read components, the stretch port, the read lease and their users' stretcher functions.
[time_stretcher_coverage.md](time_stretcher_coverage.md) lists the 355 it does not reach, keyed by function and
condition, each with a category, a reason where the category's meaning does not already give it, and what pins it:
- 102 the code itself rules out (defensive checks, conditions fixed by construction);
- 112 need a card that has not answered, an allocation that fails or memory pressure, or CPU load high enough to cull
  or to shrink a head's sinc window;
- 14 are not stretcher paths;
- 126 are reachable on the device, and no gate scripts them. EDIT (19) and LATE (13) are the edit and late-start paths
  the decision-10 songs leave out: render-audio-stretch-marker and render-audio-stretch-length resize a clip while its
  stretched voice plays, and render-audio-stretch-late starts playback part-way through stretched clips. STOP (4)
  needs a render shorter than a block: a stop fast-releases an audio clip's envelope within its first 128-frame block,
  so its voice never renders after the clocks stop. REC (13) is an audio clip recording, EXPORT (14) the stem export's
  render, and OPEN (63) sides a preset or song could reach but none is built for: a third perc-cache chunk, a crossfade
  out of a cache whose read position is in a cache chunk's last frame, and the read head's content corners (a stereo
  sample read linearly while rendering in stereo, a sinc step over 16 frames, tail zeros past a non-looping end,
  among others).

Each residency, memory, cull, edit, late-start, stop and unbuilt side is pinned: by a records array whose legacy rows
take it (step 1's table, decision 11), by a unit spec case for the read head's and the search's own paths, or by an
acceptance named in the list (decision 10, decision 11 for an app-side fault whose value no record holds, the STOP
ruling, decision 3).

## Words

Used throughout:
- **frame**: one sample of every channel; positions count frames from the audio data's start.
- **ratio**: the stretch ratio, UQ8.24 (2^24 is 1.0): how fast the source is consumed against the output, pitch
  excluded. Above 1.0 the sound is sped up.
- **increment**: the phase increment, UQ8.24: how fast a head reads the source. Above 1.0 is pitched up.
- **combined**: `(ratio × increment) >> 24`, 64-bit: source frames consumed per output frame, UQ.24.
- **direction**: +1 forwards, −1 reversed.
- **head**: a read head (2.8) playing the source from some position at the increment.
- **pixel**: 128 source frames, the span one byte of the perc analysis describes.
- **pre-margin**: the waveform before the start marker, in the play direction; a stretcher-made loop crossfades into it.
- **fudge**: a stretcher an unstretched looping audio clip takes for its last frames before a loop end, only to
  crossfade into the pre-margin.

## 1. What the stretcher does

**What it takes in.** A stretched voice hands the stretcher, every render block:
- the source position, a 64-bit count of frames with 24 fractional bits, which the stretcher advances by the combined
  increment per output frame. It starts at the voice's play position. A tempo-synced audio clip resets it to the
  sequencer's position at every hop instead (`RS_AUDIO_STRETCH`: all 1,653 hops synced);
- the pitch, as the increment; and the speed, as the ratio. A synth voice derives the ratio from its time-stretch
  amount and its pitch (`sample/ts_speed_slow`, `sample/ts_pitch_*`); an audio clip from its length against the tempo;
- the direction (`sample/ts_reversed`, the reversed tracks of `RS_AUDIO_STRETCH` and `_ARRANGED`);
- the repeat mode, as a loop type: no loop, a loop the reader makes at the loop end (`sample/ts_repeat_loop`,
  `sample/ts_long_hold`, 20 restarts in one hold), or, for an audio clip, a loop the stretcher makes by crossfading into
  the region before the start marker (24 such crossfades on `RS_AUDIO_STRETCH`'s tone track).

**What it produces.** The output is the sum of two heads reading the source at the increment. The newer head plays;
at each hop a new head starts and the old one fades out under a linear crossfade. Between hops one head sounds alone.
A hop places the new head behind or ahead of where the source position is, by the ratio: slowed down, heads replay a
stretch of source; sped up, they skip one. So the source advances at the combined increment while each head advances
at the increment.
- The first hop comes 200 output frames into the note, give or take 32 at random.
- Later hops come every few thousand output frames: the width depends on the ratio through tuned tables (a fine band
  within an octave of 1.0, a coarse band out to two octaves, clamped beyond: `RS_AUDIO_STRETCH`'s RSCLAMP and RSHALT
  tracks, stretched at 4.46 and 0.238, `sample/ts_pitch_up36` and `_down36`), and the crossfade is a share of the hop
  plus a fixed part.
- The new head's start is moved a few frames, and given a sub-sample phase, so that its waveform lines up with the old
  head's at the crossfade's middle: a search over three moving averages either way.
- At most three hops run in one audio routine call across all voices; a voice whose hop is due waits, and the more
  often it has waited the sooner it gets its turn.
- A hop that would start past the waveform's end silences the new head; the note ends when both heads have run out.

**The perc analysis.** Each sample keeps, per direction, a byte of percussiveness for every 128 source frames, filled
ahead of the play position as the voice plays. A hop chooses its width, between the table's minimum and maximum, as the
one whose span of source holds the most percussiveness on average, so hops land on transients and drum hits stay short
when sped up. On the percussive source `sample/ts_perc` 27 of 32 hops move off the default width; on the tone
`sample/ts_tonal` none of 4 do.

**Where a cache takes over.** A pitched voice may also write what it renders to a sample cache and later replay it
instead of stretching (`sample/ts_cached_loop`). When it leaves a stretched cache, because the pitch or ratio changed
(`sample/ts_cache_bend`) or because it reached what has been written (`sample/ts_cache_crossfade`), a fresh stretcher
crossfades out of the cache's next frames (up to 255 already written, copied to a small buffer) into its own new head.

**The fudge.** An unstretched audio clip that loops with a region before its start marker gets a stretcher for its last
few output frames before each loop end, only to crossfade into that region (`RS_AUDIO_STRETCH_NATIVE`: 100 such
crossfades). That stretcher never hops.

## 2. The components

Each component has one responsibility. The DSP components are pure: they see no `Sample`, cluster, voice or engine;
they take sample windows and typed state and give samples and hop decisions. Noise comes in as the synth's noise stream
(`Cong`, `src/dsp/fixed/noise/cong.h`), never from a global. They follow `docs/dev/dsp_component_architecture.md`:
plain value `State`, kernels where the arithmetic is per sample, `static_assert`ed concepts, and Q formats named on
every parameter. The live pitch shifter (`src/dsp/fixed/pitch_shift/hop_scheduler.h`) is the nearest precedent in
shape. Two of its parts are shared outright: its slope follower and percussiveness byte (decision 6) and its waveform
search's core (decision 9). The rest of its arithmetic differs (a ring history instead of a file), so the other
components are their own types.

The new files are named in plain text below; none exists yet. The interfaces are sketches: the small value types they
name without defining (`Block`, `Markers`, `PercFill`, `Reservation` and the like) are settled when the rewrite is
planned; the responsibilities, the ownership and the contracts are what this note fixes.

### 2.1 Hop tuning (DSP)

Home: src/dsp/fixed/timestretch/hop_tuning.h. Pure, constexpr tables, no state. The tables are the legacy
`getHopParams`' values, kept as they are: they are the behaviour (tuned by ear), and every hop depends on them.

```cpp
namespace deluge::dsp::fixed::timestretch {
using Ratio = UQ<24>;     // 2^24 = 1.0
using Increment = UQ<24>; // 2^24 = 1.0
enum class Direction : int8_t { kForward = 1, kReverse = -1 };

/// How long a hop may be and how it crossfades, at one ratio, before repitching.
struct HopShape {
	int32_t min_width;          ///< Output frames.
	int32_t max_width;          ///< Output frames.
	Q<31> crossfade_share;      ///< Of the hop; table values of 128 and over wrap negative (contract T3).
	int32_t crossfade_quarters; ///< Fixed part of the crossfade, in units of 4 frames.
	Q<23> jitter_share;         ///< The most the minimum width may move at random, as a share of itself.
};
[[nodiscard]] HopShape hop_shape(Ratio ratio);
}
```

### 2.2 Hop planner (DSP)

Home: src/dsp/fixed/timestretch/hop_planner.h. Decides where the next head starts and how long its hop and crossfade
are. Stateless: every input is an argument; the perc bytes come as a window.

```cpp
/// Percussiveness bytes in one direction, indexed by pixel (frame >> 7), readable from earliest to latest.
struct PercWindow {
	std::span<const uint8_t> bytes; ///< bytes[0] is pixel `origin`.
	int32_t origin;
	int32_t earliest; ///< Pixels, inclusive, in the direction's order.
	int32_t latest;
};

struct HopRequest {
	int64_t position_frames;  ///< The source position's whole frame (contract Q2).
	Ratio ratio;
	Increment increment;
	uint64_t combined;
	Direction direction;
	int32_t length_frames;    ///< The waveform's.
	HopShape shape;           ///< After the jitter (contract B1).
	std::optional<LoopAhead> loop; ///< The stretcher-made loop's facts, when that loop type is active.
};

struct HopPlan {
	enum class Kind : uint8_t { kPastEnd, kHead, kLoopCrossfade };
	Kind kind;
	int32_t start_frame;        ///< Before alignment.
	int32_t hop_frames;         ///< Output frames until the next hop.
	int32_t crossfade_frames;   ///< Output frames.
	CrossfadeRamp ramp;         ///< (2.4)
	bool align;                 ///< False for a loop crossfade into a cache (the cache fixes the phase).
	int32_t max_hop_frames;     ///< The ceiling a near loop end set, else INT32_MAX.
};

[[nodiscard]] HopShape jittered(HopShape shape, Cong& noise);              // one draw, contract B1
[[nodiscard]] HopPlan plan_hop(const HopRequest&, const PercWindow* perc); // contracts B2-B7
```

### 2.3 Waveform aligner (DSP)

Home: src/dsp/fixed/timestretch/waveform_aligner.h. Moves a planned start by whole frames and a sub-sample phase so the
new head's waveform lines up with the old head's at the crossfade's middle. It reads frames through a source concept,
so it never sees a cluster; the playback side supplies runs of resident frames. The pitch shifter's
`deluge::dsp::fixed::pitch_shift::WaveformAligner` runs the same search over a ring history, and the two share one
search core, src/dsp/fixed/timestretch/waveform_search.h, templated on the frame reader and parameterised by the flip
count, the first pass's direction and whether the search turns (decision 9). The frame source below lives in
src/dsp/fixed/sample_read/frame_source.h, shared with the perc analysis (2.6) and the read head (2.8).

```cpp
/// A run of readable frames from `frame` on in `direction`: each channel's sample is the 32-bit word whose top bytes
/// are the sample (unmasked: contract A2), `frames` long before a chunk or waveform boundary (R6).
struct FrameRun { const std::byte* first; int32_t frames; int32_t stride; };
template <class S>
concept FrameSource = requires(const S& s, int32_t frame, Direction d) {
	{ s.run(frame, d) } -> std::same_as<std::optional<FrameRun>>; // nullopt: not resident
	{ s.channels() } -> std::same_as<int32_t>;
	{ s.length_frames() } -> std::same_as<int32_t>;
};

using Averages = std::array<int32_t, 3>;
template <FrameSource S>
[[nodiscard]] std::optional<Averages> moving_averages(const S&, int32_t start_frame, int32_t crossfade_source_frames,
                                                      Direction, int32_t length_each);      // contract A2
struct Alignment { int32_t offset_frames; UQ<24> phase; };
template <FrameSource S>
[[nodiscard]] std::optional<Alignment> align(const S&, const AlignRequest&);              // contracts A1, A3-A6
```

### 2.4 Crossfader (DSP)

Home: `src/dsp/fixed/timestretch/crossfade.h`, extended. It already holds the window metrics `totalDifferenceAbs` and
`totalChange`. It gains the ramp and each segment's head gains.

```cpp
/// The crossfade's progress, Q24 of 1.0 (2^24 = done), and its step per output frame.
struct CrossfadeRamp {
	uint32_t progress = 1u << 24; ///< Done: no older head sounding.
	uint32_t increment = 0;
	[[nodiscard]] static CrossfadeRamp over_hop(int32_t frames);       // contract X1 (a)
	[[nodiscard]] static CrossfadeRamp over_loop(int32_t frames);      // (b)
	[[nodiscard]] static CrossfadeRamp over_fudge(int32_t frames);     // (c)
	[[nodiscard]] static CrossfadeRamp over_cache_tail(int32_t frames);// (d)
	[[nodiscard]] bool done() const { return progress >= (1u << 24); }
};
/// Each head's amplitude across one segment, and its step per frame.
struct HeadGains { Q<31> newer, newer_step, older, older_step; };
[[nodiscard]] HeadGains segment_gains(CrossfadeRamp&, int32_t frames, Q<31> level, Q<31> level_step); // X2
```

### 2.5 Cache tail (DSP)

Home: src/dsp/fixed/timestretch/cache_tail.h. The older head when a stretcher crossfades out of a cache: up to 255
frames copied from the cache, read once into the mix. It owns no memory: the engine lends it a buffer through the
`StretchContext` (section 5).

```cpp
class CacheTail {
public:
	/// Snapshot `frames` of `channels` words from 3-byte cache frames (contract C2) into `storage`.
	CacheTail(std::span<Q<31>> storage, const std::byte* cache_frame_origin, int32_t frames, int32_t channels);
	/// Add the next `frames` into `out` (interleaved by `out_channels`) at `level`, stepped by `level_step`. C4.
	void mix_into(Q<31>* out, int32_t frames, int32_t out_channels, Q<31> level, Q<31> level_step);
	[[nodiscard]] int32_t frames() const;
};
```

### 2.6 Perc analysis (DSP)

Home: src/dsp/fixed/timestretch/perc_analysis.h. The per-pixel analysis of a run of frames. The sample keeps the bytes
and its zones (which stretches of source are filled); this component only computes them. The per-frame slope follower
and the per-pixel byte are the pitch shifter's own, `SlopeFollower` and `percussiveness`
(src/dsp/fixed/pitch_shift/slope_follower.h), shared (decision 6): one follower and one byte exist, and the follower
rounds each pole's step where the legacy's floors, a difference accepted by name (P2). No layering rule between the
DSP library's directories forbids `timestretch/` including `pitch_shift/`, as the shifter already includes
`timestretch/crossfade.h`.

```cpp
/// The source's slope, followed: the shifter's follower, over folded frames (a quarter of each channel's word, summed
/// for stereo: `(left >> 2) + (right >> 2)`; `word >> 2` for mono).
using SlopeFollower = deluge::dsp::fixed::pitch_shift::SlopeFollower; // contract P2
/// The follower's state and the slope one frame back, which P3 compares against; kept per perc zone (P5).
struct PercState { SlopeFollower::State follower; Q<31> previous_slope; };
/// Analyse a run of frames from `first_frame`, writing `percussiveness` at each pixel's middle frame. Contract P3.
void analyse(const FrameRun&, int32_t first_frame, int32_t frames, Direction, PercState&, std::span<uint8_t> pixels,
             int32_t pixel_origin);
```

### 2.7 The playback-side stretcher

Home: src/synth/sources/sample/sample_stretcher.{h,cpp}, namespace `deluge::synth`. One per stretched voice-sample,
pooled. It owns the source position, the hop clock, the head phase, the perc-fill memo and every lease handle the
stretcher takes. It owns no read head (below). It drives the DSP components and reaches the sample only through the
stretch port (section 3). Engine-wide resources come in a `StretchContext` the engine passes to every call: the
`HopBudget`, the synth's noise stream and the cache-tail buffers.

**What the behaviour forces, and what is free.** Forced, because each is a point where the voice must be able to
cause an effect the goldens pin:
- three ways to begin: plain (S1: one noise draw, the first hop at 200 ± 32, the spare head a level copy, X3, R10);
  out of a cache tail (C1 to C3: a plain begin whose older head is the tail; a fresh stretcher is always begun just
  before it); a fudge (S2: a hop into the pre-margin at once, no draw, no later hop);
- one render per block that fills the perc analysis first (P4), then renders its segments (S3, X2 to X4, Q1);
- the voice moving the position when its own loop check wraps it (Q4), which hops at once if the newer head has
  stopped (S5);
- the queries the voice and the audio clip act on: whether stretching may end (E1), the position's whole frame (Q2),
  whether the newer head still reads, whether this stretcher is a fudge;
- ending, which lets go of every lease.

Free: the names and the grouping (a cache-tail begin is an opening of `begin`, not a call of its own); the state's
shape (the head phase below); where the position lives (here, not in the voice).

```cpp
class SampleStretcher {
public:
	struct Motion { Ratio ratio; Increment increment; Direction direction; };
	struct Plain {};
	struct FromCacheTail { CacheWindow window; int32_t channels; };
	struct Fudge { int32_t output_frames_to_loop; };
	using Opening = std::variant<Plain, FromCacheTail, Fudge>;

	bool begin(SampleStretchPort&, StretchContext&, int64_t position, Motion, LoopType, Opening);
	/// P4, then the block's segments. False: the note ended.
	bool render(SampleStretchPort&, StretchContext&, const Block&);
	/// The voice's loop check moved the position (Q4); hop now if the newer head has stopped (S5).
	void wrap_to(SampleStretchPort&, StretchContext&, int64_t position, const Block&);
	/// Let go of every reservation, perc chunk and tail buffer. Explicit: the object is pooled and reused in place.
	void end(SampleStretchPort&, StretchContext&);
	[[nodiscard]] bool may_end(const SampleStretchPort&) const; // E1
	[[nodiscard]] int32_t frame() const;                         // Q2
	[[nodiscard]] bool newer_reading() const;
	[[nodiscard]] bool is_fudge() const;

private:
	SourcePosition position_; ///< Q24 frames, int64 (Q1, Q2).
	HopClock clock_;          ///< Output frames to the next hop, and how often the budget passed it over (S3).
	HeadPhase phase_;
	PercFillMemo perc_fill_;  ///< Where the last fill started and ended, and its direction (P4).
	Leases leases_;           ///< The three reservations and the two perc chunks (section 5).
};
```

**The head phase.** Each alternative holds only what its phase needs, so the states the legacy's two flags and a
progress word allow but cannot mean (an older head marked reading with its ramp done outside a plain begin's first
segment, a ramp running with no older head, a cache tail and a reader both older) cannot be written:

```cpp
/// The older head: the voice-sample's spare head, or a cache tail.
using Older = std::variant<SpareHead, CacheTail>;
struct Shadowed {};                                     ///< A plain begin before its first segment ends (X3).
struct Solo {};                                         ///< The newer head reads alone.
struct Crossfading { CrossfadeRamp ramp; Older older; }; ///< Both read; the older fades out.
struct Fading { CrossfadeRamp ramp; Older older; };     ///< The newer has stopped; the older fades out alone.
struct Silent {};                                       ///< Neither reads.
using HeadPhase = std::variant<Shadowed, Solo, Crossfading, Fading, Silent>;
```

- A plain begin enters `Shadowed`; a fudge, `Crossfading` over the spare; a cache-tail begin, `Crossfading` over the
  tail, or `Shadowed` when C1 finds nothing to copy.
- `Shadowed` leaves for `Solo` after its first segment (or `Silent` if the newer stops in it).
- A hop from `Solo` or `Crossfading` (S3 calls it; the newer is reading) leaves for `Crossfading` over the previous
  newer when the new head starts, `Solo` when it cannot (S4: the newer reads on, the spare stops), and `Fading` when
  it would start past the end (B6).
- A hop from `Fading` or `Silent` (only S5 calls it: the newer has stopped) drops the fading older head at once, and
  the previous newer, which becomes the spare, has stopped too. So it leaves for `Solo` when the new head starts, at
  full level with no crossfade and no alignment (A1 runs only with an older head reading), and for `Silent` when it
  cannot start or would start past the end. This is `hopEnd` giving the older head the newer's activity before it
  starts the new one, as `reInit` calls it.
- `Shadowed` never hops: it lasts one segment, and the first hop comes at least 168 frames in (S1).
- `Crossfading` leaves for `Solo` when the ramp is done or the older stops, and for `Fading` when the newer stops.
  `Fading` leaves for `Silent` when the ramp is done or the older stops; `Solo` for `Silent` when the newer stops.
- In `Silent` the render ends the note unless a cache is being written or the voice loops, whose next wrap hops (S5).

**Who owns the read heads.** Each head is a `ReadHead` state (2.8, in the DSP library) paired with an app-side
`SampleReadLease` that pins the chunk its cursor reads. The two move together: a seat lets the lease go before it moves
the cursor (H1), and a level copy takes a lease of its own on the shared chunk. May a head live inside this
`deluge::synth` object? No. Unstretched playback reads the voice-sample's head from the voice's render, so the head the
voice plays from cannot be the stretcher's; and the lease is an app type, the storage layer's, which the synth tree
does not name (`docs/dev/synth_source_ports.md`), so a head split between a synth-side state and an app-side lease would
make every seat and copy cross the seam twice. So:
- the voice-sample (app) owns two heads, state and lease each: the one it plays from and a spare;
- the stretcher owns which role each plays, through its head phase, and how loud each is;
- the port seats, reads and marks the heads by role. A hop seats the spare at the new start (H1) and only once it
  has started swaps the roles, so a start that fails leaves the newer untouched and S4 needs no undo;
- when stretching ends, the voice-sample closes its spare's lease itself, with its cursor and the chunks it pinned.

This is the one statement of who owns what; section 5's table follows it.

### 2.8 Read head (DSP)

Home: src/dsp/fixed/sample_read/read_head.h, namespace `deluge::dsp::fixed::sample_read`, beside the frame source
(2.3). One head's cursor over the source, its sub-sample phase and its interpolation window, which is the DSP library's
existing `Interpolator` (src/dsp/fixed/interpolation/interpolator.h), fed as the legacy reader feeds it. Unstretched
playback and both stretcher heads read through it. It replaces `SampleLowLevelReader`'s reading arithmetic: the
window check (`considerUpcomingWindow`), the chunk walk, and the native and resampled reads (contracts R1 to R11, and
A0's play byte).

It holds no lease and no pointer: its cursor is a frame position, and it reads frames through a `ReadSource`, which
adds to the `FrameSource` the chunk walk's one effect (moving the lease to the chunk holding a frame) and the current
chunk's boundary and floor (R6). The app side implements it: `SampleReadLease` (src/deluge/model/sample/), which holds
the region port's cursor (`DelugeSampleSource`), the current region and the head's one independent lease on it, and
answers "not ready" where the chunk is not resident. A head past its end has no frame, only a count of the zero taps it
has fed, so the legacy reader's null pointer reused as a counter becomes its own state.

```cpp
namespace deluge::dsp::fixed::sample_read {
enum class Residency : uint8_t { READY, LOADING, UNAVAILABLE };
/// The chunk the lease holds, in frames: where the walk next acts, and how far back the window may read (R6).
struct ChunkSpan { int32_t boundary; int32_t floor; bool final; };
template <class S>
concept read_source = frame_source<S> && requires(S& s, const S& cs, int32_t frame, Direction d) {
	{ s.move_to(frame, d) } -> std::same_as<Residency>;       // the lease moves to `frame`'s chunk (R6, R10)
	{ s.let_go() } -> std::same_as<void>;                     // the chunk goes
	{ cs.span(d) } -> std::same_as<std::optional<ChunkSpan>>; // nullopt: no chunk held
	{ cs.byte_depth() } -> std::same_as<int32_t>;
};

struct ReadHead {
	struct Reading { int32_t frame; };       ///< The next frame the window takes.
	struct Ended { int32_t zero_taps; };     ///< Past the end, or holding no chunk: zero taps fed since (R1, R3, R4).
	struct State {
		std::variant<Reading, Ended> cursor;
		UQ<24> phase;                          ///< The sub-sample phase.
		int8_t width_last;                     ///< The window's width at the last resampled check; 0 after a native one.
		Interpolator window;
	};
	/// Where the head loops, from the playback guide each call (markers obeyed or not, as the owner says).
	struct ReadBounds { std::optional<int32_t> loop_start_frame; bool endless_silence; };
	/// What a window check allows: frames to render, or that the head stopped (its end, or a chunk not ready: R11).
	struct Window { enum class Kind : uint8_t { READ, STOPPED } kind; int32_t frames; };
	/// Where a read mixes: the output, its channels, and the amplitude stepped per frame.
	struct Output { std::span<Q<31>> out; int8_t out_channels; Q<31> amplitude; Q<31> step; };

	template <read_source S>
	static Residency seat(State&, S&, int32_t frame, UQ<24> phase, Direction);              // R10
	template <read_source S>
	static Residency place(State&, S&, int32_t frame, Direction);                           // a late start's seat
	[[nodiscard]] static State level_copy(const State&);                                    // R10
	template <read_source S>
	static bool walk(State&, S&, const ReadBounds&, Direction);                             // R6
	template <read_source S>
	static Window check(State&, S&, const ReadBounds&, int32_t frames, Increment, int32_t width,
	                    Direction);                                                         // R1 to R6
	template <read_source S>
	static void read_native(State&, const S&, Output&, int32_t frames, Direction);          // R7
	template <read_source S>
	static void read_resampled(State&, const S&, Output&, int32_t frames, Increment, int32_t width, int32_t kernel,
	                           std::byte*& cache_write, Direction);                         // R8, R9
	template <read_source S>
	static bool read(State&, S&, const ReadBounds&, Output&, int32_t frames, Increment, int32_t width,
	                 int32_t kernel, std::byte* cache_write, Direction);                    // R11: check, then read
	[[nodiscard]] static int32_t play_byte(const State&, const ReadFacts&, Direction, bool compensate); // A0
};
}
```

The read loop (R11) is the owner's: the voice's unstretched render and the port's `read` each run check-then-read until
their frames are rendered. The legacy reader's other users switch with it: `VoiceSample`, which derives from
`SampleLowLevelReader` today and instead holds two heads; `SamplePlaybackAdapter`'s unison clone, which is a level copy
that opens its own cursor and so walks on past its next chunk (decision 13), unlike the shadow copy; and the DSP bench's
reader cases (`src/deluge/bench/pitch_shift_cases.cpp`,
`src/deluge/bench/cases.h`).

## 3. The seam to the sample

**Why a port of its own.** Two kinds of voice stretch: a synth voice plays its sample through `SamplePlaybackPort`
(`src/synth/sources/sample/sample_playback_port.h`), implemented by `SamplePlaybackAdapter`
(`src/deluge/model/voice/sample_playback_adapter.h`); an audio clip renders its `VoiceSample` directly, with no port.
Both reach the stretcher from `VoiceSample::render`. So the stretcher's sample access cannot live on
`SamplePlaybackPort` alone. It gets a port of its own, `SampleStretchPort`
(src/synth/sources/sample/sample_stretch_port.h), implemented once app-side over a voice-sample, its `Sample`, its two
read heads and its playback guide, and reached by both kinds of voice. `SamplePlaybackPort` keeps its render signature
and gains nothing. This is decision 1.

```cpp
class SampleStretchPort {
public:
	enum class Head : uint8_t { kNewer, kOlder };
	// The source.
	[[nodiscard]] virtual SourceFacts facts() const = 0;                 // layout and markers, in frames
	[[nodiscard]] virtual std::optional<int64_t> synced_position() const = 0;
	[[nodiscard]] virtual std::optional<FrameRun> run(int32_t frame, Direction) const = 0;
	// The perc analysis.
	[[nodiscard]] virtual std::optional<PercWindow> perc_at(int32_t pixel, Direction) = 0;
	virtual PercFill fill_perc(int32_t from, int32_t to, Direction, int32_t max_frames) = 0;
	// The heads, by role.
	virtual void shadow() = 0;                                           // the spare becomes a level copy (H1, R10)
	virtual SeatResult seat_and_swap(int32_t frame, UQ<24> phase, LoopType) = 0; // H1; swaps only on success
	virtual HeadRead read(Head, Q<31>* out, int32_t frames, HeadGain) = 0; // R11's stretcher read
	[[nodiscard]] virtual HeadMark mark(Head) const = 0;                 // A0's play byte, and the phase (A5)
	[[nodiscard]] virtual bool cursors_meet() const = 0;                 // E1
	// Residency the stretcher holds, by handle.
	virtual void keep(Reservation&, int32_t frame, Direction) = 0;
	virtual void let_go(Reservation&) = 0;
	virtual void hold(PercChunk) = 0;
	virtual void drop(PercChunk) = 0;
};
```

**Its size.** Fourteen operations, each one forced by a behaviour:

| Operations | Needed by |
|---|---|
| `facts`, `synced_position` | every hop's position (S6), the loop crossfade (B7), the end (B6), the clamps (B4, B8) |
| `run` | the aligner's frames (A2, A4) and the start clamp's residency check (B8) |
| `perc_at`, `fill_perc` | the beam search (B3) and the fill (P4); the bytes and zones are the sample's, shared by its voices |
| `shadow`, `seat_and_swap`, `read`, `mark`, `cursors_meet` | the plain begin (X3), every hop and fudge (H1, S4), the segments (X2, read by R7 to R9), the aligner's reference (A0, A5), E1 |
| `keep`, `let_go`, `hold`, `drop` | the hop-back, hop-retry and lookahead reservations and the two perc chunks (section 5) |

Merging further (the facts with the sync, the two lease pairs into one) would put two behaviours behind one call. The
port is smaller than `SamplePlaybackPort`, which has 23.

**The five users.**
- `src/deluge/model/voice/voice_sample.cpp`: `VoiceSample` holds a `SampleStretcher*` from the pool and the spare
  head, and builds the port over itself, its sample and its guide on the stack for each call. Its render keeps the
  choice between reading a cache, writing one, and playing plain or stretched, the unsynced loop check (Q4, which calls
  `wrap_to`) and the cache-write copy (W1); the stretched segment loop (hops, gains, both heads, the position) is
  `SampleStretcher::render`. `fudgeTimeStretchingToAvoidClick` and `weShouldBeTimeStretchingNow` become `begin` (with a
  `Fudge` or `Plain` opening, and a `FromCacheTail` one where the render leaves a stretched cache); `endTimeStretching`
  becomes `end` and closes the spare's lease; `sampleZoneChanged` and `getPlaySample` read `frame()`.
- `src/deluge/model/sample/sample.cpp`: `Sample::fillPercCache` takes no stretcher. It returns a `PercFill`: where the
  filled zone now ends, the perc chunks the fill touched (for the stretcher to hold), and whether the lookahead
  reservation may be let go, with the frame it should move to. Its per-frame arithmetic calls the perc analysis
  (2.6). `Sample::prepareToReadPercCache` becomes the port's `perc_at`, returning a `PercWindow` with an origin in
  place of a pointer offset back from a chunk's payload. `Sample::getAveragesForCrossfade` goes: the aligner's
  `moving_averages` reads the same frames through `run`.
- `src/deluge/model/sample/sample_low_level_reader.cpp`: replaced by the read head (2.8, decision 8). In step 7 it
  takes no stretcher: `readSamplesNative` and `readSamplesResampled` drop the parameter they only passed on, and
  `readSamplesForTimeStretching` returns whether the head still reads instead of writing a flag into the stretcher (its
  card-error exit is never taken: R11). In step 7a its arithmetic becomes the read head's (R1 to R11), its residency the
  app's `SampleReadLease`, its loop for the stretcher the port's `read`, and it is deleted with its other users
  switched (2.8).
- `src/deluge/model/clip/audio_clip.cpp`: reads `newer_reading()` and the voice-sample's own `fudging` where it read
  the legacy flags (the restart at the loop, the fudge, the cull immunity), and begins the fudge through the
  voice-sample. `fudging` is the voice's, as the legacy's was: a fudge with no frames to the loop begins its stretcher
  plain (`is_fudge()` false) while the voice still counts as fudging.
- `src/deluge/processing/engines/audio_engine.cpp`: the pool's type, and the `StretchContext` (a `HopBudget` in place
  of the two globals, the noise stream, the tail buffers); the render's culling bypass reads the budget.

## 4. Numeric contracts

The 55 contracts are in [time_stretcher_contracts.md](time_stretcher_contracts.md), one table per component, each as
its inputs, its exact arithmetic in order, and the computation it must equal: hop tuning 3 (T1 to T3), scheduler 6 (S1
to S6), planner 8 (B1 to B8), aligner 7 (A0 to A6), crossfader 4 (X1 to X4), cache tail 4 (C1 to C4), perc analysis 5
(P1 to P5), heads 1 (H1), position, loop, exit and cache write 6 (Q1 to Q4, E1, W1), read head 11 (R1 to R11). Each
equals the legacy computation but P2, which equals the pitch shifter's `SlopeFollower` (decision 6). The noise stream's
draw count is a contract too: one at a plain begin, one per hop attempt. The stretcher's seven named quirks are kept;
the goldens pin five, and A2's and X1's are kept but not golden-pinned. The read head's five (R1, R2, R8 and R9's
16-bit taps, R8's mono halving, R10's level copy that cannot move to the next chunk) are kept, and step 1's records say
which goldens reach each.

## 5. Lifetimes, threading and real-time limits

### Who owns what

Following section 2.7: the voice-sample owns the read heads and their leases, the stretcher owns the roles and every
lease handle it takes, the engine owns the tail buffers.

| Resource | Owner | Taken | Given back |
|---|---|---|---|
| The head the voice plays from (the newer head): its `ReadHead` state and its `SampleReadLease` | the voice-sample | a plain begin takes nothing (the head plays on); a hop or fudge seats the spare (H1), which becomes the newer | becomes the older at the next successful hop |
| The spare head (the older head) | the voice-sample | a plain begin seats it as a level copy with its own lease on the shared chunk (`shadow`, R10); a successful hop or fudge makes the previous newer the spare | reseated at the next hop (its lease goes first, H1); closed by the voice-sample when stretching ends, with its cursor |
| A head's chunk lease, and the region port's prefetch of the next chunk | the head's `SampleReadLease` | a seat (R10), a chunk walk's move (R6), a level copy (R10) | the next seat or move, a walk that ends, or the voice-sample's unassign |
| Hop-back reservation | the stretcher (a handle; the port keeps it) | at a begin and after every hop, from the earliest frame the next hop could start (below) | `end` |
| Hop-retry reservation | the stretcher | a hop whose start chunk is not resident | that hop's retry succeeding, or `end` |
| Perc lookahead reservation | the stretcher | each perc fill, moved to where it stopped reading | when the filled zone already covers the lookahead, or `end` |
| Perc-cache chunk leases | the stretcher, the two most recently used | a fill or a read touching a chunk not held (the older of the two is dropped) | `end` |
| A cache-tail buffer | the engine's tail buffers, lent to the stretcher through the `StretchContext` | a `FromCacheTail` begin | the older head stopping (the crossfade's end), the next hop, or `end` |
| The stretcher | the voice-sample, from the engine's pool | the first stretched block, or a fudge | `end`, then back to the pool |

The hop-back reach: `widest = max(max_width, min_width << 1)`, repitched as B2; `back = ((rate / 45) >> 1) + 1`, plus
`(int64(widest) × (2^24 − ratio)) >> 25` when slowed down; the reservation starts at `clamp(position − back × dir, 0,
length − 1)`. It decides only residency, not samples: the goldens' card answers at once.

The DSP components hold nothing: no lease, no pointer kept past a call. A `ReadHead`'s state is a frame position, its
phase and its window, never a pointer into a chunk. A `PercWindow` and a `FrameRun` are valid only for the call they
are passed to.

### Tiers

- **Audio tier (the render).** Every call that reads or writes samples: `begin`, `render` (hops included) and
  `wrap_to`, and every read head's check and read, stretched or not. Taking a stretcher from the pool happens here.
- **The render's sequencer tick, or a playhead move on the interaction tier.** `AudioClip::processCurrentPos` and
  `AudioClip::resumePlayback` read `newer_reading()` and the voice-sample's `fudging` and may end stretching (`end`). On the
  interaction tier they run inside a move's section (`docs/dev/playback_moves.md`): the render is held off and nothing
  may allocate. `end` allocates nothing; returning a tail buffer or the stretcher pushes onto a reserved free list.
- **Voice unassignment** calls `end` from wherever the voice-sample is unassigned today, under the same exclusion.

### No allocation on the render

The render-allocation count (`RP-ALLOC`, printed by `tests/integration/scenarios/render_preset_scenario.cpp` and not
gated) for every stretcher case, measured by running each golden-synth case's render directly on the current
`golden_vt_render` and reading its `RP-ALLOC` line:

| Allocations | Cases |
|---|---|
| 2 | 17 cases at the root, which have no sample cache: `ts_long_hold`, `ts_loop_fast`, `ts_marker_loop_end`, `_ahead`, `_released`, `ts_marker_loop_start`, `ts_marker_start`, `ts_repeat_cut`, `_loop`, `_once`, `ts_reverse_loop`, `ts_speed_fast`, `_slow`, `ts_zone_end_ahead`, `_loop`, `_once`, `_released` |
| 4 | 28 cases with a sample cache: `timestretch`, `ts_cache_bend_early`, `ts_cache_replay`, `ts_cached_loop` (and `stereo/ts_cached_loop`), `ts_marker_loop_end_cached`, `_ahead`, `ts_marker_start_cached`, `ts_markers`, `ts_mono`, `ts_perc`, `ts_pitch_cents`, `_down12`, `_down36`, `_fifth`, `_up36`, `ts_reversed`, `ts_short`, `ts_tonal`, `ts_zone_end_cached`, `_ahead`, `_once`; and the 6 that crossfade out of a cache, 5 before decision 2: `ts_cache_bend`, `ts_cache_bend_root`, `ts_cache_crossfade`, `_mono`, `ts_cache_linear`, `ts_cache_stretch_amount` |
| 9 | `ts_poly` (four voices, four caches) |

Where they come from, traced by a debugger watchpoint on the counter in four cases (`ts_long_hold`, `ts_pitch_fifth`,
`ts_cache_crossfade`, `ts_poly`):
- **2, the perc cache's first fill** in `Sample::fillPercCache`: its byte block (`alloc_sdram`) and its zone list's
  growth. The sample's, not the stretcher's; it moves with the fill unchanged.
- **The sample cache** (`Sample::getOrCreateCache`): one for each cache's memory, plus each growth of the sample's
  cache list (once for the first cache; three times for `ts_poly`'s four, so 7 there). Not the stretcher's.
- **1, the cache tail's buffer**, the legacy stretcher's only render allocation (`allocMaxSpeed` of 256 words per
  channel, 1 KB mono or 2 KB stereo), in each crossfade out of a cache. Decision 2 removed it: the engine's eight tail
  buffers are made at start, and the six cases that crossfade out of a cache count 4, the others unchanged.
- In the four traced cases the pool (48 stretchers) never fell back to the heap. The other cases' counts follow the
  same pattern (2 for the perc cache, 2 for one sample cache); they were counted, not traced.

These counts are the ceiling: no case may rise. The render-song gates print no allocation count, so the songs have no
ceiling yet (section 6, step 1).

### On the device

Measured on the device images of the rewrite's last code commit and of its branch point (`tools/bench_image.py
--no-bench`, the DSP bench off), with `arm-none-eabi-nm -S -C` and the section headers.
- **Placement.** Every function of the stretcher set and the read-head set below is in `.text`, and their tables and the
  port's vtable in `.rodata`: on-chip SRAM, none in the GUI archive's SDRAM code or a size-optimised source. As before,
  what keeps them there is their homes, the DSP library and `deluge_synth`, not `[[gnu::hot]]`
  (`src/deluge/CMakeLists.txt`).
- **The stretcher set** (the stretcher's and its port's functions, the hop planner's and aligner's out-of-line code, the
  hop tables, the users' stretcher functions, and `VoiceSample::render`): 15,808 bytes at the branch point, 19,762
  after, +3,954. The largest are `SampleStretcher::hop` 3,800 (the legacy `hopEnd` 4,444), `read_heads` 1,220,
  `plan_hop` 1,184, `begin` 1,072, `moving_averages` 640, and `VoiceSample::render` 4,286 (4,352).
- **The read-head set** (the reader's functions, against the lease's and the read head's): 6,546 bytes before, 8,384
  after, +1,838. Most of it is the read kernels instantiated per channel layout: linear 282 to 394 bytes each, sinc 890
  (mono) and 1,124 (stereo), native 184 to 252. They read faster than the legacy's on the device: `bench pitch_shift`
  against the legacy, sinc −1% to −11% and linear −5% to −16% at 32 to 128 frames.
- **Both sets are over the budget of a few hundred bytes** (+5,792 together). The image's `.text` grew 5,968 bytes and
  its `.rodata` shrank 864.
- **Objects.** `SampleStretcher` is 120 bytes (the legacy object 208), `ReadHead::State` 80, `SampleReadLease` 48 (the
  legacy reader 128), the voice-sample 304 (160). At the default pools of 48 of each, the voice-samples take +6,912
  bytes and the stretchers −4,224, a net +2,688 from the heap. The engine's eight tail buffers are 16 KB of `.bss`
  (decision 2): `.bss` grew by exactly that, 16,384 bytes.
- **Headroom.** The SRAM heap, `__sram_heap_end − __sram_heap_start`, is 594,288 bytes, 21,472 less than the branch
  point's 615,760: the tail buffers and the code growth.

### The read's NEON data plane

The resampled sinc read and the native read run a staged data plane (`src/dsp/fixed/sample_read/stage.h`,
`gather.h`, `quad_mix.h`, `native_quads.h`).
The sinc read decodes its run's frames into planar int16 staging, newest first, after the window's sixteen taps, reads
each output's taps straight from staging four outputs at a time, and mixes four frames at a time; the native read masks
and mixes four frames at a time straight from the run. The control plane (the walk, the window check, the leases) and
`ReadHead::State` are unchanged, and the output is bit-identical (`read_head_oracle_spec` holds the scalar kernels as
the oracle). The linear read stays scalar: staged, it measured slower, its two taps too little work for the decode and
the in-order NEON pipeline's latencies.

Measured on the device (`bench pitch_shift`, cycles per output frame at a block of 128, before → after):
- **Sinc:** 16-bit stereo 171.4 → 70.1, 32-bit stereo 171.4 → 72.3, 24-bit stereo 172.9 → 71.9, 16-bit mono 141.1 →
  47.3, reversed 177.1 → 69.9, a rate of 4 173.4 → 78.8, stereo to one channel 172.4 → 69.2.
- **Native:** 16-bit stereo 25.8 → 15.3, 24-bit stereo 25.8 → 17.7, 16-bit mono 12.8 → 10.1, stereo to one channel 20.9
  → 12.4. Linear is unchanged (18.9 mono, 33.0 stereo).
- **A stretched voice** (`stretch_voice_*`: the stretcher and both heads over a resident region): pitched 269.8 → 120.7;
  unpitched 32.9 → 32.8 at 128 frames, but 59.7 → 64.3 at 32 and 35.9 → 37.1 at 64, with fewer instructions and more
  stalled cycles than before.
- **Size:** the read-head set (the lease's and the read head's functions, the data plane's included) 8,904 → 29,712
  bytes, most of it the native read's per-depth, per-layout passes and `decode_taps` (3,048); `.text` +20,560, `.bss`
  +4,096 (the engine's staging, `AudioEngine::readStaging`). Every function is in `.text`, on-chip. The SRAM heap is
  569,488 bytes, 24,640 less.
- **Emulator:** delugemu runs under an instruction-counted clock, so fewer instructions per audio block move every later
  event: against the branch point every leg's audio matches after a constant boot shift but 35 sample-playing legs, whose
  presses land on other blocks, and their pad and OLED captures differ; the image against itself matches throughout,
  and x86 parity over the same scripts (no clock) shows no difference.

## 6. The rewrite's steps

One step per component, each with its contracts' unit specs written and failing before the component. Each step is
bit-identical on the goldens before it lands (golden-synth on x86, and golden-synth-arm filtered to the stretcher
cases; step 7a's set is wider), except step 4, which re-records the goldens the shared slope follower changes
(decision 6); every later step is bit-identical to the baseline step 4 leaves.
1. **Records.** For every contract, workloads and their results, recorded by a temporary probe of the legacy code
  and committed as a records header under `tests/unit/dsp/` (decision 4). P2's records are the shared follower's, not
  the legacy's (decision 6): the probe logs P2's inputs, the frame words, from the legacy fill, and the records hold
  the pitch shifter's `SlopeFollower` results on them. Every other contract's records are the legacy's; P3 and B3 take
  the legacy's slopes and bytes as recorded inputs, so their specs hold under the shared follower too. The render-song
  scenario reports its render-allocation count, so the songs get a ceiling. The probe's workloads:
  - **the goldens:** every `sample/ts_*` and `sample/timestretch` case and the four `RS_AUDIO_STRETCH*` songs, each
    contract's inputs and results logged at every call (the sides they take);
  - **the read head's** (R1 to R11, A0): the reader's computations logged at every call over all of golden-synth's
    sample cases and the render-song audio gates, unstretched playback included, since it reads through the head too;
  - **generated inputs** for the pure arithmetic: hop tuning over the whole ratio range, the planner, crossfader and
    cache-tail arithmetic over values the goldens leave out;
  - **fault injection:** the same golden renders with the probe answering "not ready" or "not resident", or failing an
    allocation, at a chosen call, and logging what the legacy does next;
  - **a sweep** for the one unbuilt side with sample output (C1's chunk-end limit, below).

  The coverage run, before the read head lands, also measures the reader's own functions, which the script's `SCOPE`
  leaves out today because they only pass the stretcher along: `considerUpcomingWindow`, the chunk walk
  (`changeClusterIfNecessary`, `moveOnToNextCluster`, `setupReassessmentLocation`, `reassessReassessmentLocation`), the
  fills and the step back (`fillInterpolationBuffer*`, `jumpBackSamples`), the seat (`setupClustersForPlayFromByte`,
  `assignClusters`), `getPlayByteLowLevel`, and the reads (`readSamplesNative`, `readSamplesResampled`,
  `readSamplesLinear`, `jumpForwardLinear`, `jumpForwardZeroes`). Their untaken sides join
  [time_stretcher_coverage.md](time_stretcher_coverage.md), keyed and categorised as the stretcher's are.

  The sides only a card that has not answered, a failed allocation or an unbuilt source reaches, and how each one's
  values are produced:

  | Contract or reservation | Side | Produced by |
  |---|---|---|
  | S4, and section 5's hop-retry reservation | a hop's new head cannot start; the retry | fault injection at the seat (`setupNewPlayHead`) at chosen hops: the state S4 leaves, the reservation, and the retry's outcome 500 frames on |
  | H1 | its three failure exits: the chunk setup not ready, the chunk move failing, a ready setup holding no region | fault injection at each, for a hop and for a fudge's begin (whose failure unassigns the voice) |
  | B8 | a start before the start marker whose frame is not resident moves to the marker (`hopEnd`'s start clamp) | fault injection at the clamp's residency check, before and after alignment |
  | A4 (and A5's partial phase, decision 7) | a frame not resident abandons the search | fault injection at the aligner's frame read, at a chosen step of a chosen pass |
  | X3, Q4's reads, R11 | a stretcher head's chunk not ready mid-read: zero taps, then a stopped head (the voice is not cut: R11) | fault injection at the walk's acquire (`moveOnToNextCluster`) during a head's read, newer and older |
  | R10, X3, E1 | a level copy's walk reaching its first chunk boundary, which it never passes (R10's quirk) | no injection: the walk fails whatever the card does, so the probe logs it wherever a plain begin's first segment crosses a chunk; whether a golden-synth case does is the record's to say (render-song's gates fail the underrun it counts) |
  | R3, R6, R10 | the next chunk not ready mid-walk; a low-level loop's restart not ready; a seat loading or unavailable | fault injection at the walk's acquire (`moveOnToNextCluster`) and the seat's (`assignClusters`), stretched and unstretched |
  | C1 | the chunk-end limit: the read position in a cache chunk's last frame at the switch | a sweep: `sample/ts_cache_crossfade` rendered with its note length stepped a frame at a time until the switch lands there (about 1 in 5,400) |
  | P1, P5, and section 5's perc-chunk leases | a third distinct perc-cache chunk, the older of the two held dropped (`rememberPercCacheCluster`) | accepted as unpinned: it decides which chunks stay leased, never a sample; specced from reading against a fake port |
  | `AudioClip::getCullImmunity` and the voice cull (CULL) | a voice culled under CPU load | fault injection: the probe raises the direness at a chosen block, logging each voice's cull immunity and which voice the legacy culls |
  | Section 5's hop-back and lookahead reservations | where each is kept and let go | logged from the goldens, which run them; they decide residency only |
  | The allocations (the stretcher pool, the tail buffer, the perc store, a cache) and memory pressure | an allocation fails, or a cache or perc-cache chunk is stolen | fault injection in the probe's allocator, or an eviction between calls; the tail buffer's case is decision 2's: with all eight lent, no crossfade |

  A slow-card render (render-song's `RENDER_FILL_BURST` over the stretch songs) reaches some of these without injection,
  as streaming-margin reached the cache-resume sides; its gate fails any underrun, so it cross-checks the injected
  records rather than producing them.
2. **Hop tuning** (2.1; T1 to T3).
3. **Crossfader and cache tail** (2.4, 2.5; X1, X2, C1 to C4).
4. **Perc analysis** (2.6; P1 to P3, P5) over the pitch shifter's `SlopeFollower` and `percussiveness` (decision 6),
  and `Sample::fillPercCache` computes through it and returns a `PercFill` instead of taking the stretcher. The
  rounded follower can change the perc bytes, and with them where hops land: this step re-records, in the same commit,
  every golden it changes, and names them there: the `sample/ts_*`, `sample/timestretch` and `RS_AUDIO_STRETCH*` cases
  whose output changes. Every other step is bit-identical to that new baseline.
5. **Hop planner** (2.2; B1 to B8).
6. **Waveform aligner** (2.3; A1 to A6; A0 is the read head's, specced in step 7a) over the shared search core
  (decision 9), which the pitch shifter's `WaveformAligner` moves onto in the same step, its goldens and unit specs
  checking its side bit for bit.
7. **The playback side.** `SampleStretchPort` and its app adapter (H1, and A0 over the legacy reader until step 7a),
  the `StretchContext` with `HopBudget` and the tail buffers, and `SampleStretcher` with its head phase (S1 to S6, X3,
  X4, P4, Q1 to Q4, E1, W1); `VoiceSample` gains its spare reader, and it, `AudioClip` and the engine switch over;
  `SampleLowLevelReader` stops taking the stretcher; `SampleSource::stretch_to_undo_pitch`
  (`src/synth/sources/sample/sample_source.cpp`) saturates the ratio instead of wrapping it (decision 3). The legacy
  class is deleted in this step, with `src/deluge/dsp_legacy/`, its target and its `scripts/layers.toml` rows.

  7a. **The read head** (2.8; R1 to R11, and A0's play byte), here by decision 12 (a). The `ReadHead` in the DSP library
  and its app-side `SampleReadLease`; `VoiceSample` holds two heads and stops deriving from `SampleLowLevelReader`; its
  unstretched read loop, the port adapter's head operations (`shadow`, `seat_and_swap`, `read`, `mark`,
  `cursors_meet`), `SamplePlaybackAdapter`'s unison clone and the DSP bench's reader cases switch to it, the bench's
  cases compared on the rig against the reader's; `SampleLowLevelReader` is deleted. The stretcher's code does not
  change. Unstretched sample playback reads through the head, so this step's gate is wider than the stretcher's:
  unstretched playback stays bit-identical on golden-synth's sample cases (x86, and golden-synth-arm) and on the
  render-song audio gates (every gate whose output `tests/golden/render/songs.json` records), as well as on the
  stretcher cases, but for the unison clone's walk past its next chunk (decision 13), whose changed goldens, if any,
  this step re-records. Its records and its coverage were taken in step 1.
8. **The gate.** Every golden bit for bit on x86 and ARM against the baseline step 4 left, unstretched sample playback
  included: golden-synth's sample cases and the render-song audio gates (step 7a's); no case's `RP-ALLOC` rises; x86
  parity 0 differences; the nightly with both corpora; an emulator sweep against `main` with no unexplained difference;
  the device placement and sizes above; and the coverage script re-run over the new files, the read head's and its
  lease's included (as step 1 measured the reader's own functions), its untaken sides written into
  [time_stretcher_coverage.md](time_stretcher_coverage.md) in place of the legacy's, keyed by function and condition,
  each with a category and, where the category does not give it, a reason; and no RES, MEM, CULL or reachable side
  without the record or the acceptance step 1 gave its contract.

## 7. Decisions

Each is a decision, its options kept as the rationale; the chosen one is marked. Decision 12 follows from decision 8.

1. **Where the stretcher reaches the sample.** Audio clips render their voice-sample with no `SamplePlaybackPort`.
   - (a) **Decided:** `SampleStretchPort`, a port of its own (14 operations, section 3), implemented once app-side
     over a voice-sample; synth voices and audio clips both reach it through `VoiceSample::render`.
     `SamplePlaybackPort` is unchanged.
   - (b) `SamplePlaybackPort` gains the stretch operations, and audio clips first move onto a `SamplePlaybackAdapter`
     (a larger change, outside the stretcher).
   - (c) Both: the stretch operations on `SamplePlaybackPort` for synth voices, and a second adapter for clips.
2. **The cache tail's buffer, the stretcher's one render allocation.**
   - (a) **Decided:** a fixed set of tail buffers the engine makes when it starts (eight stereo buffers, 16 KB),
     lent through the `StretchContext`, an engine resource like the `HopBudget`, not through the per-voice port. With
     none free there is no crossfade, as the legacy does when its allocation fails. `RP-ALLOC` falls by one in the six
     crossfade cases.
   - (b) Embed the 2 KB buffer in every stretcher: the pool grows by about 96 KB of fast memory.
   - (c) Keep allocating on the render; the ceiling still holds.
3. **The stretch ratio far below the root.** The rewrite changes this range; the decision is how. No golden pins any of
   it. Three things go wrong there, from about 7 octaves below the root down:
   - **7 to 8 octaves down.** The ratio (`2^48 / increment` with no other pitch) passes 2^31 − 2^24, and the legacy,
     which passes it to B3, B4, B7 and the hop-back reach as `int32_t`, reads it as negative or wraps `ratio + 2^24`.
     It divides by nothing there, but the arithmetic is wrong: B3's beam edges and B4's back edge move the wrong way,
     B7's divisor is a sign-extended 64-bit value, and the hop-back reach takes its speed-up branch on a slowed-down
     voice. The rewrite reads the ratio unsigned (the contracts file's "ratio's signedness"), so all four change here.
   - **Past 8 octaves.** The ratio itself wraps in `SampleSource::stretch_to_undo_pitch`; at 12 it is 0, the combined
     increment is 0 and a cache-length divide divides by zero.
   - **A one-frame hop.** In the same far range a planned hop can come to one frame (B5: `hop = 1` when under 1), its
     crossfade is then capped at `1 >> 1` = 0, and `2^24 / 0` divides by zero.

   What the fix does:
   - (a) **Decided:** read the ratio unsigned everywhere, saturate it at `2^32 − 1` (a ratio of 256) instead of
     wrapping, and floor B5's hop at 2 (`hop = max(hop, 2)`, where `hop = 1` is today, before the crossfade). Then the
     uncapped crossfade is at least 39 frames (the fixed part is at least 40; at a 2-frame hop the share's product
     rounds to 0 or −1), so it is capped at `2 >> 1` = 1; `hop −= 1` leaves 1; the ceiling is at least 32, so `hop`
     stays 1; and `xf = min(1, 1)` = 1, `over_hop(1)` = 2^24: a one-frame hop with a one-frame crossfade. The floor
     changes nothing at planned hops of 2 or more, but the unsigned reading changes the 7-to-8-octave range above.
   - (b) Keep the legacy's signed reading from 7 to 8 octaves (bit-exact there), and saturate and floor only past 8.
     Past 8 octaves the saturated ratio is read unsigned, as 256 (read signed, `2^32 − 1` would be −1, a ratio
     below 0). This keeps arithmetic that is wrong by construction, and the rewrite would have to reproduce the
     wrapping int32 sums on purpose (signed overflow is undefined in C++), to keep a range no golden pins.
   - (c) Stretching stops (the voice plays unstretched) once the ratio passes 2^31 − 2^24, about 7 octaves down.
   - (d) The voice is cut there.
4. **How the contracts' values are recorded.**
   - (a) **Decided:** a temporary probe of the legacy code writes workloads' inputs and results; only the records
     header is committed. No legacy code enters the tree's tests.
   - (b) A verbatim test-only reference of the legacy computations under `tests/unit/dsp/`, as the live pitch shifter's
     rewrite kept, checked against the new components on generated workloads.
5. **The plain begin's shadow reader** (X3, E1). The legacy seats the spare as a level copy of the newer, reads it once
   at level 0 in the first segment (adding exactly 0: `mixed_in` rounds a zero product to 0), and then it stops where it
   is. E1 is never checked in the block a stretcher begins (the begin skips the check), so the copy matters from the
   next block on: having read the same frames as the newer from the same cursor, at an increment of 1.0 it stops on the
   newer's cursor, and a stretch that lasted one block, in one segment, ends at the next block's start, before any hop.
   That holds only while the segment stays within one chunk: the copy cannot move to the next chunk (R10's quirk), so a
   segment that crosses a chunk boundary stops the copy at it, the cursors never meet, and E1 waits for a hop. Pitched,
   or after a resampled block left the newer a nonzero interpolation width, the copy's width of 0 can leave its cursor
   elsewhere, and E1 then waits for a hop too. The copy's read cannot cut the voice: a failed read only stops it (R11).
   - (a) **Decided:** keep it, as the `Shadowed` phase, because it decides E1's timing: with it, a one-block stretch
     whose segment stays within a chunk ends at the next block's start, as the legacy's does, at the cost of one silent
     read of one segment per plain begin. The phase is deleted once the rewrite is proven: the `TODO.md` entry "Delete
     the time stretcher's `Shadowed` phase once the rewrite is proven" checks the goldens with the shadow read removed
     and drops it, accepting the one-block-stretch difference by name if any golden changes.
   - (b) Drop the copy and the read, and let E1 wait for a hop: a one-block stretch would run on until its first hop,
     a sample difference accepted by name.
6. **The slope follower and the percussiveness byte, shared with the pitch shifter.** The byte (P3) is the same
   arithmetic as the shifter's `percussiveness`; the legacy follower (P2) floors each pole's step where the shifter's
   `SlopeFollower` rounds it.
   - (a) **Decided:** the stretcher uses the shifter's own `SlopeFollower` (src/dsp/fixed/pitch_shift/slope_follower.h)
     and `percussiveness`, a difference accepted by name. One follower and one byte then exist, with one set of unit
     specs, and the floor, which only the legacy's arithmetic gave, goes with it. The perc bytes rank hop widths
     (B3); rounded, they rank them by the same measure, but each byte can move, and so can where a hop lands. Step 4
     re-records, in the same commit, the `sample/ts_*`, `sample/timestretch` and `RS_AUDIO_STRETCH*` goldens whose
     output changes, and every other step is bit-identical to that baseline. P2's records are the shared follower's.
   - (b) Move `percussiveness` to a header both include, and give the stretcher a floor follower of its own:
     bit-identical, at the cost of a second follower that differs from the shifter's by one rounding.
   - (c) A mode on the shifter's `SlopeFollower` for the floor step.
   - (d) Two independent copies.
7. **The aligner's abandoned search keeps a partial phase.** When a frame turns out not resident mid-search, the legacy
   starts the head at the planned frame (offset 0) but with a sub-sample phase from a sign flip found before it gave
   up: a phase for an offset it did not take. Only a card still loading reaches it; no golden does.
   - (a) **Decided:** fix, as a difference accepted by name: an abandoned search starts the head at phase 0.
   - (b) Keep it.
8. **The read heads.** The arc's design names the "dual read heads" a component and has the new code decide where the
   read heads sit. The stretcher decides where they sit and how loud they are (H1, A0, X2); the question is whether
   the heads themselves are rewritten too.
   - (a) Keep the legacy `SampleLowLevelReader` as both heads, behind the port, for this rewrite, and rewrite the read
     head later as its own step over the same goldens. A smaller rewrite, but the stretcher's seam contracts (A0, H1)
     would name the legacy reader's fields until then.
   - (b) **Decided:** write a new read-head component now (2.8): the interpolation window and the cursor over a
     `FrameSource` in the DSP library, the leases app-side, and switch unstretched playback to it as well. The heads
     are where every stretched sample comes from: rewritten, the seam's contracts (A0, H1, X2) name the read head's
     state and computations (R1 to R11) instead of an app type's fields, the cursor is a frame position the stretcher's
     arithmetic already counts in instead of a byte pointer into a chunk, and the window check, the chunk walk and the
     reads get unit specs of their own. Its interpolation and chunk walk are shared with unstretched playback, so both
     switch at once; its step (7a) is gated on unstretched playback staying bit-identical on golden-synth's sample
     cases and the render-song audio gates; step 1 measures the reader's own functions' coverage, and step 8 the read
     head's.
9. **The aligner's search, shared with the pitch shifter.** `deluge::dsp::fixed::pitch_shift::WaveformAligner`
   (`src/dsp/fixed/pitch_shift/waveform_aligner.h`) runs the same search over the same `crossfade.h` metrics: three
   moving averages slid a frame at a time, the best difference kept, a turn at a worse first step of the first pass
   searching forward, the backward pass starting one frame back, and a sub-sample phase placed at a sign flip of the
   summed change by the same division and the same one-frame correction. Read in play order (backwards when reversed),
   the stretcher's search is the shifter's searching forward first. They differ in the flip count (the shifter stops
   after 2 flips, the stretcher after 4), in what they read (the shifter a ring history, with its own reach quirk Q4 of
   `docs/dev/live_pitch_shifter_quirks.md`; the stretcher resident runs, abandoning the search at a frame not
   resident), and in the shifter's backward-first search when pitching down, which searches one way and never turns
   (its quirk Q10), a case the stretcher never has.
   - (a) **Decided:** one search core, src/dsp/fixed/timestretch/waveform_search.h, templated on the frame reader
     (how far it reaches, and whether a frame is readable) and parameterised by the flip count, the first pass's
     direction and whether the search turns (the shifter's pitch-down search is one way); the shifter's
     `WaveformAligner` and the stretcher's aligner become its two users, each keeping its own set-up and, for the
     stretcher, A5's final phase sum. The loop's sign, offset and phase rules then exist once. The shifter already
     includes `crossfade.h` from `timestretch/`, so the dependency runs the way it does today, and the shifter's goldens
     and unit specs check its side bit for bit in the same step (step 6).
   - (b) Keep two: the stretcher's own aligner, the shifter's untouched. No shifter change during this rewrite, at the
     cost of two copies of a loop whose order of operations is the contract.
   - (c) Write the stretcher's own in step 6, and merge the two as a step of their own after step 8.
10. **The reachable sides no gate scripts** (EDIT, LATE and STOP in the coverage list, 35 sides). They are reachable
    on the device: an audio clip's marker moved while it plays (`VoiceSample::sampleZoneChanged` for an audio clip),
    `AudioClip::resumePlayback` after a clip-length edit with the voice alive, an audio clip reversed while its voice
    reads a cache, a long sample the project does not reference auditioned from the browser, a late start part-way
    through a clip, and a release tail rendering after the clocks stop. They sit in `AudioClip::processCurrentPos`,
    `AudioClip::resumePlayback`, `AudioClip::render` and the paths to `end`, all of which step 7 changes.
    - (a) **Decided:** build render-song knobs and cases for them before step 7, recorded on the legacy: a marker
      move on a playing audio clip (as render-preset's `RENDER_ZONE_MOVE` moves a synth's zone), a clip-length edit
      while the clip's voice plays (`Song::setClipLength`), playback started part-way through a stretched clip, and
      the clocks stopped while a stretched clip's tail renders (render-song fails a render whose playback stops, so this
      case needs its own check). The reversed-while-cached and auditioned EDIT sides (4), and the recording (REC) and
      stem-export (EXPORT) sides, stay unpinned, accepted by name.
    - (b) Accept the 35 sides by name as unpinned: specced from reading, checked only by step 8's emulator sweep.
    - (c) Build the late start and the stopped-clock tail only, and accept the edits by name.

11. **The sides no golden reaches** (step 1's table: a card that has not answered, a failed allocation or memory
    pressure, CPU load high enough to cull, the two unbuilt sides).
    - (a) **Decided:** as the table says: fault injection in the probe for the seat, read, residency-check and
      allocation failures and for a forced cull; a sweep for C1's chunk-end limit; and the perc store's third-chunk
      rotation accepted as unpinned by name, since it decides residency and no sample. Each injected record becomes a
      unit spec, so the rewrite's failure paths equal the legacy's without a golden.
    - (b) As (a), and also a slow-card render-song gate (the stretch songs under `RENDER_FILL_BURST`) whose check
      allows the stretcher's retries, so the hop-retry path is pinned end to end. Its timing depends on the storage
      model, so its record moves whenever that model does.
    - (c) Accept every side no golden reaches as unpinned, specced from reading the legacy code.

12. **When the read head lands.** The read head replaces the reader behind the port's head operations (`shadow`,
    `seat_and_swap`, `read`, `mark`, `cursors_meet`) and in unstretched playback. The stretcher reaches the heads only
    through those operations, so the order changes the app side only.
    - (a) **Decided:** after the playback side, as step 7a. Step 7's adapter drives the legacy reader, which stops
      taking the stretcher, and step 7a swaps it for the read head behind the same operations, with no change to the
      stretcher. Each step changes one thing under the goldens, step 7 the stretcher and step 7a the reader, so a
      difference names its step, and no legacy stretcher code is edited. The adapter's head operations are written
      twice, once over each reader.
    - (b) Before the playback side, as step 6a. The adapter is written once, over the read head, but for one step the
      legacy `TimeStretcher` reads through the new head: its older reader, `setupNewPlayHead`, `hopEnd`'s play byte
      and `readSamplesForTimeStretching`'s callers are edited, in code step 7 deletes.
    - (c) With the playback side, in step 7: one switch, at the cost of a step whose difference, if any, could be
      either's.
13. **The unison clone's stop at its next chunk.** `SamplePlaybackAdapter::cloneVoiceSampleFrom` gives a unison part
    added to a sounding sample voice a level copy of its neighbour's reader, with no region-port cursor of its own, so
    when it plays unstretched it stops at its next chunk boundary (R10's quirk). Nothing intends this, and no
    render-song gate reaches it, since the failed walk counts an underrun.
    - (a) **Decided:** fix it in step 7a, as a difference accepted by name: the clone opens its own cursor, as a seat
      does, and walks on. The shadow copy keeps the stop, since E1's timing depends on it (decision 5).
    - (b) Keep the stop bit-identical in step 7a and fix it as a change of its own after step 8.

**Findings that need no decision.** These disappear with the legacy code and change no output:
- In `TimeStretcher::hopEnd`, the comment on the one-frame floor says it happens "if pitch up very high and also
  time-stretch sped up a lot". It is reached only with an increment under about 5,600, more than 11 octaves *down*,
  past the ratio's wrap (decision 3).
- In `VoiceSample::render`'s cache copy, `fudgingNow` is set and never read; its comment says the cached volume must
  be compensated when a fudge starts while a cache is written. Nothing compensates, and by reading the code nothing
  needs to: the stretched mix is at half scale and the copy's `<< 1` brings it to the unstretched cache's scale.
- `frame_read_origin`'s comment (`src/deluge/storage/cluster/cluster.h`) says the bytes before a frame are masked out
  of the read; the cache tail's copy does not mask them (C2).
- The legacy's `hasLoopedBackIntoPreMargin` is written and never read, the fudge start computes a pre-margin frame
  count it never uses, and its buffered-head mode (`TIME_STRETCH_ENABLE_BUFFER`) is compiled out.
- The reversed single-row waveform's column −1 (`TODO.md`) is in the waveform view, which the rewrite does not touch.
- Every failed chunk walk in the reader (`changeClusterIfNecessary` returning false) leaves it holding no region:
  `moveOnToNextCluster` clears `region_` when its acquire fails, and a low-level loop's restart and the 1,024-turn exit
  (E227) call `unassignAllReasons` first. So a chunk not ready mid-read gives zero taps and then a stopped head, never a
  cut voice (a stretcher's head stops and the voice reads on; unstretched, the stopped head ends the voice as its end
  does), and these branches are dead: `considerUpcomingWindow`'s "a Cluster wasn't loaded in time. So just cut the
  sound" exit (a failed walk with a region still held); `readSamplesForTimeStretching`'s card-error exit
  (`hasCurrentRegion()` after a failed check), and with it `VoiceSample::render`'s two exits on that read failing (the
  coverage list's DEF sides); and the native check's E001 and E147, since a walk that succeeds leaves the cursor
  strictly before the boundary, and the cursor and every boundary are whole frames from the data's start (the end and
  loop-end bytes are `data_start + k × frame_bytes`, and E163 checks the forwards chunk boundary). The rewrite's window
  check has no cut: it reads or the head has stopped (2.8).
- A hop's culling bypass (S3) never reaches a cull: `hopEnd` sets `AudioEngine::bypassCulling` during the render, the
  render clears it at its end, and `cullVoices` reads it only from `setDireness`, at the next block's start. The
  rewrite's `HopBudget` keeps setting it at the same point, so nothing changes.
