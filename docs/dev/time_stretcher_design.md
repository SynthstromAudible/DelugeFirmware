# The time stretcher: design and numeric contracts

The sample time stretcher is rewritten as new C++23 code: pure DSP components in `src/dsp/fixed/timestretch/`, beside
`src/dsp/fixed/timestretch/crossfade.h`, and the playback side in `src/synth/sources/sample/`. Its output stays
bit-identical. This note defines the components and the arithmetic they must keep. The legacy stretcher
(`src/deluge/dsp_legacy/timestretch/time_stretcher.cpp` and the stretcher paths of its five users) appears here only as
the computation each contract must equal; nothing is moved, split or copied from it.

The goldens that pin the stretcher are the `sample/ts_*` and `sample/timestretch` render-preset cases and the
`RS_AUDIO_STRETCH*` render-song gates (`tests/integration/checks/render_song_cases.md`). A host coverage build of
them (`scripts/tasks/stretcher_coverage.py`) reaches 794 of the 992 branch sides of the stretcher and its users'
stretcher functions; every side not reached has a written reason (residency, memory pressure, a defensive check, a
recording, a late start, an edit no gate scripts), and two are reachable but unbuilt (a third perc-cache chunk, which
needs a source over 2^23 frames, and a crossfade from a cache that starts in a cache chunk's last frame).

Words used throughout:
- **frame**: one sample of every channel; positions count frames from the audio data's start.
- **ratio**: the stretch ratio, UQ8.24 (2^24 is 1.0): how fast the source is consumed against the output, pitch
  excluded. Above 1.0 the sound is sped up.
- **increment**: the phase increment, UQ8.24: how fast a head reads the source. Above 1.0 is pitched up.
- **combined**: `(ratio × increment) >> 24`, 64-bit: source frames consumed per output frame, UQ.24.
- **direction**: +1 forwards, −1 reversed.
- **head**: a sample reader playing the source from some position at the increment.

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
  tracks, `sample/ts_pitch_up36` and `_down36`), and the crossfade is a share of the hop plus a fixed part.
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
name without defining (`BlockArgs`, `Markers`, `PercFill`, `Reservation` and the like) are settled when the rewrite is
planned; the responsibilities, the ownership and the contracts are what this note fixes.

### 2.1 Hop tuning (DSP)

Home: src/dsp/fixed/timestretch/hop_tuning.h. Pure, constexpr tables, no state.

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
so it never sees a cluster; the playback side supplies runs of resident frames.

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
frames copied from the cache, read once into the mix. It owns no memory: the playback side lends it a buffer (see
section 5).

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
pooled. It owns the stretch state, the older head, the cache tail's lent buffer and every lease the stretcher takes;
it drives the DSP components and reaches the sample only through the stretch port (section 3).

```cpp
class SampleStretcher {
public:
	struct Start { int64_t position; Ratio ratio; Increment increment; Direction direction; LoopType loop; };
	/// A normal start: first hop at 200 ± 32 (contract S1), the hop-back reservation placed. One noise draw.
	bool start(SampleStretchPort&, const Start&, Cong& noise);
	/// A fudge: the new head starts in the pre-margin now and crossfades in over the frames to the loop. S2.
	bool start_fudge(SampleStretchPort&, const Start&, int32_t output_frames_to_loop);
	/// The stretcher-made loop wrapped the position: take it, and hop now if the newer head has stopped. S5.
	void restart_at(SampleStretchPort&, int64_t position, const BlockArgs&);
	/// Fill the perc analysis ahead of the position for this block. P4.
	void prepare_block(SampleStretchPort&, const BlockArgs&);
	/// Crossfade out of a cache's next written frames into this stretcher's head. C1-C3.
	void crossfade_from_cache(SampleStretchPort&, const CacheWindow&, int32_t channels);
	/// Render `frames` into `out`: hops, segments, both heads, the position. False: the note ended (or a card error).
	bool render(SampleStretchPort&, Q<31>* out, const BlockArgs&, HopBudget&, Cong& noise);
	/// Whether stretching may stop now without a click (contract E1).
	[[nodiscard]] bool can_stop(SampleStretchPort&) const;
	[[nodiscard]] int32_t position_frame() const;                         // Q2
	[[nodiscard]] bool newer_sounding() const;
	[[nodiscard]] bool fudging() const;
	/// Release every lease, the older head and the tail buffer. Explicit: the object is pooled and reused in place.
	void release(SampleStretchPort&);

private:
	SourcePosition position_;      ///< Q24 frames, int64 (Q1).
	HopClock hop_;                 ///< Frames to the next hop, times passed over by the budget (S3).
	CrossfadeRamp ramp_;
	HeadActivity heads_;           ///< Which heads still read: newer, older.
	OlderHead older_;              ///< A reader slot the port owns, or the cache tail.
	PercFillMemo perc_fill_;       ///< Where the last fill started and ended, and its direction (P4).
	StretchLeases leases_;         ///< Perc lookahead, hop-back, hop-retry reservations; two perc chunks.
};
```

Two small engine-side pieces go with it:
- **`HopBudget`** (src/synth/sources/sample/hop_budget.h): how many hops this audio routine call has run, and whether a
  hop asked for culling to be bypassed. The engine owns one, resets it at each routine call, and passes it to every
  render. It replaces two engine globals (contract S3).
- **The pool**: the engine keeps an `ObjectPool` (`src/deluge/memory/object_pool.h`) of `SampleStretcher`, as it does
  of the legacy class today.

## 3. The seam to the sample

**Why a port of its own.** Two kinds of voice stretch: a synth voice plays its sample through `SamplePlaybackPort`
(`src/synth/sources/sample/sample_playback_port.h`), implemented by `SamplePlaybackAdapter`
(`src/deluge/model/voice/sample_playback_adapter.h`); an audio clip renders its `VoiceSample` directly, with no port.
Both reach the stretcher from `VoiceSample::render`. So the stretcher's sample access cannot live on
`SamplePlaybackPort` alone. The design gives it a narrow port, `SampleStretchPort`
(src/synth/sources/sample/sample_stretch_port.h), implemented once app-side over a voice-sample, its `Sample` and its
playback guide, and reached by both kinds of voice. `SamplePlaybackPort` keeps its render signature; it gains nothing.
This is open question 1.

```cpp
class SampleStretchPort {
public:
	// The source's layout and markers.
	struct Layout { int32_t length_frames; int32_t channels; int32_t byte_depth; int32_t sample_rate; };
	[[nodiscard]] virtual Layout layout() const = 0;
	[[nodiscard]] virtual Markers markers() const = 0;         // start, start marker, end-or-loop-end, loop start, in frames
	[[nodiscard]] virtual std::optional<int64_t> synced_position() const = 0; // a synced clip with a running clock
	// Frames, for the aligner: a zero-lease peek (FrameSource).
	[[nodiscard]] virtual std::optional<FrameRun> run(int32_t frame, Direction) const = 0;
	// The perc analysis: what is filled at a pixel, and a fill.
	[[nodiscard]] virtual std::optional<PercWindow> perc_at(int32_t pixel, Direction) = 0;
	virtual PercFill fill_perc(int32_t from, int32_t to, Direction, int32_t max_frames) = 0;
	// The heads. The newer head is the voice-sample's own reader; the older is a reader slot the stretcher holds.
	virtual bool start_newer(int32_t frame, UQ<24> phase, LoopType) = 0; // leases for the new head
	virtual void newer_becomes_older(OlderSlot&) = 0;                   // the reader and its leases move
	virtual void older_becomes_newer(OlderSlot&) = 0;                   // a failed hop puts it back
	virtual HeadRead read(Head, Q<31>* out, int32_t frames, const ReadArgs&) = 0; // C++ reader, unchanged arithmetic
	[[nodiscard]] virtual int32_t head_frame(Head) const = 0;
	[[nodiscard]] virtual UQ<24> head_phase(Head) const = 0;
	[[nodiscard]] virtual bool heads_level() const = 0;                 // contract E1
	virtual void release_older(OlderSlot&) = 0;
	// Leases the stretcher holds, by handle; the port opens, moves and closes them.
	virtual void reserve(Reservation&, int32_t frame, Direction) = 0;
	virtual void close(Reservation&) = 0;
	virtual void lease_perc_chunk(PercChunk) = 0;
	virtual void release_perc_chunk(PercChunk) = 0;
	// The cache tail's buffer.
	virtual std::span<Q<31>> borrow_tail(int32_t channels) = 0;          // empty: none free (open question 2)
	virtual void return_tail(std::span<Q<31>>) = 0;
};
```

**The five users.**
- `src/deluge/model/voice/voice_sample.cpp`: `VoiceSample` holds a `SampleStretcher*` from the pool instead of the
  legacy class, and builds the port over itself, its sample and its guide on the stack for each call. Its render keeps
  the choice between reading a cache, writing one, and playing plain or stretched, and the cache-write copy (contract
  W1); the stretched segment loop (hops, gains, both heads, the position) moves into `SampleStretcher::render`. The
  unsynced loop-end check (contract Q4) stays with the voice, which owns the loop, and calls `restart_at`.
  `fudgeTimeStretchingToAvoidClick`, `weShouldBeTimeStretchingNow`, `endTimeStretching`, `sampleZoneChanged` and
  `getPlaySample` call the stretcher's `start_fudge`, `start` and `prepare_block`, `release`, and `position_frame`.
- `src/deluge/model/sample/sample.cpp`: `Sample::fillPercCache` takes no stretcher. It returns a `PercFill`: where the
  filled zone now ends, the perc chunks the fill touched (for the stretcher to lease), and whether the lookahead
  reservation may close, with the frame it should move to. Its per-frame arithmetic calls the perc analysis
  (2.6). `Sample::prepareToReadPercCache` becomes the port's `perc_at`, returning a `PercWindow` with an origin in
  place of a pointer offset back from a chunk's payload. `Sample::getAveragesForCrossfade` goes: the aligner's
  `moving_averages` reads the same frames through the port.
- `src/deluge/model/sample/sample_low_level_reader.cpp`: `SampleLowLevelReader` takes no stretcher. Its
  `readSamplesNative` and `readSamplesResampled` drop the parameter they only passed on, and
  `readSamplesForTimeStretching` returns whether the head is still reading, and whether a card error stopped it,
  instead of writing a flag into the stretcher. Its reading arithmetic is unchanged: it is the sample reader's, shared
  with unstretched playback, not the stretcher's.
- `src/deluge/model/clip/audio_clip.cpp`: reads `newer_sounding()` and `fudging()` where it reads the legacy flags
  today (the restart at the loop, the fudge, the cull immunity), and starts the fudge through the voice-sample.
- `src/deluge/processing/engines/audio_engine.cpp`: the pool's type, and a `HopBudget` in place of the two globals;
  the render's culling bypass reads the budget.

## 4. Numeric contracts

Each contract is a computation whose result feeds output samples (or, for S3 and E1, decides when they change). It is
written as its inputs, its exact arithmetic in order, and the legacy computation it must equal. Each becomes a unit
spec against values recorded from the legacy code before its component is written (open question 4). Integer types are
the legacy's: where a contract says 32-bit unsigned, a signed value is reinterpreted, and the wrap is part of the
contract. A **quirk** is arithmetic the goldens pin that a fresh design would not choose; it is kept and named in the
code.

### Hop tuning

| # | Inputs | Arithmetic, in order | Must equal |
|---|---|---|---|
| T1 | ratio | `log = quick_log(ratio)` (`src/dsp/fixed/curves/quick_log.h`). Fine band when `800·2^20 ≤ log < 864·2^20`: `pos = log − 800·2^20`; each field is `kLinearTableInterpolator.interpolate_signed(pos, 26, fine_table, 4)`; widths and the fixed crossfade part `>> 16`; the share `<< 8` (32-bit, wrapping); the jitter share unshifted. | `getHopParams`, fine branch |
| T2 | ratio | Coarse band otherwise: clamp `log` to [768·2^20, 896·2^20]; `pos = log − 768·2^20`, then `pos = 2^27 − 1` if `pos ≥ 2^27`; each field is `interpolate_signed(pos, 27, coarse_table, 2)`, shifted as T1. | `getHopParams`, coarse branch |
| T3 | — | The ten tables, fine (17 entries, −12 to +12 semitones of speed in steps of 1.5) and coarse (5 entries, −2 to +2 octaves): minimum width, maximum width, crossfade share, fixed crossfade part, jitter. Values as the legacy's `minHopSizeFine` … `randomCoarse`. **Quirks:** the fixed part's fine entry at −1.5 semitones is 170 among 10s; share entries of 128 and over (fine 160 and 140, coarse 200 and 160) wrap negative at `<< 8`, which B5's unsigned cap turns into half the hop (pinned: `sample/ts_speed_slow`, RSCRAWL, RSHALT). | the legacy tables |

### Hop scheduler (in the playback-side stretcher)

| # | Inputs | Arithmetic, in order | Must equal |
|---|---|---|---|
| S1 | noise | First hop: `200 + (int8_t(noise.next() >> 24) >> 2)` output frames (−32 to +31); ramp done (`progress = 2^24`, increment 0). Exactly one draw, at a normal start; none at a fudge. | `TimeStretcher::init`, normal start |
| S2 | frames to loop `n` (output), increment | If increment ≠ 2^24: `n' = (uint64(n) × increment + 2^23) >> 24`, else `n' = n`. The new head starts `n'` frames into the pre-margin; ramp `over_fudge(n')` (X1 c); the next hop never comes (INT32_MAX). **Quirk:** the crossfade lasts `n'` output frames, a source-frame count, so a pitched fudge's crossfade ends off the loop point (pinned: `RS_AUDIO_STRETCH_EDGES`'s tied tracks). | `TimeStretcher::init`, fudge |
| S3 | hops this routine `h`, times passed over `m`, frames to hop `t` | Per segment, only while the newer head reads: due when `t ≤ 0`; allowed when `h < 3` and `uint32(m) ≥ uint32(h) << 1`. Due and allowed: hop, `h += 1`, `m = 0`, culling bypassed. Due and not allowed: `m += 1`. When allowed, the segment is `min(rest of block, t)`; when not, the rest of the block (so `t` goes negative). After the segment `t −= length`. | `VoiceSample::render`'s hop check; `TimeStretcher::hopEnd`'s counter and culling bypass |
| S4 | — | A hop whose new head cannot start (its chunk not ready): the old head is put back as the newer, the older is marked stopped, the ramp's increment is 0, the next hop in 500 frames; the wanted chunk is reserved for the retry if it is not resident, else the retry reservation closes. | `TimeStretcher::hopEnd`, failed start |
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

### Waveform aligner

| # | Inputs | Arithmetic, in order | Must equal |
|---|---|---|---|
| A1 | increment, crossfade | `each = clamp(int32((uint64(increment) × 35) >> 24), 1, 70)`; `xf_src = int32((uint64(xf) × increment) >> 24)`. Only while the older head reads, and only if its byte is not before the data's start. | `hopEnd`, the search set-up |
| A2 | frames, start, `xf_src`, `each` | `mid = start + (xf_src >> 1) × dir`; `read = mid − ((each × 3) >> 1) × dir`. Forwards only: fail if `read` is within half the crossfade of either end. Fail if the window's end falls outside the data (one byte of slack before its start). Three totals, each the sum over `each` frames of `word >> 16` per channel (left plus right). The words are unmasked 32-bit reads ending at each sample's top byte. **Quirk:** the reversed direction has no half-crossfade margin. | `Sample::getAveragesForCrossfade` |
| A3 | hop frames, increment, rate | `limit = (hop × 40) >> 8`; `limit = int32((uint64(limit) × increment) >> 24)`; `limit = min(limit, (rate / 45) >> 1)`. `hop` is the value after B5 (or B7). | `hopEnd`, the search size |
| A4 | old and new averages | Best starts as `totalDifferenceAbs(old, new)` at offset 0. Search the play direction first. Each pass: the four window boundaries step one frame at a time (against play: the first boundary starts one frame back); each running total gains its upper boundary's value and loses its lower one, signed by search-against-play. At each step: if this is the first step of a first pass searching with play and it is worse, switch direction now (that pass does not count: the other direction is searched in full, then the play direction again); a strictly smaller difference is the new best; when the sign of `totalChange` flips, count a flip (four flips over both passes stop the search). A pass ends at the limit or the waveform's end; a frame not resident abandons the search. | `hopEnd`, the phase search |
| A5 | at a sign flip, increment ≠ 2^24 | If this step or the previous is the best: `phase = (uint64(abs(last)) << 24) / uint32(abs(last) + abs(this))`, `2^24 − phase` when searching against play; if (this step is best) differs from (searching against play), the best offset moves one frame back in play. At the end: `phase += older head's phase`; past 2^24, subtract 2^24 and move one frame on. | `hopEnd`, sub-sample alignment |
| A6 | offset | The start moves by the offset; clamped to the waveform's first frame; then B8. | `hopEnd`, after the search |

### Crossfader

| # | Inputs | Arithmetic, in order | Must equal |
|---|---|---|---|
| X1 | frames `n` | Four increments, all 32-bit unsigned: (a) a hop: `2^24 / n`; (b) a loop crossfade: `(2^24 − 1 + n) / n`; (c) a fudge: `(2^24 + n) / n`; (d) a cache tail: `2^24 / uint16(n) + 1`. Progress starts at 0. **Quirk:** (b) and (c) differ by one where `n` divides 2^24. | `hopEnd` (a, b), `init` (c), `setupCrossfadeFromCache` (d) |
| X2 | ramp, segment `n`, level, step | `newer = as_wrap<Q31>(Q24::from_raw(progress))`; `older = 2147483647 − newer`; `progress += increment × n` (wrapping); `after = as_clamp_then_shift<Q31>(Q24::from_raw(progress))`; `newer_step = divide_by_small(after − newer, n)`; `change = mul_as_wrap<Q31>(level, newer_step)`; gains: newer `mul_as_wrap<Q31>(level, newer)` stepped `level_step + change`; older `mul_as_wrap<Q31>(level, older)` stepped `level_step − change`. | `VoiceSample::render`, the crossfade envelope |
| X3 | ramp, heads | The older head sounds while it reads and `progress < 2^24`; after its segment, if `progress ≥ 2^24` it stops. While it reads but the ramp is done (before the first hop), it is read at level 0 and step 0 (open question 5). | `VoiceSample::render`, the older head |
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

### Position, loop, exit and cache write

| # | Inputs | Arithmetic, in order | Must equal |
|---|---|---|---|
| Q1 | segment `n` | `position += int64(combined) × n × dir`, combined `= (uint64(uint32(ratio)) × uint32(increment)) >> 24`. | `VoiceSample::render`, after the heads |
| Q2 | position | The whole frame: `position >> 24` forwards, `(position + 2^24 − 1) >> 24` reversed. | `TimeStretcher::getSamplePos` |
| Q3 | play byte | Start: `position = floor((play_byte − data_start) / bytes_per_frame) << 24` (rounding toward −∞; the byte may be before the data). | `weShouldBeTimeStretchingNow`, `fudgeTimeStretchingToAvoidClick` |
| Q4 | position, block part `n`, loop end byte | Unsynced, per uncached read: `after = position + combined × n × dir` (`+ 2^24 − 1` reversed); `over = (int32(after >> 24) × bytes_per_frame + data_start − loop_end_byte) × dir`; if `over > 0`: `end = rel ≥ 0 ? rel / uint8(bytes_per_frame) : −1`; `left = (int64(end) << 24) − position`; if `left × dir < int64(combined)`: looping restarts at `(int64(start_frame) << 24) − left` (S5), at most 1,024 times in a block; not looping ends the note. Else `n = min(n, uint64(left × dir) / combined)`. | `VoiceSample::render`'s loop check |
| E1 | heads | Stretching may stop when the newer and older heads are at the same byte and the voice is synced with no lag (or not synced). | `VoiceSample::render`, leaving the stretcher |
| W1 | stretched mix | Writing a cache: the heads render at X4's cache level into a zeroed scratch block; per frame the cache takes the top three bytes of `word << 1`, left then right; the output mixes the word (mono: `(l >> 1) + (r >> 1)` after the left is cached) with `mixed_in`, the amplitude stepped first. | `VoiceSample::render`, the cache copy |

**Counts.** 42 contracts: hop tuning 3, scheduler 6, planner 8, aligner 6, crossfader 4, cache tail 4, perc analysis
5, position, loop, exit and cache write 6. Seven are about when, not what (S3, S4, S5, B6, B8, E1, P4); the rest are
arithmetic.

**Noise draws.** The synth's noise stream is shared by every voice, so the draw count is a contract too: one at a
normal start (S1) and one per hop attempt, a failed one included, whatever the hop does after (B1). None at a fudge or at a restart that does
not hop.

## 5. Lifetimes, threading and real-time limits

### Who owns what

| Resource | Owner | Taken | Given back |
|---|---|---|---|
| The newer head's reader and its chunk leases | the voice-sample (its own reader) | a normal start: none taken (the voice's reader plays on); a hop: fresh leases for the new head | moves to the older slot at the next hop |
| The older head's reader | the stretcher's older slot | a normal start: an independent copy with its own lease on the shared chunk (the copy has no cursor of its own); a fudge or a hop: the voice's reader moves in, leases and cursor | the next hop (replaced), a failed hop (moved back), `release` |
| Hop-back reservation | the stretcher | at a start and after every hop, from the earliest frame the next hop could start (below) | `release` |
| Hop-retry reservation | the stretcher | a hop whose start chunk is not resident | that hop's retry succeeding, or `release` |
| Perc lookahead reservation | the stretcher | each perc fill, moved to where it stopped reading | when the filled zone already covers the lookahead, or `release` |
| Perc-cache chunk leases | the stretcher, the two most recently used | a fill or a read touching a chunk not held (the older of the two is released) | `release` |
| The cache tail's buffer | the stretcher, lent | `crossfade_from_cache` | the next hop, or `release` |
| The stretcher | the voice-sample, from the engine's pool | the first stretched block, or a fudge | `release`, then back to the pool |

The hop-back reach: `widest = max(max_width, min_width << 1)`, repitched as B2; `back = ((rate / 45) >> 1) + 1`, plus
`(int64(widest) × (2^24 − ratio)) >> 25` when slowed down; the reservation starts at `clamp(position − back × dir, 0,
length − 1)`. It decides only residency, not samples: the goldens' card answers at once.

The DSP components hold nothing: no reader, no lease, no pointer kept past a call. A `PercWindow` and a `FrameRun` are
valid only for the call they are passed to.

### Tiers

- **Audio tier (the render).** Every call that reads or writes samples: `start`, `start_fudge`, `prepare_block`,
  `render` (hops included), `crossfade_from_cache`, `restart_at`. Taking a stretcher from the pool happens here.
- **The render's sequencer tick, or a playhead move on the interaction tier.** `AudioClip::processCurrentPos` and
  `AudioClip::resumePlayback` read `newer_sounding()` and `fudging()` and may end stretching (`release`). On the
  interaction tier they run inside a move's section (`docs/dev/playback_moves.md`): the render is held off and nothing
  may allocate. `release` allocates nothing; returning to the pool pushes onto its reserved free list.
- **Voice unassignment** calls `release` from wherever the voice-sample is unassigned today, under the same exclusion.

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
comparison is the users' and the stretcher's functions together, about 11.7 KB); the pooled object no larger than
today's unless open question 2 chooses to embed the tail.

## 6. The rewrite's steps

One step per component, each with its contracts' unit specs written and failing before the component. Each step is
bit-identical on the goldens before it lands (golden-synth on x86, and golden-synth-arm filtered to the stretcher cases).
1. **Records.** For every contract, workloads and the legacy's results, recorded by a temporary probe of the legacy code
  and committed as a records header under `tests/unit/dsp/` (open question 4). The render-song scenario reports its
  render-allocation count, so the songs get a ceiling.
2. **Hop tuning** (2.1; T1 to T3).
3. **Crossfader and cache tail** (2.4, 2.5; X1, X2, C1 to C4).
4. **Perc analysis** (2.6; P1 to P3, P5), and `Sample::fillPercCache` computes through it and returns a `PercFill`
  instead of taking the stretcher.
5. **Hop planner** (2.2; B1 to B8).
6. **Waveform aligner** (2.3; A1 to A6).
7. **The playback side.** `SampleStretchPort`, its app adapter, `HopBudget` and `SampleStretcher` (S1 to S6, X3,
  X4, P4, Q1 to Q4, E1, W1); `VoiceSample`, `AudioClip` and the engine switch over; `SampleLowLevelReader` stops taking
  the stretcher. The legacy class is deleted in this step, with `src/deluge/dsp_legacy/`, its target and its
  `scripts/layers.toml` rows.
8. **The gate.** Every golden bit for bit on x86 and ARM; no case's `RP-ALLOC` rises; x86 parity 0 differences; the
  nightly with both corpora; an emulator sweep against `main` with no unexplained difference; the device placement and
  sizes above; and the coverage script re-run over the new files, with every untaken side given a reason.

## 7. Open questions for Kate

Each is a decision with a recommendation first.

1. **Where the stretcher reaches the sample.** Audio clips render their voice-sample with no `SamplePlaybackPort`.
   - (a) **Recommended:** a narrow `SampleStretchPort`, implemented once app-side over a voice-sample; both synth
     voices and audio clips reach it through `VoiceSample::render`. `SamplePlaybackPort` is unchanged.
   - (b) `SamplePlaybackPort` gains the stretch operations, and audio clips first move onto a `SamplePlaybackAdapter`
     (a larger change, outside the stretcher).
   - (c) Both: the stretch operations on `SamplePlaybackPort` for synth voices and a second adapter for clips.
2. **The cache tail's buffer, the stretcher's one render allocation.**
   - (a) **Recommended:** a fixed set of tail buffers made when the engine starts (eight stereo buffers, 16 KB),
     lent by the port; with none free, no crossfade, as the legacy does when its allocation fails. `RP-ALLOC` falls by
     one in the six crossfade cases.
   - (b) Embed the 2 KB buffer in every stretcher: the pool grows by about 96 KB of fast memory.
   - (c) Keep allocating on the render; the ceiling still holds.
3. **The stretch ratio far below the root** (accepted difference, already decided: the rewrite fixes it; no golden
   pins it). Past 8 octaves below the root the ratio wraps; at 12 a zero combined increment divides by zero. The same
   range also makes the planner's one-frame hop give a zero crossfade and divide 2^24 by zero (B5), which no golden
   reaches. What the fix does:
   - (a) **Recommended:** the ratio saturates at the largest UQ8.24 value instead of wrapping (in
     `SampleSource::stretch_to_undo_pitch`, beside the playback side), and a hop of one frame gets a one-frame
     crossfade; a zero combined increment cannot occur.
   - (b) Stretching stops (the voice plays unstretched) once the ratio would leave the range.
   - (c) The voice is cut there.
4. **How the contracts' values are recorded.**
   - (a) **Recommended:** a temporary probe of the legacy code writes workloads' inputs and results; only the records
     header is committed. No legacy code enters the tree's tests.
   - (b) A verbatim test-only reference of the legacy computations under `tests/unit/dsp/`, as the live pitch shifter's
     rewrite kept, checked against the new components on generated workloads.
5. **The older head read in silence before the first hop.** From a normal start until the first hop, the legacy reads
   its shadow older head at level 0 every block; mixing at level 0 adds exactly 0 (`mixed_in` rounds 0 to 0), so it
   changes no sample, but it keeps the two heads level for E1 and for the note's end.
   - (a) **Recommended:** no older head until the first hop; E1 holds before it, and the note ends when the newer head
     does. No sample changes; the stretcher reads half as much for its first 200 frames.
   - (b) Keep the silent read.
6. **The slope follower and the percussiveness byte, shared with the pitch shifter?** The byte (P3) is the same
   arithmetic as the shifter's `percussiveness`; the follower (P2) floors where the shifter's rounds.
   - (a) **Recommended:** move `percussiveness` to a header both include; the stretcher gets its own floor follower.
   - (b) A mode on the shifter's `SlopeFollower` for the floor step.
   - (c) Two independent copies.
7. **The aligner's abandoned search keeps a partial phase.** When a frame turns out not resident mid-search, the legacy
   starts the head at the planned frame (offset 0) but with a sub-sample phase from a sign flip found before it gave
   up: a phase for an offset it did not take. Only a card still loading reaches it; no golden does.
   - (a) **Recommended:** fix, as a difference accepted by name: an abandoned search starts the head at phase 0 (or
     the older head's phase, as a finished search adds).
   - (b) Keep it.

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
