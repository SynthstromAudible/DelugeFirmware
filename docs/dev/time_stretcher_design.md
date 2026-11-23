# The time stretcher: design and numeric contracts

The sample time stretcher is rewritten as new C++23 code: pure DSP components in `src/dsp/fixed/timestretch/`, beside
`src/dsp/fixed/timestretch/crossfade.h`, and the playback side in `src/synth/sources/sample/`. Its output stays
bit-identical. This note defines the components; the arithmetic they must keep, contract by contract, is in
[time_stretcher_contracts.md](time_stretcher_contracts.md), which also defines the words used here. The legacy stretcher
(`src/deluge/dsp_legacy/timestretch/time_stretcher.cpp` and the stretcher paths of its five users) appears here only as
the computation each contract must equal; nothing is moved, split or copied from it.

## Decisions

The open questions (section 7), one line each, with the recommendation:
1. Where the stretcher reaches the sample: its own `SampleStretchPort` (14 operations); `SamplePlaybackPort` unchanged.
2. The cache tail's buffer: eight tail buffers the engine makes at start, lent through the `StretchContext`.
3. The ratio far below the root: read it unsigned everywhere, saturate it at `2^32 − 1`, floor B5's hop at 2.
4. Recording the contracts' values: a temporary probe of the legacy code, only its records header committed.
5. The plain begin's shadow reader: keep it, as the `Shadowed` phase.
6. The percussiveness byte and slope follower: share `percussiveness` with the pitch shifter; a floor follower of
   its own.
7. The aligner's abandoned search: start the head at phase 0.
8. The read heads: keep the legacy `SampleLowLevelReader` behind the port for this rewrite.
9. The aligner's search: one core shared with the pitch shifter's `WaveformAligner`, parameterised by its flip
   count, its first direction and whether it turns.
10. The reachable sides no gate scripts: build render-song cases for them (marker moves, clip-length edits, late starts,
    the stopped-clock tail) before step 7.
11. The sides no golden reaches: fault injection in the probe, a sweep for C1's chunk-end limit, and the third
    perc-cache chunk accepted as unpinned (step 1's table).

The differences Kate accepts by name if the recommendations are taken; everything else stays bit-identical, and the
contracts file's quirks are kept:
- **Far below the root** (3): from about 7 octaves down the ratio reads unsigned and saturates at 256 instead of reading
  negative or wrapping, and a one-frame hop gets a one-frame crossfade instead of dividing by zero.
- **An abandoned alignment** (7) starts the head at phase 0, not at a phase found for an offset it did not take.
- **A ninth crossfade out of a cache** (2) while all eight tail buffers are lent gets no crossfade, where the legacy
  allocates one.
- **Left unpinned:** the perc store's third-chunk rotation (4), which decides residency only; and the recording and
  stem-export sides (10).

## Coverage

The goldens that pin the stretcher are the `sample/ts_*` and `sample/timestretch` render-preset cases and the
`RS_AUDIO_STRETCH*` render-song gates (`tests/integration/checks/render_song_cases.md`). A host coverage build of them
(`scripts/tasks/stretcher_coverage.py`) reaches 815 of the 1,040 branch sides of the stretcher and its users' stretcher
functions. [time_stretcher_coverage.md](time_stretcher_coverage.md) lists the 225 it does not reach, keyed by function
and condition, each with a category, and a reason where the category's meaning does not already
give it:
- 64 the code itself rules out (defensive checks, conditions fixed by construction);
- 91 need a card that has not answered, an allocation that fails or memory pressure, or CPU load high enough to cull;
- 6 are not stretcher paths;
- 64 are reachable on the device, and no gate scripts them. EDIT (18: an audio clip's marker moved while it plays, a
  clip-length edit with its voice alive), LATE (12: playback started part-way through a clip) and STOP (5: a release
  tail rendering after the clocks stop) sit in the functions step 7 changes (`AudioClip::processCurrentPos`,
  `AudioClip::resumePlayback`, `AudioClip::render`, and their paths to the stretcher's queries and `end`): they are
  unpinned because nothing scripts them, not because they cannot happen (open question 10). REC (13) is an audio clip
  recording, EXPORT (14) the stem export's render, and OPEN (2) two sides a preset or song could reach but none is
  built for: a third perc-cache chunk, which needs a source over 2^23 frames, and a crossfade out of a cache whose read
  position is in a cache chunk's last frame.

How the residency, memory and unbuilt sides' legacy values are recorded is step 1's table (open question 11).

## Words

Used throughout:
- **frame**: one sample of every channel; positions count frames from the audio data's start.
- **ratio**: the stretch ratio, UQ8.24 (2^24 is 1.0): how fast the source is consumed against the output, pitch
  excluded. Above 1.0 the sound is sped up.
- **increment**: the phase increment, UQ8.24: how fast a head reads the source. Above 1.0 is pitched up.
- **combined**: `(ratio × increment) >> 24`, 64-bit: source frames consumed per output frame, UQ.24.
- **direction**: +1 forwards, −1 reversed.
- **head**: a sample reader playing the source from some position at the increment.
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
shape, but its arithmetic differs (rounded slope steps, a ring history instead of a file), so these are their own types.

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
`deluge::dsp::fixed::pitch_shift::WaveformAligner` runs the same search over a ring history; whether the two share one
core is open question 9.

```cpp
/// A run of readable frames from `frame` on in `direction`: each channel's sample is the 32-bit word whose top bytes
/// are the sample (unmasked: contract A2), `frames` long before a chunk or waveform boundary.
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

Home: src/dsp/fixed/timestretch/perc_analysis.h. The per-frame slope follower and the per-pixel percussiveness byte.
The sample keeps the bytes and its zones (which stretches of source are filled); this component only computes them.

```cpp
/// The source's slope, followed: a processor kernel over folded frames (a quarter of each channel's word, summed).
struct SourceSlopeFollower {
	using value_type = int32_t;
	using step_type = int32_t;
	struct Settings {}; struct Coefficients {};
	struct State { int32_t last = 0; int32_t previous_slope = 0; std::array<int32_t, 2> poles{}; };
	static int32_t step(const Coefficients&, const EmptyBlock&, State&, int32_t folded); // contract P2
};
/// Analyse a run of frames from `first_frame`, writing a byte at each pixel's middle frame. Contract P3.
void analyse(const FrameRun&, int32_t first_frame, int32_t frames, Direction, SourceSlopeFollower::State&,
             std::span<uint8_t> pixels, int32_t pixel_origin);
```

### 2.7 The playback-side stretcher

Home: src/synth/sources/sample/sample_stretcher.{h,cpp}, namespace `deluge::synth`. One per stretched voice-sample,
pooled. It owns the source position, the hop clock, the head phase, the perc-fill memo and every lease handle the
stretcher takes. It owns no reader (below). It drives the DSP components and reaches the sample only through the stretch
port (section 3). Engine-wide resources come in a `StretchContext` the engine passes to every call: the `HopBudget`,
the synth's noise stream and the cache-tail buffers.

**What the behaviour forces, and what is free.** Forced, because each is a point where the voice must be able to
cause an effect the goldens pin:
- three ways to begin: plain (S1: one noise draw, the first hop at 200 ± 32, the spare reader a level copy, X3);
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
	/// P4, then the block's segments. False: the note ended, or a card error cut it.
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
/// The older head: the voice-sample's spare reader, or a cache tail.
using Older = std::variant<SpareReader, CacheTail>;
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

**Who owns the readers.** May the app's `SampleLowLevelReader` (128 bytes) live inside this `deluge::synth` object?
No. It is an app type: its cursor and chunk leases are the storage layer's, and the synth tree names no app type
(`docs/dev/synth_source_ports.md`); embedding it would also tie the synth object's layout to it. So:
- the voice-sample (app) owns two readers, the one it plays from and a spare, with their cursors and chunk leases;
- the stretcher owns which role each plays, through its head phase, and how loud each is;
- the port seats, reads and marks the readers by role. A hop seats the spare at the new start (H1) and only once it
  has started swaps the roles, so a start that fails leaves the newer untouched and S4 needs no undo;
- when stretching ends, the voice-sample unseats its spare itself.

This is the one statement of who owns what; section 5's table follows it.

## 3. The seam to the sample

**Why a port of its own.** Two kinds of voice stretch: a synth voice plays its sample through `SamplePlaybackPort`
(`src/synth/sources/sample/sample_playback_port.h`), implemented by `SamplePlaybackAdapter`
(`src/deluge/model/voice/sample_playback_adapter.h`); an audio clip renders its `VoiceSample` directly, with no port.
Both reach the stretcher from `VoiceSample::render`. So the stretcher's sample access cannot live on
`SamplePlaybackPort` alone. It gets a port of its own, `SampleStretchPort`
(src/synth/sources/sample/sample_stretch_port.h), implemented once app-side over a voice-sample, its `Sample` and its
playback guide, and reached by both kinds of voice. `SamplePlaybackPort` keeps its render signature and gains nothing.
This is open question 1.

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
	// The readers, by role.
	virtual void shadow() = 0;                                           // the spare becomes a level copy (H1)
	virtual SeatResult seat_and_swap(int32_t frame, UQ<24> phase, LoopType) = 0; // H1; swaps only on success
	virtual HeadRead read(Head, Q<31>* out, int32_t frames, HeadGain) = 0;
	[[nodiscard]] virtual HeadMark mark(Head) const = 0;                 // A0's byte, and the phase (A5)
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
| `shadow`, `seat_and_swap`, `read`, `mark`, `cursors_meet` | the plain begin (X3), every hop and fudge (H1, S4), the segments (X2), the aligner's reference (A0, A5), E1 |
| `keep`, `let_go`, `hold`, `drop` | the hop-back, hop-retry and lookahead reservations and the two perc chunks (section 5) |

Merging further (the facts with the sync, the two lease pairs into one) would put two behaviours behind one call. The
port is smaller than `SamplePlaybackPort`, which has 23.

**The five users.**
- `src/deluge/model/voice/voice_sample.cpp`: `VoiceSample` holds a `SampleStretcher*` from the pool and the spare
  reader, and builds the port over itself, its sample and its guide on the stack for each call. Its render keeps the
  choice between reading a cache, writing one, and playing plain or stretched, the unsynced loop check (Q4, which calls
  `wrap_to`) and the cache-write copy (W1); the stretched segment loop (hops, gains, both heads, the position) is
  `SampleStretcher::render`. `fudgeTimeStretchingToAvoidClick` and `weShouldBeTimeStretchingNow` become `begin` (with a
  `Fudge` or `Plain` opening, and a `FromCacheTail` one where the render leaves a stretched cache); `endTimeStretching`
  becomes `end` and unseats the spare; `sampleZoneChanged` and `getPlaySample` read `frame()`.
- `src/deluge/model/sample/sample.cpp`: `Sample::fillPercCache` takes no stretcher. It returns a `PercFill`: where the
  filled zone now ends, the perc chunks the fill touched (for the stretcher to hold), and whether the lookahead
  reservation may be let go, with the frame it should move to. Its per-frame arithmetic calls the perc analysis
  (2.6). `Sample::prepareToReadPercCache` becomes the port's `perc_at`, returning a `PercWindow` with an origin in
  place of a pointer offset back from a chunk's payload. `Sample::getAveragesForCrossfade` goes: the aligner's
  `moving_averages` reads the same frames through `run`.
- `src/deluge/model/sample/sample_low_level_reader.cpp`: `SampleLowLevelReader` takes no stretcher. Its
  `readSamplesNative` and `readSamplesResampled` drop the parameter they only passed on, and
  `readSamplesForTimeStretching` returns whether the head still reads, and whether a card error stopped it, instead of
  writing a flag into the stretcher. Its reading arithmetic is unchanged: it is the sample reader's, shared with
  unstretched playback (open question 8).
- `src/deluge/model/clip/audio_clip.cpp`: reads `newer_reading()` and `is_fudge()` where it reads the legacy flags
  today (the restart at the loop, the fudge, the cull immunity), and begins the fudge through the voice-sample.
- `src/deluge/processing/engines/audio_engine.cpp`: the pool's type, and the `StretchContext` (a `HopBudget` in place
  of the two globals, the noise stream, the tail buffers); the render's culling bypass reads the budget.

## 4. Numeric contracts

The 44 contracts are in [time_stretcher_contracts.md](time_stretcher_contracts.md), one table per component, each as
its inputs, its exact arithmetic in order, and the legacy computation it must equal: hop tuning 3 (T1 to T3), scheduler
6 (S1 to S6), planner 8 (B1 to B8), aligner 7 (A0 to A6), crossfader 4 (X1 to X4), cache tail 4 (C1 to C4), perc
analysis 5 (P1 to P5), heads 1 (H1), position, loop, exit and cache write 6 (Q1 to Q4, E1, W1). The noise stream's
draw count is a contract too: one at a plain begin, one per hop attempt. Eight quirks are named there and kept; the
goldens pin six, and A2's and X1's are kept but not golden-pinned.

## 5. Lifetimes, threading and real-time limits

### Who owns what

Following section 2.7: the voice-sample owns the readers, the stretcher owns the roles and every lease handle, the
engine owns the tail buffers.

| Resource | Owner | Taken | Given back |
|---|---|---|---|
| The reader the voice plays from (the newer head), its cursor and chunk leases | the voice-sample | a plain begin takes nothing (the reader plays on); a hop or fudge seats the spare (H1), which becomes the newer | becomes the older at the next successful hop |
| The spare reader (the older head) | the voice-sample | a plain begin seats it as a level copy with its own lease on the shared chunk (`shadow`); a successful hop or fudge makes the previous newer the spare | reseated at the next hop (its leases go first, H1); unseated by the voice-sample when stretching ends |
| Hop-back reservation | the stretcher (a handle; the port keeps it) | at a begin and after every hop, from the earliest frame the next hop could start (below) | `end` |
| Hop-retry reservation | the stretcher | a hop whose start chunk is not resident | that hop's retry succeeding, or `end` |
| Perc lookahead reservation | the stretcher | each perc fill, moved to where it stopped reading | when the filled zone already covers the lookahead, or `end` |
| Perc-cache chunk leases | the stretcher, the two most recently used | a fill or a read touching a chunk not held (the older of the two is dropped) | `end` |
| A cache-tail buffer | the engine's tail buffers, lent to the stretcher through the `StretchContext` | a `FromCacheTail` begin | the crossfade's end, or `end` |
| The stretcher | the voice-sample, from the engine's pool | the first stretched block, or a fudge | `end`, then back to the pool |

The hop-back reach: `widest = max(max_width, min_width << 1)`, repitched as B2; `back = ((rate / 45) >> 1) + 1`, plus
`(int64(widest) × (2^24 − ratio)) >> 25` when slowed down; the reservation starts at `clamp(position − back × dir, 0,
length − 1)`. It decides only residency, not samples: the goldens' card answers at once.

The DSP components hold nothing: no reader, no lease, no pointer kept past a call. A `PercWindow` and a `FrameRun` are
valid only for the call they are passed to.

### Tiers

- **Audio tier (the render).** Every call that reads or writes samples: `begin`, `render` (hops included) and
  `wrap_to`. Taking a stretcher from the pool happens here.
- **The render's sequencer tick, or a playhead move on the interaction tier.** `AudioClip::processCurrentPos` and
  `AudioClip::resumePlayback` read `newer_reading()` and `is_fudge()` and may end stretching (`end`). On the
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
| 4 | 22 cases with a sample cache: `timestretch`, `ts_cache_bend_early`, `ts_cache_replay`, `ts_cached_loop` (and `stereo/ts_cached_loop`), `ts_marker_loop_end_cached`, `_ahead`, `ts_marker_start_cached`, `ts_markers`, `ts_mono`, `ts_perc`, `ts_pitch_cents`, `_down12`, `_down36`, `_fifth`, `_up36`, `ts_reversed`, `ts_short`, `ts_tonal`, `ts_zone_end_cached`, `_ahead`, `_once` |
| 5 | 6 cases that crossfade out of a cache: `ts_cache_bend`, `ts_cache_bend_root`, `ts_cache_crossfade`, `_mono`, `ts_cache_linear`, `ts_cache_stretch_amount` |
| 9 | `ts_poly` (four voices, four caches) |

Where they come from, traced by a debugger watchpoint on the counter in four cases (`ts_long_hold`, `ts_pitch_fifth`,
`ts_cache_crossfade`, `ts_poly`):
- **2, the perc cache's first fill** in `Sample::fillPercCache`: its byte block (`alloc_sdram`) and its zone list's
  growth. The sample's, not the stretcher's; it moves with the fill unchanged.
- **The sample cache** (`Sample::getOrCreateCache`): one for each cache's memory, plus each growth of the sample's
  cache list (once for the first cache; three times for `ts_poly`'s four, so 7 there). Not the stretcher's.
- **1, the cache tail's buffer**, the stretcher's only render allocation (`allocMaxSpeed` of 256 words per channel,
  1 KB mono or 2 KB stereo). Open question 2 removes it.
- In the four traced cases the pool (48 stretchers) never fell back to the heap. The other cases' counts follow the
  same pattern (2 for the perc cache, 2 for one sample cache, 1 per crossfade out of a cache); they were counted, not
  traced.

These counts are the ceiling: no case may rise. The render-song gates print no allocation count, so the songs have no
ceiling yet (section 6, step 1).

### On the device

Measured on the device image of this tree (`dbt build RelWithDebInfo`), with `arm-none-eabi-nm -S` and the section
headers:
- **Placement.** All of `.text` is on-chip SRAM. The legacy class's 18 functions occupy 7,384 bytes there,
  contiguously, and its tables 220 bytes of `.rodata` (also SRAM). Its users' stretcher functions add 4,084 bytes
  (`Sample::fillPercCache` 2,278, `Sample::getAveragesForCrossfade` 624, `VoiceSample::weShouldBeTimeStretchingNow`
  302, `VoiceSample::fudgeTimeStretchingToAvoidClick` 296, `Sample::prepareToReadPercCache` 288,
  `SampleLowLevelReader::readSamplesForTimeStretching` 208, the pool hand-out 54, `VoiceSample::endTimeStretching` 34);
  `VoiceSample::render` is 4,352. The class's `[[gnu::hot]]` does not decide where it lands: it sits at the front of the
  application's code because its archive links first, and `TrackChannel::render`, also `[[gnu::hot]]`, sits far from it.
  What keeps code fast is staying out of the size-optimised sources and out of the GUI archive the device runs from
  SDRAM (`src/deluge/CMakeLists.txt`); the new homes, the DSP library and `deluge_synth`, are in neither.
- **Objects.** The legacy object is 208 bytes, 128 of them its older head's reader; the pool holds 48 (9,984 bytes of
  fast memory). The voice-sample is 160 bytes; a perc zone 28.
- **Headroom.** The SRAM heap, `__sram_heap_end − __sram_heap_start`, is 616,240 bytes.

The rewrite's budget: the stretcher's code and tables stay in `.text`/`.rodata` outside the size-optimised set, within
a few hundred bytes of today's total (the DSP components are header-first and inline into their callers, so the
comparison is the users' and the stretcher's functions together, about 11.7 KB). In memory, the spare reader (128
bytes) moves from the stretcher into every voice-sample (160 bytes today, so 288), and the stretcher loses it: at the
default pool sizes, 48 voice-samples and 48 stretchers, that is +6,144 bytes and about −6,144 bytes, a net change of
about 0 (the new stretcher's exact size is settled when it is written). The pooled stretcher is no larger than
today's unless open question 2 chooses to embed the tail.

## 6. The rewrite's steps

One step per component, each with its contracts' unit specs written and failing before the component. Each step is
bit-identical on the goldens before it lands (golden-synth on x86, and golden-synth-arm filtered to the stretcher
cases).
1. **Records.** For every contract, workloads and the legacy's results, recorded by a temporary probe of the legacy code
  and committed as a records header under `tests/unit/dsp/` (open question 4). The render-song scenario reports its
  render-allocation count, so the songs get a ceiling. The probe's workloads:
  - **the goldens:** every `sample/ts_*` and `sample/timestretch` case and the four `RS_AUDIO_STRETCH*` songs, each
    contract's inputs and results logged at every call (the 815 sides they take);
  - **generated inputs** for the pure arithmetic: hop tuning over the whole ratio range, the planner, crossfader and
    cache-tail arithmetic over values the goldens leave out;
  - **fault injection:** the same golden renders with the probe answering "not ready" or "not resident", or failing an
    allocation, at a chosen call, and logging what the legacy does next;
  - **a sweep** for the one unbuilt side with sample output (C1's chunk-end limit, below).

  The sides only a card that has not answered, a failed allocation or an unbuilt source reaches, and how each one's
  legacy values are produced:

  | Contract or reservation | Side | Produced by |
  |---|---|---|
  | S4, and section 5's hop-retry reservation | a hop's new head cannot start; the retry | fault injection at the seat (`setupNewPlayHead`) at chosen hops: the state S4 leaves, the reservation, and the retry's outcome 500 frames on |
  | H1 | its three failure exits: the chunk setup not ready, the chunk move failing, a ready setup holding no region | fault injection at each, for a hop and for a fudge's begin (whose failure unassigns the voice) |
  | B8 | a start before the start marker whose frame is not resident moves to the marker (`hopEnd`'s start clamp) | fault injection at the clamp's residency check, before and after alignment |
  | A4 (and A5's partial phase, open question 7) | a frame not resident abandons the search | fault injection at the aligner's frame read, at a chosen step of a chosen pass |
  | X3, Q4's reads | a card error cuts the voice mid-read (`readSamplesForTimeStretching`) | fault injection: the reader loses its region mid-read |
  | C1 | the chunk-end limit: the read position in a cache chunk's last frame at the switch | a sweep: `sample/ts_cache_crossfade` rendered with its note length stepped a frame at a time until the switch lands there (about 1 in 5,400) |
  | P1, P5, and section 5's perc-chunk leases | a third distinct perc-cache chunk, the older of the two held dropped (`rememberPercCacheCluster`) | accepted as unpinned: it decides which chunks stay leased, never a sample; specced from reading against a fake port |
  | `AudioClip::getCullImmunity` and the voice cull (CULL) | a voice culled under CPU load | fault injection: the probe raises the direness at a chosen block, logging each voice's cull immunity and which voice the legacy culls |
  | Section 5's hop-back and lookahead reservations | where each is kept and let go | logged from the goldens, which run them; they decide residency only |
  | The allocations (the stretcher pool, the tail buffer, the perc store, a cache) and memory pressure | an allocation fails, or a cache or perc-cache chunk is stolen | fault injection in the probe's allocator, or an eviction between calls; the tail buffer's case changes with open question 2 |

  A slow-card render (render-song's `RENDER_FILL_BURST` over the stretch songs) reaches some of these without injection,
  as streaming-margin reached the cache-resume sides; its gate fails any underrun, so it cross-checks the injected
  records rather than producing them.
2. **Hop tuning** (2.1; T1 to T3).
3. **Crossfader and cache tail** (2.4, 2.5; X1, X2, C1 to C4).
4. **Perc analysis** (2.6; P1 to P3, P5), and `Sample::fillPercCache` computes through it and returns a `PercFill`
  instead of taking the stretcher.
5. **Hop planner** (2.2; B1 to B8).
6. **Waveform aligner** (2.3; A1 to A6; A0 is the port's, specced in step 7).
7. **The playback side.** `SampleStretchPort` and its app adapter (A0, H1), the `StretchContext` with `HopBudget` and
  the tail buffers, and `SampleStretcher` with its head phase (S1 to S6, X3, X4, P4, Q1 to Q4, E1, W1); `VoiceSample`
  gains its spare reader, and it, `AudioClip` and the engine switch over; `SampleLowLevelReader` stops taking the
  stretcher; `SampleSource::stretch_to_undo_pitch` (`src/synth/sources/sample/sample_source.cpp`) saturates the ratio
  instead of wrapping it (open question 3). The legacy class is deleted in this step, with `src/deluge/dsp_legacy/`,
  its target and its `scripts/layers.toml` rows.
8. **The gate.** Every golden bit for bit on x86 and ARM; no case's `RP-ALLOC` rises; x86 parity 0 differences; the
  nightly with both corpora; an emulator sweep against `main` with no unexplained difference; the device placement and
  sizes above; and the coverage script re-run over the new files, its untaken sides written into
  [time_stretcher_coverage.md](time_stretcher_coverage.md) in place of the legacy's, keyed by function and condition,
  each with a category and, where the category does not give it, a reason; and no RES, MEM, CULL or reachable side
  without the record or the acceptance step 1 gave its contract.

## 7. Open questions for Kate

Each is a decision with a recommendation first.

1. **Where the stretcher reaches the sample.** Audio clips render their voice-sample with no `SamplePlaybackPort`.
   - (a) **Recommended:** `SampleStretchPort`, a port of its own (14 operations, section 3), implemented once app-side
     over a voice-sample; synth voices and audio clips both reach it through `VoiceSample::render`.
     `SamplePlaybackPort` is unchanged.
   - (b) `SamplePlaybackPort` gains the stretch operations, and audio clips first move onto a `SamplePlaybackAdapter`
     (a larger change, outside the stretcher).
   - (c) Both: the stretch operations on `SamplePlaybackPort` for synth voices, and a second adapter for clips.
2. **The cache tail's buffer, the stretcher's one render allocation.**
   - (a) **Recommended:** a fixed set of tail buffers the engine makes when it starts (eight stereo buffers, 16 KB),
     lent through the `StretchContext`, an engine resource like the `HopBudget`, not through the per-voice port. With
     none free there is no crossfade, as the legacy does when its allocation fails. `RP-ALLOC` falls by one in the six
     crossfade cases.
   - (b) Embed the 2 KB buffer in every stretcher: the pool grows by about 96 KB of fast memory.
   - (c) Keep allocating on the render; the ceiling still holds.
3. **The stretch ratio far below the root.** The rewrite changes this range; the question is how. No golden pins any of
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
   - (a) **Recommended:** read the ratio unsigned everywhere, saturate it at `2^32 − 1` (a ratio of 256) instead of
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
   - (a) **Recommended:** a temporary probe of the legacy code writes workloads' inputs and results; only the records
     header is committed. No legacy code enters the tree's tests.
   - (b) A verbatim test-only reference of the legacy computations under `tests/unit/dsp/`, as the live pitch shifter's
     rewrite kept, checked against the new components on generated workloads.
5. **The plain begin's shadow reader** (X3, E1). The legacy seats the spare as a level copy of the newer, reads it once
   at level 0 in the first segment (adding exactly 0: `mixed_in` rounds a zero product to 0), and then it stops where it
   is. E1 is never checked in the block a stretcher begins (the begin skips the check), so the copy matters from the
   next block on: having read the same frames as the newer from the same cursor, at an increment of 1.0 it stops on the
   newer's cursor, and a stretch that lasted one block, in one segment, ends at the next block's start, before any hop.
   Pitched, or after a resampled block left the newer a nonzero interpolation width, the copy's width of 0 can leave its
   cursor elsewhere, and E1 then waits for a hop. The copy's read can also hit a card error and cut the voice.
   - (a) **Recommended:** keep it, as the `Shadowed` phase: E1's timing and the card-error cut stay as they are, at
     the cost of one silent read of one segment per plain begin.
   - (b) Drop the copy and the read, and let E1 wait for a hop: a one-block stretch would run on until its first hop,
     a sample difference accepted by name.
6. **The slope follower and the percussiveness byte, shared with the pitch shifter?** The byte (P3) is the same
   arithmetic as the shifter's `percussiveness`; the follower (P2) floors where the shifter's rounds.
   - (a) **Recommended:** move `percussiveness` to a header both include; the stretcher gets its own floor follower.
   - (b) A mode on the shifter's `SlopeFollower` for the floor step.
   - (c) Two independent copies.
7. **The aligner's abandoned search keeps a partial phase.** When a frame turns out not resident mid-search, the legacy
   starts the head at the planned frame (offset 0) but with a sub-sample phase from a sign flip found before it gave
   up: a phase for an offset it did not take. Only a card still loading reaches it; no golden does.
   - (a) **Recommended:** fix, as a difference accepted by name: an abandoned search starts the head at phase 0.
   - (b) Keep it.
8. **The read heads.** The arc's design names the "dual read heads" a component and has the new code decide where the
   read heads sit. This note keeps the legacy `SampleLowLevelReader` as both heads, behind the port: the stretcher
   decides where they sit and how loud they are (H1, A0, X2), not how they read.
   - (a) **Recommended:** keep the legacy reader behind the port for this rewrite. Its interpolation and chunk walk are
     shared with unstretched playback, so a new head here would change both at once, and its contracts at the seam
     (A0, H1) are already the port's. A read-head rewrite follows as its own step over the same goldens.
   - (b) Write a new read-head component now (the interpolation window and the cursor over a `FrameSource` in the DSP
     library, the leases app-side) and switch unstretched playback to it as well.
9. **The aligner's search, shared with the pitch shifter?** `deluge::dsp::fixed::pitch_shift::WaveformAligner`
   (`src/dsp/fixed/pitch_shift/waveform_aligner.h`) runs the same search over the same `crossfade.h` metrics: three
   moving averages slid a frame at a time, the best difference kept, a turn at a worse first step of the first pass
   searching forward, the backward pass starting one frame back, and a sub-sample phase placed at a sign flip of the
   summed change by the same division and the same one-frame correction. Read in play order (backwards when reversed),
   the stretcher's search is the shifter's searching forward first. They differ in the flip count (the shifter stops
   after 2 flips, the stretcher after 4), in what they read (the shifter a ring history, with its own reach quirk Q4 of
   `docs/dev/live_pitch_shifter_quirks.md`; the stretcher resident runs, abandoning the search at a frame not
   resident), and in the shifter's backward-first search when pitching down, which searches one way and never turns
   (its quirk Q10), a case the stretcher never has.
   - (a) **Recommended:** one search core, src/dsp/fixed/timestretch/waveform_search.h, templated on the frame reader
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
    - (a) **Recommended:** build render-song knobs and cases for them before step 7, recorded on the legacy: a marker
      move on a playing audio clip (as render-preset's `RENDER_ZONE_MOVE` moves a synth's zone), a clip-length edit
      while the clip's voice plays (`Song::setClipLength`), playback started part-way through a stretched clip, and
      the clocks stopped while a stretched clip's tail renders (render-song fails a render whose playback stops, so this
      case needs its own check). The reversed-while-cached and auditioned EDIT sides (4), and the recording (REC) and
      stem-export (EXPORT) sides, stay unpinned, accepted by name.
    - (b) Accept the 35 sides by name as unpinned: specced from reading, checked only by step 8's emulator sweep.
    - (c) Build the late start and the stopped-clock tail only, and accept the edits by name.

11. **The sides no golden reaches** (step 1's table: a card that has not answered, a failed allocation or memory
    pressure, CPU load high enough to cull, the two unbuilt sides).
    - (a) **Recommended:** as the table says: fault injection in the probe for the seat, read, residency-check and
      allocation failures and for a forced cull; a sweep for C1's chunk-end limit; and the perc store's third-chunk
      rotation accepted as unpinned by name, since it decides residency and no sample. Each injected record becomes a
      unit spec, so the rewrite's failure paths equal the legacy's without a golden.
    - (b) As (a), and also a slow-card render-song gate (the stretch songs under `RENDER_FILL_BURST`) whose check
      allows the stretcher's retries, so the hop-retry path is pinned end to end. Its timing depends on the storage
      model, so its record moves whenever that model does.
    - (c) Accept every side no golden reaches as unpinned, specced from reading the legacy code.

**Findings that need no decision.** These disappear with the legacy code and change no output:
- In `TimeStretcher::hopEnd`, the comment on the one-frame floor says it happens "if pitch up very high and also
  time-stretch sped up a lot". It is reached only with an increment under about 5,600, more than 11 octaves *down*,
  past the ratio's wrap (open question 3).
- In `VoiceSample::render`'s cache copy, `fudgingNow` is set and never read; its comment says the cached volume must
  be compensated when a fudge starts while a cache is written. Nothing compensates, and by reading the code nothing
  needs to: the stretched mix is at half scale and the copy's `<< 1` brings it to the unstretched cache's scale.
- `frame_read_origin`'s comment (`src/deluge/storage/cluster/cluster.h`) says the bytes before a frame are masked out
  of the read; the cache tail's copy does not mask them (C2).
- The legacy's `hasLoopedBackIntoPreMargin` is written and never read, the fudge start computes a pre-margin frame
  count it never uses, and its buffered-head mode (`TIME_STRETCH_ENABLE_BUFFER`) is compiled out.
- The reversed single-row waveform's column −1 (`TODO.md`) is in the waveform view, which the rewrite does not touch.
- A hop's culling bypass (S3) never reaches a cull: `hopEnd` sets `AudioEngine::bypassCulling` during the render, the
  render clears it at its end, and `cullVoices` reads it only from `setDireness`, at the next block's start. The
  rewrite's `HopBudget` keeps setting it at the same point, so nothing changes.
