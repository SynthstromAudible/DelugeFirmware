# The time stretcher's coverage

How far the goldens reach the rewritten time stretcher, its read head, and their users' stretcher functions; why each
branch side they do not reach is not reached; and what pins its behaviour instead. The rewrite is
[time_stretcher_design.md](time_stretcher_design.md)'s; this is its step 8's measure.

**What was run.** `scripts/tasks/stretcher_coverage.sh` builds `golden_vt_render` with clang source coverage and runs,
through the gates' own checks, golden-synth (487 cases) and the 66 render-song gates whose output is compared with
`tests/golden/render/songs.json`. Only recorded renders count: a song gate's dry-twin and unswitched renders, and gates
that keep no audio record (streaming-margin, arrangement-take), are left out, as is render-song-corpus, whose record
predates the design note's decision 6. The functions measured are the script's `SCOPE`
(`scripts/tasks/stretcher_coverage.py`): every function of the stretcher's own files, of the time-stretch and
sample-read components (`src/dsp/fixed/timestretch/`, `src/dsp/fixed/sample_read/`), of the stretch port and of the
read lease; and in their users the functions that call into the stretcher or the read head, branch on whether a voice
has a stretcher, or keep the hop budget and the culling bypass a hop sets.

**Measured before the read's NEON data plane** (2026-11-24): the scope now also holds `stage.{h,cpp}`, `gather.h`,
`quad_mix.h` and `native_quads.h`, and the read head's sinc and native kernels moved into them, so the totals and the
read head's rows below are the scalar kernels' until `scripts/tasks/stretcher_coverage.sh` is run again. The data plane
is held to the scalar kernels word for word by `read_head_oracle_spec`.

**Result.** 1,295 of the 1,650 branch sides are taken; the 355 untaken are listed below by file and function. A side is
named by its condition, the branch's source text; where one function tests the same text more than once, its repeats
are counted in source order. A template's functions are listed per instantiation.

## Categories

| Category | Sides | Meaning |
|---|---|---|
| **Ruled out by the code** | 102 | |
| DEF | 71 | defensive: the code before it already rules the condition out (named per side) |
| CON | 31 | by construction: the condition is fixed by the code itself (named per side) |
| **Needs a fault or load** | 112 | |
| RES | 53 | residency: the sample data asked for is not resident (a card read still pending, a chunk evicted, or a card error). The goldens' card answers at once and their gates fail any underrun |
| MEM | 34 | memory: an allocation fails, or memory pressure steals a cache or perc-cache chunk; no golden runs out of memory |
| CULL | 25 | voice culling under CPU load, and the sinc window a reader shrinks to linear under it; the gates fail a render whose direness rises, so neither happens |
| **Reachable on the device, no gate scripts it** | 127 | |
| EDIT | 19 | an edit while a voice plays that the edit songs leave out: a clip-length edit of an unstretched, cached or releasing voice (`resumePlayback`), a clip reversed while it reads a cache, a long unreferenced sample auditioned, a head re-acquired past its end |
| LATE | 13 | a late start's paths render-audio-stretch-late leaves out: a late start while the previous sound fades, or one that restarts while releasing |
| STOP | 4 | a clip rendering after both clocks stop. A stop fast-releases the audio output's envelope at 65,536 per frame over a stage of 2^23, so it ends within the first 128-frame block and the voice is unassigned before it renders: a song stopped under a stretched clip renders no voice block after the stop. Only a render shorter than a block leaves a partial tail |
| REC | 13 | an audio clip recording, or an aborted recorder |
| EXPORT | 14 | the offline stem export's render |
| OPEN | 64 | reachable from a preset or song, not built: a third perc-cache chunk (a source over 2^23 frames), a crossfade from a cache whose read position is in a cache chunk's last frame, and the read head's content corners (a stereo sample read linearly while rendering in stereo, a cache written from a mono sample, tail zeros past a non-looping end, an end point on a chunk boundary, a pitch over four octaves up) |
| **Not a stretcher path** | 14 | |
| NS | 14 | a voice with no stretcher, or an unstretched cache; other goldens own it |

**Pinned by** names what holds a side's behaviour where no golden reaches it:
- a records array of `tests/unit/dsp/time_stretcher_records.h`, whose rows (the legacy's values, taken by the records
  probe) take that side when its spec replays them: `kSeatFaults`, `kReadHeadFaults`, `kAllocationFaults`,
  `kEvictionFaults`, `kCullFaults` and the others named;
- a unit spec case, for a side of the read head's or the search's own code that no record reaches: the spec and its
  case are named;
- an acceptance by name: decision 10 (the render-song edit and late-start cases; the EDIT and LATE sides they leave),
  decision 11 (an app-side fault or cull side whose legacy value no record holds: accepted, with its reason), the STOP
  ruling (a stop fast-releases the voice within its first block), decision 3 (the stretched ratio's saturation, a
  difference from the legacy by design), or "accepted (the app's: no golden builds it)" for an app-side OPEN side.

Every RES, MEM, CULL, EDIT, LATE, STOP and OPEN side names one. A side's reason is left as "—" where its category's
meaning gives it.

## Functions

| File (under `src/`) | Function | Calls | Lines run | Branch sides taken |
|---|---|---|---|---|
| `dsp/fixed/sample_read/frame_source.h` | deluge::dsp::fixed::sample_read::frame_word | 62152370 | 5/5 | 0/0 |
| `dsp/fixed/timestretch/crossfade.h` | deluge::dsp::fixed::totalDifferenceAbs | 4851041 | 8/8 | 4/4 |
| `dsp/fixed/timestretch/crossfade.h` | deluge::dsp::fixed::totalChange | 4846868 | 7/7 | 2/2 |
| `dsp/fixed/timestretch/crossfade.h` | deluge::dsp::fixed::timestretch::CrossfadeRamp::over_hop | 8824 | 3/3 | 0/0 |
| `dsp/fixed/timestretch/crossfade.h` | deluge::dsp::fixed::timestretch::CrossfadeRamp::over_loop | 161 | 4/4 | 0/0 |
| `dsp/fixed/timestretch/crossfade.h` | deluge::dsp::fixed::timestretch::CrossfadeRamp::over_fudge | 112 | 3/3 | 0/0 |
| `dsp/fixed/timestretch/crossfade.h` | deluge::dsp::fixed::timestretch::CrossfadeRamp::over_cache_tail | 580 | 3/3 | 0/0 |
| `dsp/fixed/timestretch/crossfade.h` | deluge::dsp::fixed::timestretch::CrossfadeRamp::done | 213269 | 1/1 | 0/0 |
| `dsp/fixed/timestretch/crossfade.h` | deluge::dsp::fixed::timestretch::segment_gains | 105975 | 15/15 | 0/0 |
| `dsp/fixed/timestretch/hop_tuning.h` | deluge::dsp::fixed::timestretch::hop_shape | 19119 | 19/19 | 6/6 |
| `dsp/fixed/timestretch/perc_analysis.h` | deluge::dsp::fixed::timestretch::perc_store_layout | 20986 | 5/5 | 2/2 |
| `dsp/fixed/timestretch/perc_analysis.h` | deluge::dsp::fixed::timestretch::fold | 11472154 | 7/7 | 2/2 |
| `dsp/fixed/timestretch/perc_analysis.h` | deluge::dsp::fixed::timestretch::analyse | 21043 | 16/16 | 8/10 |
| `dsp/fixed/timestretch/perc_analysis.h` | deluge::dsp::fixed::timestretch::zone_for_fill | 0 | 0/14 | 0/0 |
| `dsp/fixed/timestretch/perc_analysis.h` | deluge::dsp::fixed::timestretch::overwrites | 0 | 0/4 | 0/0 |
| `dsp/fixed/timestretch/hop_planner.h` | deluge::dsp::fixed::timestretch::jittered | 8985 | 6/6 | 0/0 |
| `dsp/fixed/timestretch/hop_planner.h` | deluge::dsp::fixed::timestretch::repitched | 8824 | 5/5 | 0/0 |
| `dsp/fixed/timestretch/hop_planner.h` | deluge::dsp::fixed::timestretch::best_width | 8807 | 42/42 | 16/16 |
| `dsp/fixed/timestretch/hop_planner.h` | deluge::dsp::fixed::timestretch::back_edge | 8824 | 11/11 | 4/4 |
| `dsp/fixed/timestretch/hop_planner.h` | deluge::dsp::fixed::timestretch::hop_lengths | 8824 | 11/11 | 0/0 |
| `dsp/fixed/timestretch/hop_planner.h` | deluge::dsp::fixed::timestretch::past_end | 8824 | 5/5 | 2/2 |
| `dsp/fixed/timestretch/hop_planner.h` | deluge::dsp::fixed::timestretch::loop_crossfade | 3822 | 37/39 | 9/10 |
| `dsp/fixed/timestretch/hop_planner.h` | deluge::dsp::fixed::timestretch::clamp_start | 16602 | 4/4 | 3/4 |
| `dsp/fixed/timestretch/hop_planner.h` | deluge::dsp::fixed::timestretch::plan_hop | 8985 | 33/33 | 8/8 |
| `synth/sources/sample/perc_fill.h` | deluge::synth::PercFill::touch | 2585 | 10/12 | 5/6 |
| `synth/sources/sample/perc_fill.h` | deluge::synth::PercFillMemo::wanted | 196963 | 13/13 | 10/10 |
| `synth/sources/sample/perc_fill.h` | deluge::synth::PercFillMemo::filled | 20986 | 5/5 | 0/0 |
| `dsp/fixed/sample_read/mixed_in.h` | deluge::dsp::fixed::sample_read::mixed_in | 88699849 | 3/3 | 0/0 |
| `dsp/fixed/sample_read/read_head.h` | deluge::dsp::fixed::sample_read::ReadHead::top_word | 38661212 | 5/5 | 0/0 |
| `dsp/fixed/sample_read/read_head.h` | deluge::dsp::fixed::sample_read::ReadHead::frames_stepped | 123302 | 5/5 | 0/0 |
| `dsp/fixed/sample_read/read_head.h` | deluge::dsp::fixed::sample_read::ReadHead::mix | 13009 | 9/9 | 2/2 |
| `dsp/fixed/sample_read/read_head.h` | deluge::dsp::fixed::sample_read::ReadHead::cache_word | 25744636 | 6/6 | 0/0 |
| `dsp/fixed/sample_read/read_head.h` | deluge::dsp::fixed::sample_read::ReadHead::fill_silence | 0 | 0/17 | 0/6 |
| `dsp/fixed/sample_read/read_head.h` | deluge::dsp::fixed::sample_read::ReadHead::counted | 17231 | 3/3 | 1/2 |
| `dsp/fixed/sample_read/read_head.h` | deluge::dsp::fixed::sample_read::ReadHead::level_copy | 1241 | 1/1 | 0/0 |
| `dsp/fixed/sample_read/read_head.h` | deluge::dsp::fixed::sample_read::ReadHead::play_byte | 26012 | 13/13 | 6/6 |
| `dsp/fixed/timestretch/cache_tail.h` | deluge::dsp::fixed::timestretch::CacheTail::CacheTail | 580 | 7/7 | 2/2 |
| `dsp/fixed/timestretch/cache_tail.h` | deluge::dsp::fixed::timestretch::CacheTail::CacheTail | 0 | 0/1 | 0/0 |
| `dsp/fixed/timestretch/cache_tail.h` | deluge::dsp::fixed::timestretch::CacheTail::words_written | 887 | 3/3 | 0/0 |
| `dsp/fixed/timestretch/cache_tail.h` | deluge::dsp::fixed::timestretch::CacheTail::mix_into | 887 | 31/33 | 14/16 |
| `dsp/fixed/timestretch/cache_tail.h` | deluge::dsp::fixed::timestretch::CacheTail::frames | 0 | 0/1 | 0/0 |
| `dsp/fixed/timestretch/cache_tail.h` | deluge::dsp::fixed::timestretch::CacheTail::mixed_in_floor | 154798 | 3/3 | 0/0 |
| `dsp/fixed/timestretch/cache_tail.h` | deluge::dsp::fixed::timestretch::cache_tail_frames | 580 | 10/14 | 2/4 |
| `deluge/model/clip/audio_clip.cpp` | AudioClip::processCurrentPos | 10222 | 62/76 | 31/50 |
| `deluge/model/clip/audio_clip.cpp` | AudioClip::resumePlayback | 6 | 31/53 | 15/28 |
| `deluge/model/clip/audio_clip.cpp` | AudioClip::render | 223334 | 135/143 | 77/88 |
| `deluge/model/clip/audio_clip.cpp` | AudioClip::getLoopingType | 223337 | 6/6 | 8/8 |
| `deluge/model/clip/audio_clip.cpp` | AudioClip::getCullImmunity | 0 | 0/5 | 0/4 |
| `synth/sources/sample/stretch_context.h` | deluge::synth::HopBudget::allows | 202137 | 3/3 | 4/4 |
| `synth/sources/sample/stretch_context.h` | deluge::synth::HopBudget::spend | 8985 | 4/4 | 0/0 |
| `synth/sources/sample/stretch_context.h` | deluge::synth::HopBudget::new_routine | 538961 | 4/4 | 0/0 |
| `synth/sources/sample/stretch_context.h` | deluge::synth::TailBuffers::lend | 580 | 7/9 | 1/2 |
| `synth/sources/sample/stretch_context.h` | deluge::synth::TailBuffers::give_back | 580 | 1/1 | 0/0 |
| `synth/sources/sample/stretch_context.h` | deluge::synth::TailBuffers::free_count | 0 | 0/1 | 0/0 |
| `synth/sources/sample/hop_clock.h` | deluge::synth::HopClock::first_hop | 1240 | 3/3 | 0/0 |
| `synth/sources/sample/hop_clock.h` | deluge::synth::HopClock::never | 112 | 1/1 | 0/0 |
| `synth/sources/sample/hop_clock.h` | deluge::synth::HopClock::retry | 0 | 0/1 | 0/0 |
| `synth/sources/sample/hop_clock.h` | deluge::synth::HopClock::after | 10337 | 1/1 | 0/0 |
| `synth/sources/sample/hop_clock.h` | deluge::synth::HopClock::at | 10337 | 6/6 | 0/0 |
| `synth/sources/sample/hop_clock.h` | deluge::synth::HopClock::segment | 202137 | 13/13 | 6/6 |
| `synth/sources/sample/hop_clock.h` | deluge::synth::HopClock::frames | 192182 | 1/1 | 0/0 |
| `synth/sources/sample/hop_clock.h` | deluge::synth::HopClock::advance | 203668 | 1/1 | 0/0 |
| `synth/sources/sample/hop_clock.h` | deluge::synth::HopClock::to_hop | 580 | 1/1 | 0/0 |
| `synth/sources/sample/hop_clock.h` | deluge::synth::HopClock::passed_over | 0 | 0/1 | 0/0 |
| `synth/sources/sample/hop_clock.h` | deluge::synth::fudge_into | 112 | 7/7 | 2/2 |
| `synth/sources/sample/stretch_leases.h` | deluge::synth::hop_back_frame | 10134 | 10/10 | 2/2 |
| `synth/sources/sample/stretch_mix.h` | deluge::synth::stretch_amplitude | 543983 | 4/4 | 4/4 |
| `synth/sources/sample/stretch_mix.h` | deluge::synth::segment_level | 203762 | 6/6 | 2/2 |
| `synth/sources/sample/stretch_mix.h` | deluge::synth::step_amplitude | 203668 | 6/6 | 2/2 |
| `synth/sources/sample/stretch_mix.h` | deluge::synth::write_cache_frames | 5570 | 23/23 | 8/8 |
| `synth/sources/sample/stretch_position.h` | deluge::synth::SourcePosition::SourcePosition | 11248 | 1/1 | 0/0 |
| `synth/sources/sample/stretch_position.h` | deluge::synth::SourcePosition::from_play_byte | 1352 | 6/6 | 1/2 |
| `synth/sources/sample/stretch_position.h` | deluge::synth::SourcePosition::synced | 8284 | 6/6 | 0/0 |
| `synth/sources/sample/stretch_position.h` | deluge::synth::SourcePosition::advance | 203762 | 3/3 | 0/0 |
| `synth/sources/sample/stretch_position.h` | deluge::synth::SourcePosition::frame | 208678 | 3/3 | 2/2 |
| `synth/sources/sample/stretch_position.h` | deluge::synth::SourcePosition::raw | 9316 | 1/1 | 0/0 |
| `synth/sources/sample/stretch_position.h` | deluge::synth::combined_increment | 402181 | 3/3 | 0/0 |
| `synth/sources/sample/stretch_position.h` | deluge::synth::loop_step | 9018 | 33/33 | 11/12 |
| `synth/sources/sample/stretch_position.h` | deluge::synth::may_end | 738 | 4/4 | 9/12 |
| `synth/sources/sample/sample_stretcher.h` | deluge::synth::SampleStretcher::frame | 1490 | 1/1 | 0/0 |
| `synth/sources/sample/sample_stretcher.h` | deluge::synth::SampleStretcher::position | 9018 | 1/1 | 0/0 |
| `synth/sources/sample/sample_stretcher.h` | deluge::synth::SampleStretcher::is_fudge | 0 | 0/1 | 0/0 |
| `synth/sources/sample/sample_stretcher.h` | deluge::synth::SampleStretcher::clock | 0 | 0/1 | 0/0 |
| `deluge/model/sample/sample.cpp` | Sample::fillPercCache | 20986 | 226/269 | 78/98 |
| `deluge/model/sample/sample.cpp` | Sample::perc_at | 8824 | 47/54 | 19/22 |
| `synth/sources/sample/perc_fill.h` | deluge::synth::PercChunk::operator== | 8219 | 1/1 | 0/0 |
| `deluge/model/voice/voice_sample.cpp` | VoiceSample::beenUnassigned | 1947 | 4/4 | 0/0 |
| `deluge/model/voice/voice_sample.cpp` | VoiceSample::noteOn | 1946 | 10/10 | 0/0 |
| `deluge/model/voice/voice_sample.cpp` | VoiceSample::endTimeStretching | 2726 | 12/12 | 2/2 |
| `deluge/model/voice/voice_sample.cpp` | VoiceSample::attemptLateSampleStart | 2 | 49/62 | 13/24 |
| `deluge/model/voice/voice_sample.cpp` | VoiceSample::fudgeTimeStretchingToAvoidClick | 112 | 23/32 | 5/8 |
| `deluge/model/voice/voice_sample.cpp` | VoiceSample::weShouldBeTimeStretchingNow | 196963 | 30/34 | 12/14 |
| `deluge/model/voice/voice_sample.cpp` | VoiceSample::lease | 666691 | 4/4 | 0/0 |
| `deluge/model/voice/voice_sample.cpp` | VoiceSample::readFacts | 26012 | 7/7 | 0/0 |
| `deluge/model/voice/voice_sample.cpp` | VoiceSample::loopStartFrame | 44101 | 4/4 | 0/0 |
| `deluge/model/voice/voice_sample.cpp` | VoiceSample::setupClusersForInitialPlay | 1946 | 15/23 | 8/14 |
| `deluge/model/voice/voice_sample.cpp` | VoiceSample::changeClusterIfNecessary | 16 | 8/10 | 4/6 |
| `deluge/model/voice/voice_sample.cpp` | VoiceSample::reassessReassessmentLocation | 2349 | 4/4 | 2/2 |
| `deluge/model/voice/voice_sample.cpp` | VoiceSample::getPlayByteLowLevel | 17027 | 5/5 | 2/2 |
| `deluge/model/voice/voice_sample.cpp` | VoiceSample::unassignAllReasons | 1987 | 3/3 | 0/0 |
| `deluge/model/voice/voice_sample.cpp` | VoiceSample::render | 543982 | 443/522 | 182/234 |
| `deluge/model/voice/voice_sample.cpp` | VoiceSample::sampleZoneChanged | 22 | 108/122 | 52/64 |
| `deluge/model/voice/voice_sample.cpp` | VoiceSample::getPlaySample | 17145 | 10/10 | 3/4 |
| `synth/sources/sample/stretch_mix.h` | deluge::synth::stretch_amplitude | 1087961 | 3/3 | 0/0 |
| `synth/sources/sample/stretch_mix.h` | deluge::synth::write_cache_frames | 1321291 | 6/6 | 0/0 |
| `dsp/fixed/sample_read/read_head.h` | deluge::dsp::fixed::sample_read::Residency deluge::dsp::fixed::sample_read::ReadHead::place<SampleReadLease> | 11920 | 7/12 | 3/6 |
| `dsp/fixed/sample_read/read_head.h` | bool deluge::dsp::fixed::sample_read::ReadHead::walk<SampleReadLease> | 678173 | 24/37 | 12/16 |
| `dsp/fixed/sample_read/read_head.h` | deluge::dsp::fixed::sample_read::ReadHead::Window deluge::dsp::fixed::sample_read::ReadHead::check<SampleReadLease> | 623310 | 11/11 | 5/6 |
| `dsp/fixed/sample_read/read_head.h` | deluge::dsp::fixed::sample_read::ReadHead::Window deluge::dsp::fixed::sample_read::ReadHead::check_native<SampleReadLease> | 498318 | 13/23 | 6/10 |
| `dsp/fixed/sample_read/read_head.h` | void deluge::dsp::fixed::sample_read::ReadHead::step_back<SampleReadLease> | 0 | 0/14 | 0/8 |
| `dsp/fixed/sample_read/read_head.h` | int deluge::dsp::fixed::sample_read::ReadHead::bytes_to_boundary<SampleReadLease> | 620320 | 5/5 | 2/2 |
| `dsp/fixed/sample_read/read_head.h` | bool deluge::dsp::fixed::sample_read::ReadHead::reshape<SampleReadLease> | 124991 | 12/29 | 3/8 |
| `dsp/fixed/sample_read/read_head.h` | void deluge::dsp::fixed::sample_read::ReadHead::back_fill<SampleReadLease> | 1143 | 22/25 | 8/10 |
| `dsp/fixed/sample_read/read_head.h` | short deluge::dsp::fixed::sample_read::ReadHead::tap<SampleReadLease> | 352099 | 4/4 | 1/2 |
| `dsp/fixed/sample_read/read_head.h` | bool deluge::dsp::fixed::sample_read::ReadHead::forward_fill<SampleReadLease> | 1143 | 17/19 | 6/8 |
| `dsp/fixed/sample_read/read_head.h` | bool deluge::dsp::fixed::sample_read::ReadHead::first_step<SampleReadLease> | 124991 | 27/29 | 16/18 |
| `dsp/fixed/sample_read/read_head.h` | void deluge::dsp::fixed::sample_read::ReadHead::push_frame<SampleReadLease> | 161270 | 8/8 | 2/2 |
| `dsp/fixed/sample_read/read_head.h` | bool deluge::dsp::fixed::sample_read::ReadHead::push_zero<SampleReadLease> | 2405 | 12/12 | 7/8 |
| `dsp/fixed/sample_read/read_head.h` | int deluge::dsp::fixed::sample_read::ReadHead::window_length<SampleReadLease> | 124941 | 32/32 | 12/14 |
| `dsp/fixed/sample_read/read_head.h` | void deluge::dsp::fixed::sample_read::ReadHead::read_native<SampleReadLease> | 497266 | 2/2 | 0/0 |
| `dsp/fixed/sample_read/read_head.h` | void deluge::dsp::fixed::sample_read::ReadHead::by_layout<void deluge::dsp::fixed::sample_read::ReadHead::read_native<SampleReadLease> | 497266 | 13/13 | 4/4 |
| `dsp/fixed/sample_read/read_head.h` | auto void deluge::dsp::fixed::sample_read::ReadHead::read_native<SampleReadLease> | 45958 | 3/3 | 0/0 |
| `dsp/fixed/sample_read/read_head.h` | void deluge::dsp::fixed::sample_read::ReadHead::read_native_as<2, 2, SampleReadLease> | 45958 | 30/32 | 5/6 |
| `dsp/fixed/sample_read/read_head.h` | void deluge::dsp::fixed::sample_read::ReadHead::put<2> | 8125886 | 8/8 | 0/0 |
| `dsp/fixed/sample_read/read_head.h` | auto void deluge::dsp::fixed::sample_read::ReadHead::read_native<SampleReadLease> | 427491 | 3/3 | 0/0 |
| `dsp/fixed/sample_read/read_head.h` | void deluge::dsp::fixed::sample_read::ReadHead::read_native_as<2, 1, SampleReadLease> | 427491 | 30/32 | 5/6 |
| `dsp/fixed/sample_read/read_head.h` | void deluge::dsp::fixed::sample_read::ReadHead::put<1> | 66256908 | 8/8 | 0/0 |
| `dsp/fixed/sample_read/read_head.h` | auto void deluge::dsp::fixed::sample_read::ReadHead::read_native<SampleReadLease> | 23817 | 3/3 | 0/0 |
| `dsp/fixed/sample_read/read_head.h` | void deluge::dsp::fixed::sample_read::ReadHead::read_native_as<1, 1, SampleReadLease> | 23817 | 30/32 | 5/6 |
| `dsp/fixed/sample_read/read_head.h` | void deluge::dsp::fixed::sample_read::ReadHead::read_resampled<SampleReadLease> | 124941 | 21/23 | 10/12 |
| `dsp/fixed/sample_read/read_head.h` | void deluge::dsp::fixed::sample_read::ReadHead::read_sinc_zeros<SampleReadLease> | 1639 | 23/23 | 13/14 |
| `dsp/fixed/sample_read/read_head.h` | void deluge::dsp::fixed::sample_read::ReadHead::zero_step<SampleReadLease> | 11370 | 19/19 | 6/8 |
| `dsp/fixed/sample_read/read_head.h` | void deluge::dsp::fixed::sample_read::ReadHead::read_sinc_as<2> | 120425 | 25/27 | 8/10 |
| `dsp/fixed/sample_read/read_head.h` | void deluge::dsp::fixed::sample_read::ReadHead::read_sinc_as<2> | 14605535 | 16/17 | 4/4 |
| `dsp/fixed/sample_read/read_head.h` | void deluge::dsp::fixed::sample_read::ReadHead::sinc_feed<2> | 14469923 | 18/21 | 5/6 |
| `dsp/fixed/sample_read/read_head.h` | void deluge::dsp::fixed::sample_read::ReadHead::read_sinc_as<1> | 275 | 25/27 | 5/10 |
| `dsp/fixed/sample_read/read_head.h` | void deluge::dsp::fixed::sample_read::ReadHead::read_sinc_as<1> | 33918 | 16/17 | 2/2 |
| `dsp/fixed/sample_read/read_head.h` | void deluge::dsp::fixed::sample_read::ReadHead::sinc_feed<1> | 33643 | 18/21 | 4/6 |
| `dsp/fixed/sample_read/read_head.h` | void deluge::dsp::fixed::sample_read::ReadHead::by_layout<void deluge::dsp::fixed::sample_read::ReadHead::read_resampled<SampleReadLease> | 2602 | 13/13 | 3/4 |
| `dsp/fixed/sample_read/read_head.h` | auto void deluge::dsp::fixed::sample_read::ReadHead::read_resampled<SampleReadLease> | 0 | 3/3 | 0/0 |
| `dsp/fixed/sample_read/read_head.h` | void deluge::dsp::fixed::sample_read::ReadHead::read_linear_as<2, 2> | 0 | 34/36 | 0/8 |
| `dsp/fixed/sample_read/read_head.h` | void deluge::dsp::fixed::sample_read::ReadHead::read_linear_as<2, 2> | 0 | 13/13 | 0/0 |
| `dsp/fixed/sample_read/read_head.h` | void deluge::dsp::fixed::sample_read::ReadHead::LinearTaps::step<2> | 188486 | 27/27 | 6/6 |
| `dsp/fixed/sample_read/read_head.h` | auto void deluge::dsp::fixed::sample_read::ReadHead::read_resampled<SampleReadLease> | 1640 | 3/3 | 0/0 |
| `dsp/fixed/sample_read/read_head.h` | void deluge::dsp::fixed::sample_read::ReadHead::read_linear_as<2, 1> | 1640 | 34/36 | 5/8 |
| `dsp/fixed/sample_read/read_head.h` | void deluge::dsp::fixed::sample_read::ReadHead::read_linear_as<2, 1> | 190126 | 13/13 | 0/0 |
| `dsp/fixed/sample_read/read_head.h` | auto void deluge::dsp::fixed::sample_read::ReadHead::read_resampled<SampleReadLease> | 962 | 3/3 | 0/0 |
| `dsp/fixed/sample_read/read_head.h` | void deluge::dsp::fixed::sample_read::ReadHead::read_linear_as<1, 1> | 962 | 34/36 | 5/8 |
| `dsp/fixed/sample_read/read_head.h` | void deluge::dsp::fixed::sample_read::ReadHead::read_linear_as<1, 1> | 115968 | 13/13 | 0/0 |
| `dsp/fixed/sample_read/read_head.h` | void deluge::dsp::fixed::sample_read::ReadHead::LinearTaps::step<1> | 115006 | 27/27 | 6/6 |
| `dsp/fixed/sample_read/read_head.h` | void deluge::dsp::fixed::sample_read::ReadHead::read_linear_zeros<SampleReadLease> | 0 | 0/15 | 0/10 |
| `deluge/model/sample/sample_read_lease.cpp` | SampleReadLease::~SampleReadLease | 11529 | 6/6 | 2/2 |
| `deluge/model/sample/sample_read_lease.cpp` | SampleReadLease::SampleReadLease | 0 | 0/4 | 0/0 |
| `deluge/model/sample/sample_read_lease.cpp` | SampleReadLease::operator= | 10247 | 15/17 | 3/4 |
| `deluge/model/sample/sample_read_lease.cpp` | SampleReadLease::bind | 1724049 | 6/6 | 0/0 |
| `deluge/model/sample/sample_read_lease.cpp` | SampleReadLease::frame_bytes | 4552893 | 3/3 | 0/0 |
| `deluge/model/sample/sample_read_lease.cpp` | SampleReadLease::byte_of | 1719386 | 3/3 | 0/0 |
| `deluge/model/sample/sample_read_lease.cpp` | SampleReadLease::channels | 1564357 | 3/3 | 0/0 |
| `deluge/model/sample/sample_read_lease.cpp` | SampleReadLease::byte_depth | 1120232 | 3/3 | 0/0 |
| `deluge/model/sample/sample_read_lease.cpp` | SampleReadLease::length_frames | 11920 | 3/3 | 0/0 |
| `deluge/model/sample/sample_read_lease.cpp` | SampleReadLease::run | 972664 | 14/16 | 3/4 |
| `deluge/model/sample/sample_read_lease.cpp` | SampleReadLease::ensure_source | 14549 | 14/18 | 5/6 |
| `deluge/model/sample/sample_read_lease.cpp` | SampleReadLease::seat_chunk | 14269 | 13/17 | 2/6 |
| `deluge/model/sample/sample_read_lease.cpp` | SampleReadLease::move_to | 19978 | 14/20 | 4/6 |
| `deluge/model/sample/sample_read_lease.cpp` | SampleReadLease::let_go | 44318 | 6/6 | 2/2 |
| `deluge/model/sample/sample_read_lease.cpp` | SampleReadLease::span | 1854071 | 36/36 | 16/16 |
| `deluge/model/sample/sample_read_lease.cpp` | SampleReadLease::copy_without_cursor | 1241 | 12/12 | 1/2 |
| `deluge/model/sample/sample_read_lease.cpp` | SampleReadLease::copy_with_cursor | 1 | 7/7 | 1/2 |
| `deluge/model/sample/sample_read_lease.cpp` | SampleReadLease::reacquire | 2349 | 7/19 | 2/6 |
| `deluge/model/sample/sample_read_lease.cpp` | SampleReadLease::resync_to | 277 | 12/14 | 2/4 |
| `deluge/model/sample/sample_read_lease.cpp` | SampleReadLease::probe_chunk | 2 | 6/6 | 0/0 |
| `deluge/model/sample/sample_read_lease.cpp` | SampleReadLease::chunk_state | 1 | 3/3 | 0/0 |
| `deluge/model/sample/sample_read_lease.cpp` | SampleReadLease::unseat | 3339 | 3/3 | 0/0 |
| `deluge/model/sample/sample_read_lease.cpp` | SampleReadLease::prefetch | 726748 | 9/11 | 3/4 |
| `deluge/model/voice/voice_sample_stretch_port.cpp` | VoiceSampleStretchPort::facts | 30057 | 24/24 | 4/4 |
| `deluge/model/voice/voice_sample_stretch_port.cpp` | VoiceSampleStretchPort::synced_position | 8985 | 6/6 | 3/4 |
| `deluge/model/voice/voice_sample_stretch_port.cpp` | VoiceSampleStretchPort::run | 5328124 | 16/18 | 3/4 |
| `deluge/model/voice/voice_sample_stretch_port.cpp` | VoiceSampleStretchPort::perc_at | 8824 | 3/3 | 0/0 |
| `deluge/model/voice/voice_sample_stretch_port.cpp` | VoiceSampleStretchPort::fill_perc | 20986 | 3/3 | 0/0 |
| `deluge/model/voice/voice_sample_stretch_port.cpp` | VoiceSampleStretchPort::slot | 327026 | 3/3 | 2/2 |
| `deluge/model/voice/voice_sample_stretch_port.cpp` | VoiceSampleStretchPort::bound | 316801 | 4/4 | 0/0 |
| `deluge/model/voice/voice_sample_stretch_port.cpp` | VoiceSampleStretchPort::shadow | 1240 | 6/6 | 0/0 |
| `deluge/model/voice/voice_sample_stretch_port.cpp` | VoiceSampleStretchPort::seat_and_swap | 9006 | 16/22 | 7/10 |
| `deluge/model/voice/voice_sample_stretch_port.cpp` | VoiceSampleStretchPort::read | 307795 | 16/16 | 4/4 |
| `deluge/model/voice/voice_sample_stretch_port.cpp` | VoiceSampleStretchPort::mark | 8985 | 7/7 | 2/2 |
| `deluge/model/voice/voice_sample_stretch_port.cpp` | VoiceSampleStretchPort::cursors_meet | 738 | 8/11 | 2/4 |
| `deluge/model/voice/voice_sample_stretch_port.cpp` | VoiceSampleStretchPort::keep | 29854 | 12/12 | 2/2 |
| `deluge/model/voice/voice_sample_stretch_port.cpp` | VoiceSampleStretchPort::let_go | 12971 | 6/6 | 2/2 |
| `deluge/model/voice/voice_sample_stretch_port.cpp` | VoiceSampleStretchPort::hold | 6 | 3/3 | 2/2 |
| `deluge/model/voice/voice_sample_stretch_port.cpp` | VoiceSampleStretchPort::drop | 6 | 6/6 | 3/4 |
| `deluge/model/voice/voice_sample_stretch_port.cpp` | voice_sample_stretch_port.cpp:frameBytes | 5395734 | 3/3 | 0/0 |
| `deluge/model/voice/voice_sample_stretch_port.cpp` | voice_sample_stretch_port.cpp:frameOf | 37553 | 4/4 | 0/0 |
| `deluge/model/voice/voice_sample_stretch_port.cpp` | voice_sample_stretch_port.cpp:reservationOf | 29854 | 3/3 | 0/0 |
| `dsp/fixed/sample_read/read_head.h` | deluge::dsp::fixed::sample_read::Residency deluge::dsp::fixed::sample_read::ReadHead::seat<SampleReadLease> | 9006 | 8/8 | 1/2 |
| `dsp/fixed/sample_read/read_head.h` | bool deluge::dsp::fixed::sample_read::ReadHead::read<SampleReadLease> | 307795 | 23/23 | 6/6 |
| `deluge/processing/engines/audio_engine.cpp` | AudioEngine::cullVoices | 535684 | 27/41 | 11/20 |
| `deluge/processing/engines/audio_engine.cpp` | AudioEngine::renderAudio | 538905 | 33/33 | 8/8 |
| `deluge/processing/engines/audio_engine.cpp` | AudioEngine::renderAudioForStemExport | 0 | 0/38 | 0/14 |
| `deluge/processing/engines/audio_engine.cpp` | AudioEngine::solicitStretcher | 1352 | 5/7 | 0/0 |
| `deluge/processing/engines/audio_engine.cpp` | AudioEngine::stretcherUnassigned | 1352 | 3/3 | 0/0 |
| `deluge/processing/engines/audio_engine.cpp` | AudioEngine::setDireness | 535712 | 14/23 | 6/10 |
| `synth/sources/sample/sample_stretcher.cpp` | deluge::synth::SampleStretcher::newer_reading | 214093 | 4/4 | 6/6 |
| `synth/sources/sample/sample_stretcher.cpp` | deluge::synth::SampleStretcher::sounding | 12590 | 3/3 | 0/0 |
| `synth/sources/sample/sample_stretcher.cpp` | deluge::synth::SampleStretcher::heads | 203761 | 14/14 | 8/8 |
| `synth/sources/sample/sample_stretcher.cpp` | deluge::synth::SampleStretcher::set_heads | 212747 | 18/18 | 12/12 |
| `synth/sources/sample/sample_stretcher.cpp` | deluge::synth::SampleStretcher::give_back_tail | 10337 | 8/8 | 4/4 |
| `synth/sources/sample/sample_stretcher.cpp` | deluge::synth::SampleStretcher::keep_hop_back | 10134 | 7/9 | 1/2 |
| `synth/sources/sample/sample_stretcher.cpp` | deluge::synth::SampleStretcher::begin | 1352 | 40/49 | 12/18 |
| `synth/sources/sample/sample_stretcher.cpp` | deluge::synth::SampleStretcher::remember | 1283 | 9/11 | 5/6 |
| `synth/sources/sample/sample_stretcher.cpp` | deluge::synth::SampleStretcher::prepare | 196963 | 21/21 | 8/8 |
| `synth/sources/sample/sample_stretcher.cpp` | deluge::synth::SampleStretcher::hop | 8985 | 82/96 | 27/30 |
| `synth/sources/sample/sample_stretcher.cpp` | deluge::synth::SampleStretcher::read_heads | 203761 | 29/29 | 17/18 |
| `synth/sources/sample/sample_stretcher.cpp` | deluge::synth::SampleStretcher::render | 195394 | 33/35 | 17/18 |
| `synth/sources/sample/sample_stretcher.cpp` | deluge::synth::SampleStretcher::wrap_to | 260 | 7/7 | 2/2 |
| `synth/sources/sample/sample_stretcher.cpp` | deluge::synth::SampleStretcher::end | 1352 | 15/15 | 4/4 |
| `synth/sources/sample/sample_stretcher.cpp` | deluge::synth::SampleStretcher::may_end | 738 | 8/8 | 0/0 |
| `synth/sources/sample/stretch_ratio.h` | deluge::synth::ratio_undoing_pitch | 38 | 5/5 | 1/2 |
| `synth/sources/sample/sample_stretcher.cpp` | deluge::synth::SampleStretcher::give_back_tail (lambda) | 1054 | 6/6 | 2/2 |
| `dsp/fixed/timestretch/hop_tuning.h` | deluge::dsp::fixed::timestretch::hop_shape | 19119 | 10/10 | 0/0 |
| `dsp/fixed/timestretch/hop_tuning.h` | deluge::dsp::fixed::timestretch::hop_shape | 95595 | 3/3 | 0/0 |
| `synth/sources/sample/sample_stretcher.cpp` | deluge::synth::frame_of | 28666 | 4/4 | 0/0 |
| `synth/sources/sample/sample_stretcher.cpp` | deluge::synth::frame_bytes | 37651 | 3/3 | 0/0 |
| `dsp/fixed/timestretch/hop_planner.h` | deluge::dsp::fixed::timestretch::repitched | 17648 | 3/3 | 0/0 |
| `dsp/fixed/timestretch/hop_planner.h` | deluge::dsp::fixed::timestretch::best_width | 461057 | 1/1 | 0/0 |
| `synth/sources/sample/sample_stretcher.cpp` | deluge::synth::SampleStretcher::hop (lambda) | 16602 | 1/1 | 0/0 |
| `synth/sources/sample/sample_stretcher.cpp` | deluge::synth::PortFrames::PortFrames | 8834 | 1/1 | 0/0 |
| `dsp/fixed/timestretch/waveform_aligner.h` | deluge::dsp::fixed::timestretch::align_setup | 8834 | 6/6 | 0/0 |
| `dsp/fixed/timestretch/waveform_aligner.h` | deluge::dsp::fixed::timestretch::search_limit | 8068 | 5/5 | 0/0 |
| `dsp/fixed/timestretch/waveform_aligner.h` | deluge::dsp::fixed::timestretch::aligned_start | 7869 | 6/6 | 4/4 |
| `dsp/fixed/timestretch/waveform_aligner.h` | std::optional<std::array<int, 3ul>> deluge::dsp::fixed::timestretch::moving_averages<deluge::synth::PortFrames> | 16902 | 28/34 | 18/22 |
| `synth/sources/sample/sample_stretcher.cpp` | deluge::synth::PortFrames::length_frames | 28259 | 1/1 | 0/0 |
| `dsp/fixed/timestretch/waveform_aligner.h` | deluge::dsp::fixed::timestretch::averages_read | 24771 | 5/5 | 0/0 |
| `synth/sources/sample/sample_stretcher.cpp` | deluge::synth::PortFrames::run | 5311522 | 3/3 | 0/0 |
| `dsp/fixed/timestretch/waveform_aligner.h` | deluge::dsp::fixed::timestretch::FoldedFrames<deluge::synth::PortFrames>::FoldedFrames | 23806 | 1/1 | 0/0 |
| `dsp/fixed/timestretch/waveform_aligner.h` | deluge::dsp::fixed::timestretch::FoldedFrames<deluge::synth::PortFrames>::operator | 20784751 | 18/22 | 19/20 |
| `dsp/fixed/timestretch/waveform_aligner.h` | std::optional<deluge::dsp::fixed::timestretch::Alignment> deluge::dsp::fixed::timestretch::align<deluge::synth::PortFrames> | 8068 | 27/29 | 7/8 |
| `dsp/fixed/timestretch/waveform_aligner.h` | deluge::dsp::fixed::timestretch::PlayOrderReader<deluge::synth::PortFrames>::PlayOrderReader | 7869 | 1/1 | 0/0 |
| `dsp/fixed/timestretch/waveform_search.h` | deluge::dsp::fixed::timestretch::SearchResult deluge::dsp::fixed::timestretch::search<deluge::dsp::fixed::timestretch::PlayOrderReader<deluge::synth::PortFrames>> | 7869 | 3/3 | 0/0 |
| `dsp/fixed/timestretch/waveform_search.h` | deluge::dsp::fixed::timestretch::search_detail::Run<deluge::dsp::fixed::timestretch::PlayOrderReader<deluge::synth::PortFrames>>::Run | 7869 | 4/4 | 1/2 |
| `dsp/fixed/timestretch/waveform_search.h` | deluge::dsp::fixed::timestretch::search_detail::Run<deluge::dsp::fixed::timestretch::PlayOrderReader<deluge::synth::PortFrames>>::result | 7869 | 18/20 | 8/10 |
| `dsp/fixed/timestretch/waveform_search.h` | deluge::dsp::fixed::timestretch::search_detail::Run<deluge::dsp::fixed::timestretch::PlayOrderReader<deluge::synth::PortFrames>>::pass | 18991 | 35/37 | 24/26 |
| `dsp/fixed/timestretch/waveform_aligner.h` | deluge::dsp::fixed::timestretch::PlayOrderReader<deluge::synth::PortFrames>::reach | 18991 | 5/5 | 2/2 |
| `dsp/fixed/timestretch/waveform_search.h` | deluge::dsp::fixed::timestretch::search_detail::Run<deluge::dsp::fixed::timestretch::PlayOrderReader<deluge::synth::PortFrames>>::slide | 4776247 | 14/16 | 7/8 |
| `dsp/fixed/timestretch/waveform_aligner.h` | deluge::dsp::fixed::timestretch::PlayOrderReader<deluge::synth::PortFrames>::folded | 19104988 | 1/1 | 0/0 |
| `dsp/fixed/timestretch/waveform_search.h` | deluge::dsp::fixed::timestretch::search_detail::Run<deluge::dsp::fixed::timestretch::PlayOrderReader<deluge::synth::PortFrames>>::flipped | 20339 | 15/15 | 10/10 |
| `synth/sources/sample/sample_stretcher.cpp` | deluge::synth::PortFrames::channels | 0 | 0/1 | 0/0 |
| `dsp/fixed/timestretch/waveform_search.h` | deluge::dsp::fixed::timestretch::SearchResult deluge::dsp::fixed::timestretch::search<deluge::dsp::fixed::pitch_shift::WaveformAligner::RingReader<deluge::dsp::Fixed<int, 31>>> | 539 | 3/3 | 0/0 |
| `dsp/fixed/timestretch/waveform_search.h` | deluge::dsp::fixed::timestretch::search_detail::Run<deluge::dsp::fixed::pitch_shift::WaveformAligner::RingReader<deluge::dsp::Fixed<int, 31>>>::Run | 539 | 4/4 | 2/2 |
| `dsp/fixed/timestretch/waveform_search.h` | deluge::dsp::fixed::timestretch::search_detail::Run<deluge::dsp::fixed::pitch_shift::WaveformAligner::RingReader<deluge::dsp::Fixed<int, 31>>>::result | 539 | 18/20 | 8/10 |
| `dsp/fixed/timestretch/waveform_search.h` | deluge::dsp::fixed::timestretch::search_detail::Run<deluge::dsp::fixed::pitch_shift::WaveformAligner::RingReader<deluge::dsp::Fixed<int, 31>>>::pass | 775 | 35/37 | 25/26 |
| `dsp/fixed/timestretch/waveform_search.h` | deluge::dsp::fixed::timestretch::search_detail::Run<deluge::dsp::fixed::pitch_shift::WaveformAligner::RingReader<deluge::dsp::Fixed<int, 31>>>::slide | 61945 | 14/16 | 7/8 |
| `dsp/fixed/timestretch/waveform_search.h` | deluge::dsp::fixed::timestretch::search_detail::Run<deluge::dsp::fixed::pitch_shift::WaveformAligner::RingReader<deluge::dsp::Fixed<int, 31>>>::flipped | 1055 | 15/15 | 10/10 |
| `dsp/fixed/timestretch/waveform_search.h` | deluge::dsp::fixed::timestretch::SearchResult deluge::dsp::fixed::timestretch::search<deluge::dsp::fixed::pitch_shift::WaveformAligner::RingReader<deluge::dsp::StereoSample>> | 35 | 3/3 | 0/0 |
| `dsp/fixed/timestretch/waveform_search.h` | deluge::dsp::fixed::timestretch::search_detail::Run<deluge::dsp::fixed::pitch_shift::WaveformAligner::RingReader<deluge::dsp::StereoSample>>::Run | 35 | 4/4 | 1/2 |
| `dsp/fixed/timestretch/waveform_search.h` | deluge::dsp::fixed::timestretch::search_detail::Run<deluge::dsp::fixed::pitch_shift::WaveformAligner::RingReader<deluge::dsp::StereoSample>>::result | 35 | 18/20 | 5/10 |
| `dsp/fixed/timestretch/waveform_search.h` | deluge::dsp::fixed::timestretch::search_detail::Run<deluge::dsp::fixed::pitch_shift::WaveformAligner::RingReader<deluge::dsp::StereoSample>>::pass | 51 | 35/37 | 22/26 |
| `dsp/fixed/timestretch/waveform_search.h` | deluge::dsp::fixed::timestretch::search_detail::Run<deluge::dsp::fixed::pitch_shift::WaveformAligner::RingReader<deluge::dsp::StereoSample>>::slide | 4406 | 14/16 | 7/8 |
| `dsp/fixed/timestretch/waveform_search.h` | deluge::dsp::fixed::timestretch::search_detail::Run<deluge::dsp::fixed::pitch_shift::WaveformAligner::RingReader<deluge::dsp::StereoSample>>::flipped | 70 | 15/15 | 9/10 |

`AudioEngine::cullVoices`'s counts after its early return read as a wrapped 64-bit value: the profile counters are not
atomic, and the harness renders on more than one thread, so a counter computed as a difference went below 0. Its
lines there count as run in the table above; they never ran (the render always fits its period).

## Untaken sides

### `src/deluge/model/clip/audio_clip.cpp`

**AudioClip::processCurrentPos**

| Condition | Side not taken | True / false | Category | Why | Pinned by |
|---|---|---|---|---|---|
| `modelStack->getTimelineCounter() != this` | true | 0 / 10222 | REC | the clip replaced while it processes its position: an arrangement take (arrangement-take, which compares no audio, reached it) |  |
| `recorder` | true | 0 / 10222 | REC | — |  |
| `recorder->status == RecorderStatus::ABORTED` | both | 0 / 0 | REC | — |  |
| `getCurrentlyRecordingLinearly()` (first of 2) | true | 0 / 8101 | REC | pos 0 only at a linear recording's restart |  |
| `lastProcessedPos == nextSampleRestartPos` | both | 0 / 0 | REC | — |  |
| `getCurrentlyRecordingLinearly()` (second of 2) | true | 0 / 2121 | REC | — |  |
| `originalLength != 0` | both | 0 / 0 | REC | — |  |
| `originalLength < playbackHandler.swungTicksTilNextEvent` | both | 0 / 0 | REC | — |  |
| `sampleHolder.audioFile` | false | 2121 / 0 | DEF | a clip with no file or an unplayable one; every generated song's samples load |  |
| `!((Sample*)sampleHolder.audioFile)->unplayable` | false | 2121 / 0 | DEF | as above |  |
| `((AudioOutput*)output)->envelope.stage() >= Envelope::Stage::RELEASE` | true | 0 / 1957 | LATE | the envelope releasing at the clip's start, after a restart part-way | decision 10 |
| `!doingLateStart` | both | 0 / 0 | LATE | — | decision 10 |
| `!voiceSample` | true | 0 / 164 | MEM | the voice pool empty | decision 11 (accepted: no fault record) |
| `outcome != RegionOutcome::Ready` | true | 0 / 1470 | RES | the clip's start seat (setupClusersForInitialPlay) loading or unavailable | decision 11 (accepted: no fault record) |

**AudioClip::resumePlayback**

| Condition | Side not taken | True / false | Category | Why | Pinned by |
|---|---|---|---|---|---|
| `!playbackHandler.isEitherClockActive()` | true | 0 / 6 | EDIT | resumePlayback runs at an arrangement instance's start or after a clip-length edit (Song::setClipLength); a resume with the clocks stopped or the clip inactive | decision 10 |
| `!modelStack->song->isClipActive(this)` | true | 0 / 6 | EDIT | as above | decision 10 |
| `!sampleHolder.audioFile` | true | 0 / 6 | DEF | an unplayable sample |  |
| `((Sample*)sampleHolder.audioFile)->unplayable` | true | 0 / 6 | DEF | as above |  |
| `voiceSample->cache` | true | 0 / 2 | EDIT | a clip-length edit while its live voice reads a cache: the edit songs' voices stretch without one | decision 10 |
| `!success` | both | 0 / 0 | EDIT | as above | decision 10 |
| `sequenceSyncStartedNumTicksAgo < 0` | true | 0 / 6 | DEF | 'Shouldn't happen' |  |
| `!mayMakeSound` | true | 0 / 6 | REC | a resume of a clip that may not sound: an arrangement take, as `processCurrentPos`'s replaced-clip check |  |
| `voiceSample->stretching()` | false | 2 / 0 | EDIT | a clip-length edit while the clip's live voice plays unstretched | decision 10 |
| `((AudioOutput*)output)->envelope.stage() < Envelope::Stage::RELEASE` | false | 2 / 0 | EDIT | a clip-length edit while the clip's envelope releases | decision 10 |
| `voiceSample` | true | 0 / 4 | EDIT | as the two above: the stretching, non-releasing voice returns before it | decision 10 |
| `!voiceSample` | true | 0 / 4 | MEM | the voice pool empty | decision 11 (accepted: no fault record) |

**AudioClip::render**

| Condition | Side not taken | True / false | Category | Why | Pinned by |
|---|---|---|---|---|---|
| `!voiceSample` | true | 0 / 223334 | DEF | AudioOutput renders the clip only while it has a voice |  |
| `static_cast<AudioOutput*>(this->output)->envelope.stage() < Envelope::Stage::FAST_RELEASE` | false | 2 / 0 | LATE | a late start while the clip's previous sound fades out: a second <> + play in quick succession | decision 10 |
| `result == LateStartAttemptStatus::FAILURE` | true | 0 / 1 | RES | the late start failing, not just waiting: its start chunk unavailable (attemptLateSampleStart's probe found or made none); render-audio-stretch-late takes the wait | decision 11 (accepted: no fault record) |
| `!playbackHandler.isEitherClockActive()` | true | 0 / 16409 | STOP | a near-native clip rendering with the clocks stopped: a stop's fast release ends within its first 128-frame block, so the voice is gone before it renders (see STOP); `doingLateStart` is LATE's | STOP ruling |
| `doingLateStart` | true | 0 / 16409 | LATE | — | decision 10 |
| `playbackHandler.isEitherClockActive()` | false | 6770 / 0 | STOP | a tied clip without pitch change rendering with the clocks stopped, as above | STOP ruling |
| `!doingLateStart` | false | 6770 / 0 | LATE | — | decision 10 |
| `playbackHandler.playbackState` | false | 223333 / 0 | STOP | playbackState false: a clip rendering after playback stopped (arrangement-take, which compares no audio, reached it) | STOP ruling |
| `!success` | true | 0 / 112 | RES | the fudge's begin failing (its seat or chunk walk not ready, H1) or its reassessment; a missing stretcher returns true and keeps the voice | kSeatFaults |
| `cutPos < 2147483647` | false | 14 / 0 | NS | a clip that neither loops nor ever cuts; not a stretcher path |  |
| `!everythingOk` | true | 0 / 1470 | RES | possiblySetUpCache fails only when its reassessment (the lease's reacquire) finds the chunk not ready; a failed cache allocation leaves no cache and returns true | decision 11 (accepted: no fault record) |

**AudioClip::getCullImmunity**

| Condition | Side not taken | True / false | Category | Why | Pinned by |
|---|---|---|---|---|---|
| `voiceSample` | both | 0 / 0 | CULL | — | kCullFaults |
| `voiceSample->stretching()` | both | 0 / 0 | CULL | the immunity's stretching term, read only when a voice is culled | kCullFaults |

### `src/deluge/model/sample/sample.cpp`

**Sample::fillPercCache**

| Condition | Side not taken | True / false | Category | Why | Pinned by |
|---|---|---|---|---|---|
| `reversed` | true | 0 / 20986 | DEF | the whole early-out condition (start already past the waveform in the fill's direction): the perc fill memo (P4) asks no fill once one reports filledToEndOfWaveform |  |
| `!percCacheClusters[reversed]` | true | 0 / 2 | MEM | — | decision 11 (accepted: no fault record) |
| `(mgr != nullptr)` | false | 2 / 0 | DEF | no resource manager |  |
| `percCacheAssetId[reversed] == DELUGE_RESOURCE_NO_ASSET` | true | 0 / 2 | DEF | RPC1: asset table full |  |
| `isProjectReferenced()` | false | 2 / 0 | EDIT | a long sample not referenced by the project: auditioned from the browser | decision 10 |
| `!percCacheMemory[reversed]` | true | 0 / 100 | MEM | — | kAllocationFaults |
| `percClusterIndexStart >= numPercCacheClusters` | true | 0 / 1275 | DEF | E138 |  |
| `!clusterHere` | true | 0 / 1275 | MEM | a perc-cache cluster stolen | decision 11 (accepted: no fault record) |
| `startPosSamples & ((1 << (Cluster::size_magnitude + kPercBufferReductionMagnitude)) - 1)` | both | 0 / 0 | MEM | as above | decision 11 (accepted: no fault record) |
| `clusterHere` | false | 1275 / 0 | MEM | as above | decision 11 (accepted: no fault record) |
| `percClusterIndexEnd >= numPercCacheClusters` | true | 0 / 2 | DEF | E140 |  |
| `!percCacheClusters[reversed][percClusterIndexEnd]` | true | 0 / 2 | MEM | E141, a stolen cluster | decision 11 (accepted: no fault record) |
| `numSamples <= 0` | true | 0 / 19720 | DEF | nothing to fill |  |
| `sourceClusterIndex >= getFirstClusterIndexWithNoAudioData()` | true | 0 / 21043 | DEF | a source cluster outside the audio data |  |
| `sourceClusterIndex < getFirstClusterIndexWithAudioData()` | true | 0 / 21043 | DEF | as above |  |
| `percClusterIndex >= numPercCacheClusters` | true | 0 / 1308 | DEF | perc-cache cluster index past the end |  |
| `p == nullptr` | true | 0 / 4 | MEM | a perc-cache chunk's allocation fails (deluge_resource_request) | decision 11 (accepted: no fault record) |
| `window.frames == nullptr` | true | 0 / 21043 | RES | — | decision 11 (accepted: no fault record) |
| `percCacheZone->endPos == percCacheZone->startPos` | true | 0 / 19720 | DEF | an empty zone |  |

**Sample::perc_at**

| Condition | Side not taken | True / false | Category | Why | Pinned by |
|---|---|---|---|---|---|
| `i < 0` | true | 0 / 8824 | MEM | no perc zone at or before the hop's position (forwards): every fill at it failed to allocate or a stolen perc chunk trimmed its zone (else RES, the fill's source frames not resident); each block's fill starts at the position | decision 11 (accepted: no fault record) |
| `i >= static_cast<int32_t>(percCacheZones[reversed].size())` | true | 0 / 8824 | MEM | as above, reversed (the search answers past the last zone) | decision 11 (accepted: no fault record) |
| `!percCacheClusters[reversed][ourCluster]` | true | 0 / 428 | DEF | E142: a stolen perc chunk's zones are trimmed as it is stolen (percCacheClusterStolen), so a found zone's chunk is present |  |

### `src/deluge/model/sample/sample_read_lease.cpp`

**SampleReadLease::operator=**

| Condition | Side not taken | True / false | Category | Why | Pinned by |
|---|---|---|---|---|---|
| `this == &other` | true | 0 / 10247 | DEF | every move-assignment takes a temporary: `SampleReadLease{}` (seat_and_swap), `copy_without_cursor()` (shadow) or `copy_with_cursor()` (cloneVoiceSampleFrom) |  |

**SampleReadLease::run**

| Condition | Side not taken | True / false | Category | Why | Pinned by |
|---|---|---|---|---|---|
| `!holds_chunk()` | true | 0 / 972664 | DEF | every caller holds a chunk: tap from back_fill (span tested), forward_fill and push_frame (after a true walk), read_native_as after check_native's walk, read_resampled behind its span test |  |

**SampleReadLease::ensure_source**

| Condition | Side not taken | True / false | Category | Why | Pinned by |
|---|---|---|---|---|---|
| `source_backing_ != backing` | true | 0 / 4901 | DEF | a lease opens its source once per voice life: VoiceSamplePool::recycle runs the destructor, which closes it, and no voice moves to another sample |  |

**SampleReadLease::seat_chunk**

| Condition | Side not taken | True / false | Category | Why | Pinned by |
|---|---|---|---|---|---|
| `source_ == nullptr` | true | 0 / 14269 | MEM | the source pool exhausted: `deluge_sample_source_open` returns null | decision 11 (accepted: no fault record) |
| `outcome != RegionOutcome::Ready` | true | 0 / 14269 | RES | a seat or restart whose chunk is not ready (`acquire_ex` not READY) | kSeatFaults, kReadHeadFaults |
| `outcome == RegionOutcome::Loading` | both | 0 / 0 | RES | inside the not-ready seat above: LOADING (a card read pending) or UNAVAILABLE (a card error) | kReadHeadFaults, kSeatFaults |

**SampleReadLease::move_to**

| Condition | Side not taken | True / false | Category | Why | Pinned by |
|---|---|---|---|---|---|
| `index != chunk_index() + d` | true | 0 / 8058 | DEF | holding a chunk, the target is always adjacent: the walk moves to R6's boundary frame, whose byte is in chunk index ± 1, and every place lets go first (seat_and_swap's fresh lease, setupClusersForInitialPlay's let_go, the walk restart's let_go, the late start's unassignAllReasons) |  |
| `!deluge_sample_region_acquire(source_, static_cast<uint32_t>(index), static_cast<int8_t>(direction), static_cast<uint32_t>(priority_), &region)` | true | 0 / 8058 | RES | the next chunk not ready at the walk's move; also the shadow copy (copy_without_cursor, no source) at its first chunk boundary (R10's quirk), which no golden's plain begin reaches | kReadHeadFaults |

**SampleReadLease::copy_without_cursor**

| Condition | Side not taken | True / false | Category | Why | Pinned by |
|---|---|---|---|---|---|
| `copy.region_.lease != 0` | false | 1241 / 0 | OPEN | a copy of a lease holding no chunk: a plain begin's shadow copy, or a unison part cloned (cloneVoiceSampleFrom), while the voice plays its tail zeros past a non-looping end | accepted (the app's: no golden builds it) |

**SampleReadLease::copy_with_cursor**

| Condition | Side not taken | True / false | Category | Why | Pinned by |
|---|---|---|---|---|---|
| `copy.sample_ != nullptr` | false | 1 / 0 | DEF | the clone copies a sounding voice's lease, which its seat bound (`VoiceSample::lease` binds before setupClusersForInitialPlay's and the late start's place) |  |

**SampleReadLease::reacquire**

| Condition | Side not taken | True / false | Category | Why | Pinned by |
|---|---|---|---|---|---|
| `!holds_chunk()` | true | 0 / 2349 | EDIT | a reassessment of a head holding no chunk: a start marker moved (`sampleZoneChanged`) or a cache dropped during its tail past the end | decision 10 |
| `(index - final_index) * static_cast<int32_t>(direction) > 0` | true | 0 / 2349 | EDIT | the lease past its final chunk after a cache read is abandoned mid-note (a tempo or pitch change, a reversal, a marker edit moving the final chunk) | decision 10 |
| `!deluge_sample_region_acquire(source_, static_cast<uint32_t>(final_index), static_cast<int8_t>(direction), 1u, &final_region)` | both | 0 / 0 | EDIT | inside the past-the-final-chunk branch above; its true side is also the final chunk not ready | decision 10 |

**SampleReadLease::resync_to**

| Condition | Side not taken | True / false | Category | Why | Pinned by |
|---|---|---|---|---|---|
| `deluge_sample_region_state(source_, static_cast<uint32_t>(chunk_index)) == DELUGE_REGION_LOADING` | true | 0 / 277 | RES | the cache resync's chunk has a reservation in flight | decision 11 (accepted: no fault record) |
| `deluge_sample_region_acquire_ex(source_, static_cast<uint32_t>(chunk_index), static_cast<int8_t>(direction), 0u, &region) == DELUGE_REGION_READY` | false | 277 / 0 | RES | the cache resync's chunk not ready | decision 11 (accepted: no fault record) |

**SampleReadLease::prefetch**

| Condition | Side not taken | True / false | Category | Why | Pinned by |
|---|---|---|---|---|---|
| `!holds_chunk()` | true | 0 / 726748 | OPEN | a head reading over an empty lease at a render's start: only a hop seat outside the data (place's range check leaves the spare's cursor as it was, and seat_and_swap has reset its lease), see place below | accepted (the app's: no golden builds it) |

### `src/deluge/model/voice/voice_sample.cpp`

**VoiceSample::attemptLateSampleStart**

| Condition | Side not taken | True / false | Category | Why | Pinned by |
|---|---|---|---|---|---|
| `(int64_t)(startAtByte - voiceSource->endPlaybackAtByte) * voiceSource->playDirection >= 0` | true | 0 / 2 | LATE | a late start past the sample's end: playback started part-way through a clip beyond where its sample ends | decision 10 |
| `(int64_t)(startAtByte - voiceSource->startPlaybackAtByte) * voiceSource->playDirection < 0` | true | 0 / 2 | DEF | E439: the caller passes a non-negative count of samples since the start |  |
| `startAtClusterIndex >= sample->getFirstClusterIndexWithNoAudioData()` | true | 0 / 2 | DEF | E366: the end check before it keeps the start byte inside the audio data |  |
| `voiceSource->playDirection == 1` (first of 3) | false | 2 / 0 | LATE | a reversed clip started late: render-audio-stretch-late's clips play forwards | decision 10 |
| `state0 == DELUGE_REGION_UNAVAILABLE` | true | 0 / 2 | MEM | the start chunk unavailable: no chunk found or made (no free RAM, or the source pool exhausted); the E366 check rules out out-of-range | decision 11 (accepted: no fault record) |
| `hasNeighbour` | false | 1 / 0 | LATE | a late start in the sample's final chunk, which has no neighbour | decision 10 |
| `stateN != DELUGE_REGION_LOADING` | true | 0 / 1 | LATE | a late start whose neighbour is resident or unavailable: the late song's one ready attempt found its neighbour's prefetch still pending | decision 10 |
| `ReadHead::place(head(), start_lease, static_cast<int32_t>((startAtByte - sample->audioDataStartPosBytes) / static_cast<uint32_t>(sample->numChannels * sample->byteDepth)), voiceSource->playDirection == 1 ? Direction::FORWARD : Direction::REVERSE) != deluge::dsp::fixed::sample_read::Residency::READY` | true | 0 / 1 | DEF | the start chunk was acquired ready through the same lease just before, so the place re-acquires its pinned current |  |
| `voiceSource->playDirection == 1` (second of 3) | false | 1 / 0 | LATE | reversed late start, as above | decision 10 |
| `voiceSource->playDirection == 1` (third of 3) | false | 1 / 0 | LATE | as above | decision 10 |
| `numBytesIn < (Cluster::size >> 1)` | false | 1 / 0 | LATE | a late start more than half a chunk into its start chunk while its neighbour loads: the late song's start lands in the first half | decision 10 |

**VoiceSample::fudgeTimeStretchingToAvoidClick**

| Condition | Side not taken | True / false | Category | Why | Pinned by |
|---|---|---|---|---|---|
| `stretcher == nullptr` | true | 0 / 112 | MEM | the stretcher pool empty (solicitStretcher): the fudge is skipped and the voice kept | kAllocationFaults |
| `!stretcher->begin(port, context, position, motion, LoopType::NONE, deluge::synth::SampleStretcher::Fudge{numSamplesTilLoop})` | true | 0 / 112 | RES | the fudge's begin failing: its seat not ready or its chunk walk failing (H1) | kSeatFaults |
| `!success` | true | 0 / 112 | RES | the reassessment after the fudge's begin (the lease's reacquire) not ready | decision 11 (accepted: no fault record) |

**VoiceSample::weShouldBeTimeStretchingNow**

| Condition | Side not taken | True / false | Category | Why | Pinned by |
|---|---|---|---|---|---|
| `stretcher == nullptr` | true | 0 / 1240 | MEM | the stretcher pool empty (solicitStretcher): the voice is unassigned | kAllocationFaults |
| `!success` | true | 0 / 1240 | RES | the reassessment after the begin (the lease's reacquire) not ready | decision 11 (accepted: no fault record) |

**VoiceSample::setupClusersForInitialPlay**

| Condition | Side not taken | True / false | Category | Why | Pinned by |
|---|---|---|---|---|---|
| `sample->unplayable` | true | 0 / 1946 | DEF | `note_on` and AudioClip test it first; it is set only on a card swap, after `killAllVoices` |  |
| `offset < 0` | true | 0 / 1946 | DEF | every caller passes no overshoot, and the guide's start byte (getBytePosToStartPlayback) lies inside the audio data |  |
| `offset >= static_cast<int32_t>(sample->audioDataLengthBytes)` | true | 0 / 1946 | DEF | as above |  |
| `case deluge::dsp::fixed::sample_read::Residency::READY` | false | 1946 / 0 | RES | the initial seat (ReadHead::place) loading or unavailable, at a note-on or a clip's start | decision 11 (accepted: no fault record) |
| `case deluge::dsp::fixed::sample_read::Residency::LOADING` | true | 0 / 1946 | RES | the initial seat's chunk loading | decision 11 (accepted: no fault record) |
| `default` | true | 0 / 1946 | MEM | the initial seat unavailable: no chunk found or made, or the source pool exhausted (the range check before it rules out out-of-range) | decision 11 (accepted: no fault record) |

**VoiceSample::changeClusterIfNecessary**

| Condition | Side not taken | True / false | Category | Why | Pinned by |
|---|---|---|---|---|---|
| `!hasCurrentRegion()` | true | 0 / 16 | OPEN | the head holding no chunk when a cache is abandoned or a stretch ends: a cache written past a non-looping end (its head ended, reading tail zeros) then abandoned, or E1 ending a stretch with the newer head ended; else RES | accepted (the app's: no golden builds it) |
| `guide->playDirection == 1` | false | 16 / 0 | OPEN | a reversed voice leaving a cache or ending a stretch (E1): no golden's reversed voice does | accepted (the app's: no golden builds it) |

**VoiceSample::render**

| Condition | Side not taken | True / false | Category | Why | Pinned by |
|---|---|---|---|---|---|
| `phaseIncrement != kMaxSampleValue` (first of 2) | false | 153528 / 0 | CON | a voice sets up a cache only off its native rate |  |
| `interpolationBufferSize <= 2` | true | 0 / 153527 | CULL | an interpolation buffer of 2 or less while writing a cache: the interpolation-mode test before it short-circuits for linear, and the voice's buffer shrinks only under CPU direness | decision 11 (accepted: no fault record) |
| `writingToCache` (first of 2) | both | 0 / 0 | CULL | as above | decision 11 (accepted: no fault record) |
| `cache->timeStretchRatio != kMaxSampleValue` | false | 4 / 0 | NS | leaving an unstretched cache |  |
| `!success` (first of 8) | true | 0 / 5 | RES | — | decision 11 (accepted: no fault record) |
| `leaving` (first of 2) | false | 4 / 0 | CON | cacheWindow always returns a window (C1 copies nothing when no chunk is written ahead), never none |  |
| `!success` (second of 8) | true | 0 / 4 | RES | — | decision 11 (accepted: no fault record) |
| `cache->writeBytePos < cacheBytePos` (first of 4) | true | 0 / 153527 | MEM | a cache cluster stolen | kEvictionFaults |
| `!success` (third of 8) | both | 0 / 0 | MEM | stopUsingCache after a stolen cache chunk: the eviction record takes its continue side; its failure is RES | kEvictionFaults |
| `cache->writeBytePos > cacheBytePos` | true | 0 / 109278 | DEF | 'Could this really happen?': a second writer of the same cache |  |
| `!success` (fourth of 8) | true | 0 / 576 | RES | — | decision 11 (accepted: no fault record) |
| `leaving` (second of 2) | false | 576 / 0 | CON | as above: cacheWindow always returns a window |  |
| `!success` (fifth of 8) | true | 0 / 576 | RES | — | decision 11 (accepted: no fault record) |
| `cache->writeBytePos < cacheBytePos` (second of 4) | true | 0 / 576 | MEM | the cache stolen during the switch | decision 11 (accepted: no fault record) |
| `phaseIncrement != kMaxSampleValue` (second of 2) | false | 576 / 0 | CON | as the same test where the cache's parameters are checked: a voice sets up a cache only off its native rate |  |
| `interpolationBufferSize != kInterpolationMaxNumSamples` (first of 2) | true | 0 / 576 | CON | a change to linear interpolation is caught first, where the cache's parameters are checked at the render's start |  |
| `!stillGoing` (first of 5) | true | 0 / 196383 | RES | the stretcher pool empty (MEM: the voice is unassigned), or the begin's reassessment not ready (RES) | kAllocationFaults |
| `writingToCache` (second of 2) | false | 7424 / 0 | CON | inside `!cache \|\| writingToCache`, a cache is being written |  |
| `cache->writeBytePos < cacheBytePos` (third of 4) | true | 0 / 7424 | MEM | — | decision 11 (accepted: no fault record) |
| `!success` (sixth of 8) | both | 0 / 0 | MEM | — | decision 11 (accepted: no fault record) |
| `!stillGoing` (second of 5) | true | 0 / 8 | RES | — | decision 11 (accepted: no fault record) |
| `!stillGoing` (third of 5) | true | 0 / 8 | RES | — | decision 11 (accepted: no fault record) |
| `timeStretchRatio != kMaxSampleValue` | true | 0 / 1 | DEF | E240 |  |
| `phaseIncrement == kMaxSampleValue` | true | 0 / 1 | DEF | E241 |  |
| `!stopReadingFromCache()` | true | 0 / 1 | RES | — | decision 11 (accepted: no fault record) |
| `interpolationBufferSize != kInterpolationMaxNumSamples` (second of 2) | true | 0 / 1 | NS | linear interpolation at the written end of an unstretched cache |  |
| `!stillGoing` (fourth of 5) | true | 0 / 1 | RES | — | decision 11 (accepted: no fault record) |
| `!stillGoing` (fifth of 5) | true | 0 / 1 | RES | — | decision 11 (accepted: no fault record) |
| `bytesTilCacheEnd < 0` | true | 0 / 44014 | DEF | the read's earlier checks keep the cache position at or before the cache's written end |  |
| `!cacheCluster` (first of 2) | true | 0 / 44014 | DEF | a stolen cache cluster, caught earlier |  |
| `numSamplesThisCacheRead <= 0` | true | 0 / 44014 | DEF | ALPHA check |  |
| `true` | false | 5266941 / 0 | CON | `while (true)` |  |
| `uncachedClusterIndex < sample->getFirstClusterIndexWithAudioData() - 1` | true | 0 / 44014 | RES | resuming past a cache with the uncached chunk more than one chunk before the audio data; in the legacy only slow storage (streaming-margin, not a golden) reached the chunk resync after it | decision 11 (accepted: no fault record) |
| `!resync.holds_chunk()` | true | 0 / 43980 | RES | the lease holding no chunk at a cache window's resync: the previous resync found its chunk loading or not ready | decision 11 (accepted: no fault record) |
| `resync.holds_chunk()` | false | 43980 / 0 | RES | the resync's chunk loading or not ready (resync_to skips a loading chunk), so the head ends | decision 11 (accepted: no fault record) |
| `kCacheByteDepth == 3` | false | 111527 / 0 | CON | kCacheByteDepth is a constant 3 |  |
| `!setupSuccess` | true | 0 / 2571 | MEM | a new cache chunk's allocation fails (setupNewCluster's deluge_resource_request) | decision 11 (accepted: no fault record) |
| `!stopSuccess` | both | 0 / 0 | RES | — | decision 11 (accepted: no fault record) |
| `!cacheCluster` (second of 2) | true | 0 / 111527 | DEF | ALPHA check |  |
| `numSamplesThisUncachedRead <= 0` | true | 0 / 111527 | DEF | ALPHA check |  |
| `turns >= deluge::synth::kMaxLoopTurns` | true | 0 / 260 | OPEN | E169: a timestretcher-level loop shorter than one output frame's combined step (a few-frame loop pitched far up) restarts every turn; no golden's loop is that short | accepted (the app's: no golden builds it) |
| `cache->writeBytePos < cacheBytePos` (fourth of 4) | true | 0 / 105487 | MEM | — | decision 11 (accepted: no fault record) |
| `!success` (seventh of 8) | both | 0 / 0 | MEM | — | decision 11 (accepted: no fault record) |
| `cache->writeBytePos != cacheBytePos` | true | 0 / 5570 | MEM | — | decision 11 (accepted: no fault record) |
| `!success` (eighth of 8) | both | 0 / 0 | MEM | stopUsingCache after the stretched render stole a cache chunk | decision 11 (accepted: no fault record) |
| `cache` | false | 5570 / 0 | MEM | cache abandoned mid-write | decision 11 (accepted: no fault record) |

**VoiceSample::sampleZoneChanged**

| Condition | Side not taken | True / false | Category | Why | Pinned by |
|---|---|---|---|---|---|
| `cache->reversed != reversed` | true | 0 / 6 | EDIT | a cached voice reversed under it: a synth's `reversed` change kills every voice first (setting_apply.cpp), so only an audio clip reversed while its voice reads a cache reaches it | decision 10 |
| `!success` (first of 2) | both | 0 / 0 | EDIT | as above | decision 10 |
| `!success` (second of 2) | true | 0 / 1 | RES | stopUsingCache failing on a start-marker move | decision 11 (accepted: no fault record) |
| `voiceSource->playDirection == 1` (first of 2) | false | 2 / 0 | EDIT | a loop-end move under a reversed stretching voice: the marker cases' voices play forwards | decision 10 |
| `((VoiceSamplePlaybackGuide*)voiceSource)->shouldObeyLoopEndPointNow()` | false | 1 / 0 | NS | an unstretched voice's loop end moved ahead of it, or after its release |  |
| `(int32_t)(getPlayByteLowLevel(sample, voiceSource) - ((VoiceSamplePlaybackGuide*)voiceSource)->loopEndPlaybackAtByte) * voiceSource->playDirection >= 0` | false | 1 / 0 | NS | as above |  |
| `markerType == MarkerType::END` | false | 13 / 0 | CON | the four marker types are exhausted |  |
| `voiceSource->endPlaybackAtByte` | false | 8 / 0 | CON | endPlaybackAtByte is always set, as the comment suspects |  |
| `voiceSource->playDirection == 1` (second of 2) | false | 7 / 0 | EDIT | an end move under a reversed stretching voice, as above | decision 10 |
| `(int32_t)(getPlayByteLowLevel(sample, voiceSource) - voiceSource->endPlaybackAtByte) * voiceSource->playDirection >= 0` | false | 2 / 0 | NS | an unstretched voice whose end moves ahead of it |  |
| `false` | true | 0 / 17 | CON | `if (false)`, a label holder |  |

**VoiceSample::getPlaySample**

| Condition | Side not taken | True / false | Category | Why | Pinned by |
|---|---|---|---|---|---|
| `guide->playDirection == 1` | false | 1481 / 0 | OPEN | a reversed stretching voice's play position (getNumSamplesLaggingBehindSync): a reversed stretched audio clip, or a reversed STRETCH sample within a tenth of an octave of its length; the goldens' reversed synced voices are stretched further | accepted (the app's: no golden builds it) |

### `src/deluge/model/voice/voice_sample_stretch_port.cpp`

**VoiceSampleStretchPort::synced_position**

| Condition | Side not taken | True / false | Category | Why | Pinned by |
|---|---|---|---|---|---|
| `playbackHandler.isEitherClockActive()` | false | 8284 / 0 | OPEN | a synced (STRETCH-mode) synth voice hopping with both clocks stopped: its release after the song stops, or a kit row auditioned from the clip view while stopped (which passes the note's length); an audio clip's is the STOP ruling's | accepted (the app's: no golden builds it) |

**VoiceSampleStretchPort::run**

| Condition | Side not taken | True / false | Category | Why | Pinned by |
|---|---|---|---|---|---|
| `window.frames == nullptr` | true | 0 / 5328124 | RES | the frame not resident: the aligner's frame read (A4) or the start clamp's residency check (B8) | kAlignFaults, kStartClampFaults |

**VoiceSampleStretchPort::seat_and_swap**

| Condition | Side not taken | True / false | Category | Why | Pinned by |
|---|---|---|---|---|---|
| `ReadHead::seat(head, lease, frame, phase, direction) != Residency::READY` | true | 0 / 9006 | RES | a hop's or fudge's seat not ready (H1, S4) | kSeatFaults |
| `!ReadHead::walk(head, lease, bounds, direction)` | true | 0 / 9006 | RES | the seat's chunk walk failing (H1, S4) | kSeatFaults |
| `lease.holds_chunk()` | false | 9006 / 0 | DEF | ReadHead::walk returns true only while the lease has a span, which needs a chunk held, so STARTED_EMPTY is never returned |  |

**VoiceSampleStretchPort::cursors_meet**

| Condition | Side not taken | True / false | Category | Why | Pinned by |
|---|---|---|---|---|---|
| `newer.index() != older.index()` | true | 0 / 738 | OPEN | E1 with one head reading and the other ended: the plain begin's level copy stopped at its first chunk boundary (R10's quirk) when the ratio returns to 1.0, or the older ended at the waveform's end or a chunk not ready | accepted (the app's: no golden builds it) |
| `reading` | false | 738 / 0 | OPEN | E1 with both heads ended (past a non-looping end, or chunks not ready) when the ratio returns to 1.0 before the fade ends | accepted (the app's: no golden builds it) |

**VoiceSampleStretchPort::drop**

| Condition | Side not taken | True / false | Category | Why | Pinned by |
|---|---|---|---|---|---|
| `held != nullptr` | false | 6 / 0 | DEF | the stretcher drops only a chunk it holds (hold adds a lease), and a leased perc chunk cannot be stolen |  |

### `src/deluge/processing/engines/audio_engine.cpp`

**AudioEngine::cullVoices**

| Condition | Side not taken | True / false | Category | Why | Pinned by |
|---|---|---|---|---|---|
| `bypassed` | true | 0 / 7 | CULL | the render fits its period in every gate, so the cull returns before this check; a hop's bypass is also cleared at the block's end, before the next block's cull reads it (the design note's findings) | decision 11 (accepted: no fault record) |
| `currentSong` | true | 0 / 7 | CULL | as above: no cull runs | decision 11 (accepted: no fault record) |
| `i < plan.kill` | true | 0 / 7 | CULL | as above | decision 11 (accepted: no fault record) |
| `i < plan.terminate` | true | 0 / 7 | CULL | as above | decision 11 (accepted: no fault record) |
| `i < plan.force_release` | true | 0 / 7 | CULL | as above | decision 11 (accepted: no fault record) |
| `plan.kill + plan.terminate > 0` | true | 0 / 7 | CULL | as above | decision 11 (accepted: no fault record) |
| `culled != 0` | true | 0 / 7 | CULL | as above | decision 11 (accepted: no fault record) |
| `FlashStorage::highCPUUsageIndicator` | both | 0 / 0 | CULL | as above | decision 11 (accepted: no fault record) |

**AudioEngine::setDireness**

| Condition | Side not taken | True / false | Category | Why | Pinned by |
|---|---|---|---|---|---|
| `numSamples >= direnessThreshold` | true | 0 / 535712 | CULL | direness never rises: the gates fail a render whose direness rises | decision 11 (accepted: no fault record) |
| `newDireness >= cpuDireness` | both | 0 / 0 | CULL | as above | decision 11 (accepted: no fault record) |
| `cpuDireness < 0` | false | 11787 / 0 | CULL | as above: direness stays 0, so every decrement goes below 0 | decision 11 (accepted: no fault record) |

**AudioEngine::renderAudioForStemExport**

| Condition | Side not taken | True / false | Category | Why | Pinned by |
|---|---|---|---|---|---|
| `sideChainHitPending` | both | 0 / 0 | EXPORT | the offline stem export's render: no recorded gate exports stems |  |
| `currentSong != nullptr` | both | 0 / 0 | EXPORT | as above |  |
| `stemExport.includeSongFX` | both | 0 / 0 | EXPORT | as above |  |
| `recorder` | both | 0 / 0 | EXPORT | as above |  |
| `recorder->mode == AudioInputChannel::OFFLINE_OUTPUT` | both | 0 / 0 | EXPORT | as above |  |
| `recorder->mode == AudioInputChannel::MIX` | both | 0 / 0 | EXPORT | as above |  |
| `recorder->status < RecorderStatus::FINISHED_CAPTURING_BUT_STILL_WRITING` | both | 0 / 0 | EXPORT | as above |  |

### `src/dsp/fixed/sample_read/read_head.h`

**deluge::dsp::fixed::sample_read::Residency deluge::dsp::fixed::sample_read::ReadHead::seat<SampleReadLease>**

| Condition | Side not taken | True / false | Category | Why | Pinned by |
|---|---|---|---|---|---|
| `residency == Residency::READY` | false | 9006 / 0 | RES | a hop's seat not ready (seat_and_swap answers NOT_READY; S4's retry) | kSeatFaults |

**bool deluge::dsp::fixed::sample_read::ReadHead::walk<SampleReadLease>**

| Condition | Side not taken | True / false | Category | Why | Pinned by |
|---|---|---|---|---|---|
| `reading == nullptr` | true | 0 / 687198 | EDIT | check_native is the walk's only caller that does not test for a chunk first; a head ended at width 0 reaches it only when a cache is dropped mid-note (stopUsingCache) after the cache resync ended the head (render's Ended{0}) and the voice reads on natively | decision 10 |
| `source.move_to(span->boundary, direction) != Residency::READY` | true | 0 / 8058 | RES | the next chunk not ready mid-walk (SampleReadLease::move_to) | kReadHeadFaults |
| `place(state, source, *bounds.loop_start_frame + (over * d), direction) != Residency::READY` | true | 0 / 969 | RES | a low-level loop's restart not ready | kReadHeadFaults |
| `++turns >= kMaxWalkTurns` | true | 0 / 9027 | DEF | E227: each turn crosses a chunk or restarts a loop at least a frame long, and one read's overshoot is far under 1,024 frames |  |

**deluge::dsp::fixed::sample_read::ReadHead::Window deluge::dsp::fixed::sample_read::ReadHead::check<SampleReadLease>**

| Condition | Side not taken | True / false | Category | Why | Pinned by |
|---|---|---|---|---|---|
| `!reshape(state, source, bounds, width, direction)` | true | 0 / 124991 | RES | reshape fails only when its forward fill's zero taps reach the width: a head whose loop restart was not ready (a stale count) starting to interpolate; the widening case is CULL (reshape below) | read_head_window_spec ("stops a stale head whose silence would fill the window") |

**void deluge::dsp::fixed::sample_read::ReadHead::read_resampled<SampleReadLease>**

| Condition | Side not taken | True / false | Category | Why | Pinned by |
|---|---|---|---|---|---|
| `source.span(direction).has_value()` | false | 123302 / 0 | DEF | the window check before it leaves a reading cursor only over a held chunk: every let-go ends the cursor (walk, place, first_step's push_zero, fill_silence) or the voice (reacquire's failure); render's cache stop between check and read reseats or ends the voice |  |
| `run` | false | 2602 / 0 | OPEN | a linear-interpolation voice playing tail zeros after a non-looping end (or after a chunk not ready) | kReadHeadFaults |

**short deluge::dsp::fixed::sample_read::ReadHead::tap<SampleReadLease>**

| Condition | Side not taken | True / false | Category | Why | Pinned by |
|---|---|---|---|---|---|
| `run` | false | 352099 / 0 | DEF | every tap is read over a held chunk (back_fill's span test, forward_fill's and first_step's true walk), and SampleReadLease::run fails only without one |  |

**deluge::dsp::fixed::sample_read::Residency deluge::dsp::fixed::sample_read::ReadHead::place<SampleReadLease>**

| Condition | Side not taken | True / false | Category | Why | Pinned by |
|---|---|---|---|---|---|
| `frame < 0` | true | 0 / 11920 | OPEN | a reversed low-level loop restart whose overshoot carries past the data start (a loop near it shorter than one output frame's jump); or a hop seat before the data | read_head_seat_spec ("refuses a seat outside the data") |
| `frame >= source.length_frames()` | true | 0 / 11920 | OPEN | as above, forwards: a loop ending at the data end, shorter than the overshoot at a high pitch; or a hop seat past the data | read_head_seat_spec ("refuses a seat outside the data") |
| `residency != Residency::READY` | true | 0 / 11920 | RES | a seat or restart whose chunk is not ready | kSeatFaults, kReadHeadFaults |

**deluge::dsp::fixed::sample_read::ReadHead::Window deluge::dsp::fixed::sample_read::ReadHead::check_native<SampleReadLease>**

| Condition | Side not taken | True / false | Category | Why | Pinned by |
|---|---|---|---|---|---|
| `state.width_last != 0` | true | 0 / 498318 | OPEN | a resampling voice returning to exactly its native rate mid-note, e.g. a pitch bend back to zero on a note at the sample's root | read_head_window_spec ("steps a resampled window back to its centre when the read turns native") |
| `!source.span(direction).has_value()` | both | 0 / 0 | OPEN | as above; its true side with the head holding no chunk (in its tail zeros) | read_head_window_spec ("steps a resampled window back …", "stops a native read of a resampled head holding no chunk") |
| `bytes <= 0` | true | 0 / 497265 | DEF | the walk just returned true, leaving the cursor short of the boundary, so at least one frame's bytes are left |  |

**bool deluge::dsp::fixed::sample_read::ReadHead::reshape<SampleReadLease>**

| Condition | Side not taken | True / false | Category | Why | Pinned by |
|---|---|---|---|---|---|
| `state.width_last == width` | false | 123848 / 0 | CULL | the window changes width only when CPU direness drops sinc to linear mid-note (or the interpolation mode is edited mid-note) | read_head_window_spec ("narrows a window by stepping its cursor back …") |
| `w < last` | both | 0 / 0 | CULL | as above | read_head_window_spec ("narrows a window …", "fills a widened window's oldest taps with 0") |
| `!forward_fill(state, source, bounds, offset, w, direction)` | both | 0 / 0 | CULL | as above | read_head_window_spec ("fills a widened window's oldest taps with 0", "stops a stale head …") |

**bool deluge::dsp::fixed::sample_read::ReadHead::first_step<SampleReadLease>**

| Condition | Side not taken | True / false | Category | Why | Pinned by |
|---|---|---|---|---|---|
| `held` | false | 123112 / 0 | DEF | a reading cursor holds its chunk here: reshape's fills end it otherwise, and every let-go ends the cursor (walk, place) or the voice (reacquire's failure) |  |
| `source.span(direction).has_value()` | true | 0 / 2405 | DEF | every failed walk lets the chunk go (R6), and with none held the walk is not called |  |

**int deluge::dsp::fixed::sample_read::ReadHead::window_length<SampleReadLease>**

| Condition | Side not taken | True / false | Category | Why | Pinned by |
|---|---|---|---|---|---|
| `!source.span(direction).has_value()` | true | 0 / 123056 | DEF | evaluated only for a reading cursor, which first_step has just left over a held chunk |  |
| `ended != nullptr` | false | 1529 / 0 | DEF | as above: a reading cursor here holds its chunk |  |

**void deluge::dsp::fixed::sample_read::ReadHead::by_layout<void deluge::dsp::fixed::sample_read::ReadHead::read_resampled<SampleReadLease>**

| Condition | Side not taken | True / false | Category | Why | Pinned by |
|---|---|---|---|---|---|
| `out_channels == 2` | true | 0 / 1640 | OPEN | a stereo sample read linearly while rendering in stereo (`renderInStereo`: headphones or line out) | read_head_read_spec ("reads a stereo sample linearly into a stereo output …") |

**void deluge::dsp::fixed::sample_read::ReadHead::read_native_as<1, 1, SampleReadLease>**

| Condition | Side not taken | True / false | Category | Why | Pinned by |
|---|---|---|---|---|---|
| `!run` | true | 0 / 23817 | DEF | check_native's walk leaves a held chunk, and SampleReadLease::run fails only without one |  |

**void deluge::dsp::fixed::sample_read::ReadHead::read_native_as<2, 1, SampleReadLease>**

| Condition | Side not taken | True / false | Category | Why | Pinned by |
|---|---|---|---|---|---|
| `!run` | true | 0 / 427491 | DEF | as above |  |

**void deluge::dsp::fixed::sample_read::ReadHead::read_native_as<2, 2, SampleReadLease>**

| Condition | Side not taken | True / false | Category | Why | Pinned by |
|---|---|---|---|---|---|
| `!run` | true | 0 / 45958 | DEF | as above |  |

**void deluge::dsp::fixed::sample_read::ReadHead::sinc_feed<1>**

| Condition | Side not taken | True / false | Category | Why | Pinned by |
|---|---|---|---|---|---|
| `steps == 0` | true | 0 / 33643 | OPEN | a mono sample read through the sinc window below its native rate: a phase step that crosses no frame | read_head_read_spec ("feeds a mono sinc read below its native rate …") |
| `steps > kInterpolationMaxNumSamples` | true | 0 / 33643 | OPEN | a sinc read stepping over 16 source frames per output frame: a sample pitched more than four octaves up | read_head_read_spec ("jumps a sinc step of more than 16 frames …") |

**void deluge::dsp::fixed::sample_read::ReadHead::sinc_feed<2>**

| Condition | Side not taken | True / false | Category | Why | Pinned by |
|---|---|---|---|---|---|
| `steps > kInterpolationMaxNumSamples` | true | 0 / 14428668 | OPEN | as above | read_head_read_spec ("jumps a sinc step of more than 16 frames …") |

**void deluge::dsp::fixed::sample_read::ReadHead::read_sinc_as<1>**

| Condition | Side not taken | True / false | Category | Why | Pinned by |
|---|---|---|---|---|---|
| `frames <= 0` | true | 0 / 275 | DEF | window_length gives at least one frame (frames below 2 as asked, else room / increment + 1), and every caller asks for at least one |  |
| `Channels == 2` | true | 0 / 275 | CON | `Channels` is 1 in the mono instantiation |  |
| `output.out_channels == 2` | both | 0 / 0 | CON | never evaluated: `Channels == 2` is false in the mono instantiation |  |
| `stereo_out` | true | 0 / 275 | CON | `stereo_out` is false when `Channels` is 1 |  |

**void deluge::dsp::fixed::sample_read::ReadHead::read_sinc_as<2>**

| Condition | Side not taken | True / false | Category | Why | Pinned by |
|---|---|---|---|---|---|
| `frames <= 0` | true | 0 / 120425 | DEF | as above |  |
| `Channels == 2` | false | 120425 / 0 | CON | `Channels` is 2 in the stereo instantiation |  |

**void deluge::dsp::fixed::sample_read::ReadHead::read_linear_as<1, 1>**

| Condition | Side not taken | True / false | Category | Why | Pinned by |
|---|---|---|---|---|---|
| `frames <= 0` | true | 0 / 962 | DEF | window_length gives at least one frame, and every caller asks for at least one |  |
| `Channels == 2` (first of 2) | true | 0 / 962 | CON | `Channels` is 1 in the mono instantiation |  |
| `Channels == 2` (second of 2) | true | 0 / 962 | CON | `Channels` is 1 in the mono instantiation |  |

**void deluge::dsp::fixed::sample_read::ReadHead::read_linear_as<2, 1>**

| Condition | Side not taken | True / false | Category | Why | Pinned by |
|---|---|---|---|---|---|
| `frames <= 0` | true | 0 / 1640 | DEF | as above |  |
| `Channels == 2` (first of 2) | false | 1640 / 0 | CON | `Channels` is 2 in the stereo instantiation |  |
| `Channels == 2` (second of 2) | false | 1640 / 0 | CON | `Channels` is 2 in the stereo instantiation |  |

**void deluge::dsp::fixed::sample_read::ReadHead::read_linear_as<2, 2>**

| Condition | Side not taken | True / false | Category | Why | Pinned by |
|---|---|---|---|---|---|
| `frames <= 0` | both | 0 / 0 | OPEN | 0 calls: a stereo sample read linearly while rendering in stereo (`renderInStereo`) | read_head_read_spec ("reads a stereo sample linearly into a stereo output …") |
| `Channels == 2` (first of 2) | both | 0 / 0 | OPEN | 0 calls: a stereo sample read linearly while rendering in stereo | read_head_read_spec ("reads a stereo sample linearly into a stereo output …") |
| `Channels == 2` (second of 2) | both | 0 / 0 | OPEN | 0 calls: a stereo sample read linearly while rendering in stereo | read_head_read_spec ("reads a stereo sample linearly into a stereo output …") |
| `out != end` | both | 0 / 0 | OPEN | as above | read_head_read_spec ("reads a stereo sample linearly into a stereo output …") |

**void deluge::dsp::fixed::sample_read::ReadHead::read_sinc_zeros<SampleReadLease>**

| Condition | Side not taken | True / false | Category | Why | Pinned by |
|---|---|---|---|---|---|
| `channels == 2` | false | 940 / 0 | OPEN | a mono sample read through the sinc window playing its tail zeros after a non-looping end | read_head_read_spec ("leaves an ended head's zero count as a sinc read found it …") |

**void deluge::dsp::fixed::sample_read::ReadHead::read_linear_zeros<SampleReadLease>**

| Condition | Side not taken | True / false | Category | Why | Pinned by |
|---|---|---|---|---|---|
| `channels == 2` | both | 0 / 0 | OPEN | 0 calls: a linear-interpolation voice playing tail zeros after a non-looping end | kReadHeadFaults |
| `output.out_channels == 1` | both | 0 / 0 | OPEN | as above; its false side is a stereo sample in those zeros while rendering in stereo | kReadHeadFaults; read_head_read_spec ("feeds a stereo head's tail zeros into a stereo output …") |
| `i < frames` | both | 0 / 0 | OPEN | as above | kReadHeadFaults |
| `i != 0` | both | 0 / 0 | OPEN | as above | kReadHeadFaults |
| `condense` | both | 0 / 0 | OPEN | as above | kReadHeadFaults |

**void deluge::dsp::fixed::sample_read::ReadHead::zero_step<SampleReadLease>**

| Condition | Side not taken | True / false | Category | Why | Pinned by |
|---|---|---|---|---|---|
| `steps != 0` | false | 11370 / 0 | OPEN | a voice pitched below its native rate playing tail zeros past its end: a phase step that crosses no frame | kReadHeadFaults |
| `ended` | false | 14826 / 0 | DEF | zero steps run only with no run (read_resampled), and a reading cursor holds its chunk after the window check, so the cursor here is ended |  |

**void deluge::dsp::fixed::sample_read::ReadHead::back_fill<SampleReadLease>**

| Condition | Side not taken | True / false | Category | Why | Pinned by |
|---|---|---|---|---|---|
| `!span` | true | 0 / 1143 | OPEN | R1 on a head holding no chunk: a shadow copy (width 0) of a newer head in its tail zeros past a non-looping end (copy_without_cursor's empty lease), or a head ended by the cache resync resuming uncached and resampled | read_head_window_spec ("fills a fresh window past a non-looping end with counted silence") |
| `reading == nullptr` | true | 0 / 1143 | DEF | an ended cursor never holds a chunk: every `Ended` follows a let-go (walk, place's seat_chunk, render's resync) or is set where no chunk is held (push_zero, fill_silence, zero_step) |  |

**bool deluge::dsp::fixed::sample_read::ReadHead::forward_fill<SampleReadLease>**

| Condition | Side not taken | True / false | Category | Why | Pinned by |
|---|---|---|---|---|---|
| `!source.span(direction).has_value()` | true | 0 / 8871 | OPEN | as back_fill's `!span` above: R1 on a head holding no chunk | read_head_window_spec ("fills a fresh window past a non-looping end with counted silence") |
| `!walk(state, source, bounds, direction)` | true | 0 / 8871 | OPEN | a resampled start within half a sinc window (8 frames) of a non-looping end, e.g. a stretch hop landing there; else the next chunk not ready (RES) | read_head_window_spec ("fills a fresh window past a non-looping end with counted silence") |

**deluge::dsp::fixed::sample_read::ReadHead::fill_silence**

| Condition | Side not taken | True / false | Category | Why | Pinned by |
|---|---|---|---|---|---|
| `ended` | both | 0 / 0 | OPEN | 0 calls: reached only from the forward fill's no-chunk and failed-walk exits, above | read_head_window_spec ("fills a fresh window past a non-looping end …") |
| `feed.remaining() != 0` | both | 0 / 0 | OPEN | as above | read_head_window_spec ("fills a fresh window past a non-looping end …") |
| `static_cast<uint32_t>(zero_taps) >= width` | both | 0 / 0 | OPEN | as above; its true side also needs a stale count (a restart not ready) | read_head_window_spec ("fills a fresh window past a non-looping end …", "stops a stale head …") |

**deluge::dsp::fixed::sample_read::ReadHead::counted**

| Condition | Side not taken | True / false | Category | Why | Pinned by |
|---|---|---|---|---|---|
| `zero_taps == kStaleZeroTaps` | true | 0 / 17231 | RES | a stale count (a low-level restart not ready) fed further zeros | kReadHeadFaults |

**void deluge::dsp::fixed::sample_read::ReadHead::step_back<SampleReadLease>**

| Condition | Side not taken | True / false | Category | Why | Pinned by |
|---|---|---|---|---|---|
| `reading == nullptr` | both | 0 / 0 | OPEN | 0 calls: a resampling voice returning to its native rate mid-note (check_native), or its sinc window shrinking under CPU direness (reshape's narrow, CULL) | read_head_window_spec ("steps a resampled window back …", "narrows a window …") |
| `!span` | both | 0 / 0 | OPEN | as above | read_head_window_spec ("steps a resampled window back …", "narrows a window …") |
| `frames-- > 0` | both | 0 / 0 | OPEN | as above | read_head_window_spec ("narrows a window by stepping its cursor back …") |
| `(reading->frame - d - span->floor) * d < 0` | both | 0 / 0 | OPEN | as above, with the step back reaching the chunk's floor | read_head_window_spec ("narrows a window …, never before the chunk's floor") |

**bool deluge::dsp::fixed::sample_read::ReadHead::push_zero<SampleReadLease>**

| Condition | Side not taken | True / false | Category | Why | Pinned by |
|---|---|---|---|---|---|
| `ended` | false | 2405 / 0 | DEF | push_zero runs only with no chunk held, and a reading cursor holds its chunk after reshape, so the cursor here is ended |  |

### `src/dsp/fixed/timestretch/cache_tail.h`

**deluge::dsp::fixed::timestretch::cache_tail_frames**

| Condition | Side not taken | True / false | Category | Why | Pinned by |
|---|---|---|---|---|---|
| `window.bytes_written_ahead <= frame_bytes` | true | 0 / 580 | DEF | the near-end switch out of a cache fires with at least 72 frames still written (VoiceSample's 200-frame threshold against 128-frame blocks), and a finished cache wraps at its loop end, so more than a frame is always ahead | kCacheTailLimitSweep |
| `to_chunk_end <= frame_bytes` | true | 0 / 580 | OPEN | the read position in a cache chunk's last byte at the switch out of a cached stretch: an alignment of about 1 in 5,400 frames | kCacheTailLimitSweep |

**deluge::dsp::fixed::timestretch::CacheTail::mix_into**

| Condition | Side not taken | True / false | Category | Why | Pinned by |
|---|---|---|---|---|---|
| `frames_ == 0` | true | 0 / 887 | DEF | SampleStretcher::begin builds a CacheTail only when cache_tail_frames gives more than 0 frames; no other code constructs one |  |
| `it != end` | false | 76823 / 0 | DEF | the tail takes the sample's channels and a stereo output only comes from a stereo sample (numChannelsInOutputBuffer, stretchedChannels), so a stereo mix writes an even number of words and the second word is always inside |  |

### `src/dsp/fixed/timestretch/hop_planner.h`

**deluge::dsp::fixed::timestretch::loop_crossfade**

| Condition | Side not taken | True / false | Category | Why | Pinned by |
|---|---|---|---|---|---|
| `source < 0` | true | 0 / 3822 | STOP | the stretch position past the loop end of a stretcher-made loop: a clip that has just stopped, as the check's comment says | STOP ruling |

**deluge::dsp::fixed::timestretch::clamp_start**

| Condition | Side not taken | True / false | Category | Why | Pinned by |
|---|---|---|---|---|---|
| `!start_resident` | true | 0 / 463 | RES | a hop's start before the start marker whose frame is not resident (B8) | kStartClampFaults |

### `src/dsp/fixed/timestretch/perc_analysis.h`

**deluge::dsp::fixed::timestretch::analyse**

| Condition | Side not taken | True / false | Category | Why | Pinned by |
|---|---|---|---|---|---|
| `index >= 0` | false | 89626 / 0 | DEF | Sample::fillPercCache bounds each analysed run to its perc chunk (samplesLeftThisDestCluster) or to the whole store, so a pixel's middle frame never lies before the span's first pixel (a run stopping one frame early lands on a pixel's last frame, not its middle) |  |
| `index < static_cast<int32_t>(pixels.size())` | false | 89626 / 0 | DEF | as above: the run's frames, the frame after it included, stay within the chunk or the store's pixels |  |

### `src/dsp/fixed/timestretch/waveform_aligner.h`

**deluge::dsp::fixed::timestretch::FoldedFrames<deluge::synth::PortFrames>::operator**

| Condition | Side not taken | True / false | Category | Why | Pinned by |
|---|---|---|---|---|---|
| `!run_` | true | 0 / 5295379 | RES | a frame of the new head's averages not resident: the next run is refused | kAlignFaults |

**std::optional<std::array<int, 3ul>> deluge::dsp::fixed::timestretch::moving_averages<deluge::synth::PortFrames>**

| Condition | Side not taken | True / false | Category | Why | Pinned by |
|---|---|---|---|---|---|
| `!first` | true | 0 / 16143 | RES | the averages' first frame not resident | kAlignFaults |
| `frame * frame_bytes < -1` | true | 0 / 47811 | DEF | the end check before the loop (end * frame_bytes < -1 refuses) bounds every average's first frame, which lies between read and end; forwards read is at least half the crossfade |  |
| `frame >= length` | true | 0 / 47811 | OPEN | forwards every average starts before end, which the end check holds to the length; reversed has no margin (A2's quirk), so a reversed hop whose new head starts within 1.5 averages, less half its crossfade, of the waveform's end (back_edge holding it at the last frame, with a crossfade under about 3 averages) starts its averages past the end | stretch_waveform_aligner_spec ("keeps A2's reversed quirk: no half-crossfade margin") |
| `!value` | true | 0 / 1679766 | RES | a frame within the head's averages not resident | kAlignFaults |

**std::optional<deluge::dsp::fixed::timestretch::Alignment> deluge::dsp::fixed::timestretch::align<deluge::synth::PortFrames>**

| Condition | Side not taken | True / false | Category | Why | Pinned by |
|---|---|---|---|---|---|
| `found.abandoned` | true | 0 / 7869 | RES | a frame of the search's slide not resident (FoldedFrames finds no run): the search abandons (A4; phase 0, decision 7). Only the aligner spec's hand-built source takes it; kAlignFaults' frames are missing inside the head's own averages, refused before the search | stretch_waveform_aligner_spec ("starts an abandoned search at phase 0 (decision 7)") |

### `src/dsp/fixed/timestretch/waveform_search.h`

**deluge::dsp::fixed::timestretch::search_detail::Run<deluge::dsp::fixed::pitch_shift::WaveformAligner::RingReader<deluge::dsp::StereoSample>>::Run**

| Condition | Side not taken | True / false | Category | Why | Pinned by |
|---|---|---|---|---|---|
| `shape.turns` | false | 35 / 0 | NS | the pitch shifter's stereo search pitching down, which searches one way (its quirk Q10); not a stretcher path, the shifter's goldens own it |  |

**deluge::dsp::fixed::timestretch::search_detail::Run<deluge::dsp::fixed::timestretch::PlayOrderReader<deluge::synth::PortFrames>>::Run**

| Condition | Side not taken | True / false | Category | Why | Pinned by |
|---|---|---|---|---|---|
| `shape.turns` | false | 7869 / 0 | CON | align's SearchShape sets turns = true |  |

**deluge::dsp::fixed::timestretch::search_detail::Run<deluge::dsp::fixed::pitch_shift::WaveformAligner::RingReader<deluge::dsp::Fixed<int, 31>>>::result**

| Condition | Side not taken | True / false | Category | Why | Pinned by |
|---|---|---|---|---|---|
| `true` | false | 775 / 0 | CON | while (true) |  |
| `outcome == Outcome::ABANDON` | true | 0 / 775 | CON | RingReader::folded always returns a value: InputHistory::folded_high_word reads the ring and cannot fail |  |

**deluge::dsp::fixed::timestretch::search_detail::Run<deluge::dsp::fixed::pitch_shift::WaveformAligner::RingReader<deluge::dsp::StereoSample>>::result**

| Condition | Side not taken | True / false | Category | Why | Pinned by |
|---|---|---|---|---|---|
| `true` | false | 51 / 0 | CON | while (true) |  |
| `outcome == Outcome::ABANDON` | true | 0 / 51 | CON | RingReader::folded always returns a value: InputHistory::folded_high_word reads the ring and cannot fail |  |
| `outcome == Outcome::TURN` | false | 16 / 0 | NS | the pitch shifter's stereo search (pitch_shift::WaveformAligner over RingReader<StereoSample>); not a stretcher path, the shifter's goldens own it |  |
| `++directions_searched_ >= 2` | both | 0 / 0 | NS | the pitch shifter's stereo search (pitch_shift::WaveformAligner over RingReader<StereoSample>); not a stretcher path, the shifter's goldens own it |  |

**deluge::dsp::fixed::timestretch::search_detail::Run<deluge::dsp::fixed::timestretch::PlayOrderReader<deluge::synth::PortFrames>>::result**

| Condition | Side not taken | True / false | Category | Why | Pinned by |
|---|---|---|---|---|---|
| `true` | false | 18991 / 0 | CON | while (true) |  |
| `outcome == Outcome::ABANDON` | true | 0 / 18991 | RES | a frame of the search's slide not resident (FoldedFrames finds no run): the search abandons (A4; phase 0, decision 7). Only the aligner spec's hand-built source takes it; kAlignFaults' frames are missing inside the head's own averages, refused before the search | waveform_search_spec ("abandons where a frame cannot be read") |

**deluge::dsp::fixed::timestretch::search_detail::Run<deluge::dsp::fixed::pitch_shift::WaveformAligner::RingReader<deluge::dsp::StereoSample>>::pass**

| Condition | Side not taken | True / false | Category | Why | Pinned by |
|---|---|---|---|---|---|
| `here <= 0` | true | 0 / 51 | NS | the pitch shifter's stereo search (pitch_shift::WaveformAligner over RingReader<StereoSample>); not a stretcher path, the shifter's goldens own it |  |
| `!slide(read, running)` | true | 0 / 4406 | CON | RingReader::folded always returns a value: InputHistory::folded_high_word reads the ring and cannot fail |  |
| `directions_searched_ == 0` | false | 35 / 0 | NS | the pitch shifter's stereo search (pitch_shift::WaveformAligner over RingReader<StereoSample>); not a stretcher path, the shifter's goldens own it |  |
| `offset != end` | false | 4355 / 0 | NS | the pitch shifter's stereo search (pitch_shift::WaveformAligner over RingReader<StereoSample>); not a stretcher path, the shifter's goldens own it |  |

**deluge::dsp::fixed::timestretch::search_detail::Run<deluge::dsp::fixed::timestretch::PlayOrderReader<deluge::synth::PortFrames>>::pass**

| Condition | Side not taken | True / false | Category | Why | Pinned by |
|---|---|---|---|---|---|
| `here <= 0` | true | 0 / 18991 | OPEN | a search with no room: a limit of 0 (A3: a hop of a few frames or a ratio far below the root), or the averages' window ending exactly on the waveform's edge in the search's direction (forwards, its end on the last frame; reversed, its first frame the last frame, or its end at frame -1 of an 8-bit mono sample) | waveform_search_spec ("searches nothing with a limit of 0 (decision 3's range)") |
| `!slide(read, running)` | true | 0 / 4776247 | RES | a frame of the search's slide not resident (FoldedFrames finds no run): the search abandons (A4; phase 0, decision 7). Only the aligner spec's hand-built source takes it; kAlignFaults' frames are missing inside the head's own averages, refused before the search | waveform_search_spec ("abandons where a frame cannot be read") |

**deluge::dsp::fixed::timestretch::search_detail::Run<deluge::dsp::fixed::pitch_shift::WaveformAligner::RingReader<deluge::dsp::Fixed<int, 31>>>::pass**

| Condition | Side not taken | True / false | Category | Why | Pinned by |
|---|---|---|---|---|---|
| `!slide(read, running)` | true | 0 / 61945 | CON | RingReader::folded always returns a value: InputHistory::folded_high_word reads the ring and cannot fail |  |

**deluge::dsp::fixed::timestretch::search_detail::Run<deluge::dsp::fixed::pitch_shift::WaveformAligner::RingReader<deluge::dsp::Fixed<int, 31>>>::slide**

| Condition | Side not taken | True / false | Category | Why | Pinned by |
|---|---|---|---|---|---|
| `!value` | true | 0 / 247780 | CON | RingReader::folded always returns a value: InputHistory::folded_high_word reads the ring and cannot fail |  |

**deluge::dsp::fixed::timestretch::search_detail::Run<deluge::dsp::fixed::pitch_shift::WaveformAligner::RingReader<deluge::dsp::StereoSample>>::slide**

| Condition | Side not taken | True / false | Category | Why | Pinned by |
|---|---|---|---|---|---|
| `!value` | true | 0 / 17624 | CON | RingReader::folded always returns a value: InputHistory::folded_high_word reads the ring and cannot fail |  |

**deluge::dsp::fixed::timestretch::search_detail::Run<deluge::dsp::fixed::timestretch::PlayOrderReader<deluge::synth::PortFrames>>::slide**

| Condition | Side not taken | True / false | Category | Why | Pinned by |
|---|---|---|---|---|---|
| `!value` | true | 0 / 19104988 | RES | a frame of the search's slide not resident (FoldedFrames finds no run): the search abandons (A4; phase 0, decision 7). Only the aligner spec's hand-built source takes it; kAlignFaults' frames are missing inside the head's own averages, refused before the search | waveform_search_spec ("abandons where a frame cannot be read") |

**deluge::dsp::fixed::timestretch::search_detail::Run<deluge::dsp::fixed::pitch_shift::WaveformAligner::RingReader<deluge::dsp::StereoSample>>::flipped**

| Condition | Side not taken | True / false | Category | Why | Pinned by |
|---|---|---|---|---|---|
| `shape_.sub_sample` | false | 70 / 0 | NS | the pitch shifter's stereo search (pitch_shift::WaveformAligner over RingReader<StereoSample>); not a stretcher path, the shifter's goldens own it |  |

### `src/synth/sources/sample/perc_fill.h`

**deluge::synth::PercFill::touch**

| Condition | Side not taken | True / false | Category | Why | Pinned by |
|---|---|---|---|---|---|
| `touched_count == touched.size()` | true | 0 / 1283 | DEF | a fill reads at most 32 render blocks of frames and a perc chunk covers 4,194,304, so Sample::fillPercCache touches at most two chunks (PCF3 freezes otherwise) |  |

### `src/synth/sources/sample/sample_stretcher.cpp`

**deluge::synth::SampleStretcher::keep_hop_back**

| Condition | Side not taken | True / false | Category | Why | Pinned by |
|---|---|---|---|---|---|
| `facts.length_frames < 1` | true | 0 / 10134 | DEF | a sample with no frames; it cannot load |  |

**deluge::synth::SampleStretcher::begin**

| Condition | Side not taken | True / false | Category | Why | Pinned by |
|---|---|---|---|---|---|
| `fudge->output_frames_to_loop != 0` | false | 112 / 0 | OPEN | a fudge offered with no frames to the loop (AudioClip::render at a swung loop tick whose render starts on it): it begins plain, as the legacy's init did | sample_stretcher_spec: begins a fudge with no frames to the loop as a plain begin |
| `(start - first) * d < 0` | true | 0 / 112 | DEF | AudioClip::render offers a fudge only when the frames to the loop are within the pre-margin it measured |  |
| `seat == SeatResult::NOT_READY` | true | 0 / 112 | RES | a fudge's seat not ready, or its walk failing: the begin fails and the voice is unassigned (H1) | kSeatFaults |
| `seat == SeatResult::STARTED` | false | 112 / 0 | DEF | seat_and_swap returns STARTED_EMPTY only after ReadHead::walk returned true, and walk returns true only from SampleReadLease::span, which is nullopt without a held chunk |  |
| `frames > 0` | false | 580 / 0 | OPEN | cache_tail_frames giving 0: the read position in a cache chunk's last byte at the switch (its written-end check is DEF, as above) | kCacheTailLimitSweep |
| `loan` | false | 580 / 0 | MEM | all eight tail buffers lent (TailBuffers::lend): the begin stays plain, no crossfade (decision 2), as the legacy when its allocation fails | kAllocationFaults |

**deluge::synth::SampleStretcher::remember**

| Condition | Side not taken | True / false | Category | Why | Pinned by |
|---|---|---|---|---|---|
| `oldest.has_value()` | true | 0 / 6 | OPEN | a third distinct perc-store chunk remembered: playback crossing two perc-chunk boundaries, so a source over 2^23 frames; it decides only which chunks stay leased | decision 11 |

**deluge::synth::SampleStretcher::hop**

| Condition | Side not taken | True / false | Category | Why | Pinned by |
|---|---|---|---|---|---|
| `seat == SeatResult::NOT_READY` | true | 0 / 8894 | RES | a hop's seat not ready or its walk failing (S4): the newer reads on and the hop comes again in 500 frames | kSeatFaults |
| `!resident(start)` | both | 0 / 0 | RES | inside a failed seat (S4): the hop-retry reservation kept where the start is not resident, let go where it is | sample_stretcher_spec ("keeps the newer head and retries in 500 frames when a seat is not ready (S4)") |

**deluge::synth::SampleStretcher::read_heads**

| Condition | Side not taken | True / false | Category | Why | Pinned by |
|---|---|---|---|---|---|
| `older_audible` | false | 887 / 0 | DEF | a CacheTail older head comes only from begin, with an over_cache_tail ramp at progress 0, and read_heads drops the older (set_heads gives the tail back) once its ramp is done |  |

**deluge::synth::SampleStretcher::render**

| Condition | Side not taken | True / false | Category | Why | Pinned by |
|---|---|---|---|---|---|
| `!sounding()` | true | 0 / 232 | DEF | render hops only while newer_reading(), so the hop's older_reads is true and every set_heads it makes (past the end, a failed seat, a seat) keeps a head sounding |  |

### `src/synth/sources/sample/stretch_context.h`

**deluge::synth::TailBuffers::lend**

| Condition | Side not taken | True / false | Category | Why | Pinned by |
|---|---|---|---|---|---|
| `free == 0` | true | 0 / 580 | MEM | all eight tail buffers lent | kAllocationFaults |

### `src/synth/sources/sample/stretch_position.h`

**deluge::synth::SourcePosition::from_play_byte**

| Condition | Side not taken | True / false | Category | Why | Pinned by |
|---|---|---|---|---|---|
| `bytes >= 0` | false | 1352 / 0 | OPEN | a stretch begun at a play byte before the data's start: reversed, just after leaving a cache (weShouldBeTimeStretchingNow's comment), or a fudge's begin there | kStartPositionSweep |

**deluge::synth::loop_step**

| Condition | Side not taken | True / false | Category | Why | Pinned by |
|---|---|---|---|---|---|
| `to_end < frames` | false | 30 / 0 | DEF | loop_step shortens only once the part's end passes the loop end; the guide's end and loop-end bytes are whole frames from the data's start (SamplePlaybackGuide, VoiceSamplePlaybackGuide), so then combined * frames exceeds the frames left and to_end < frames |  |

**deluge::synth::may_end**

| Condition | Side not taken | True / false | Category | Why | Pinned by |
|---|---|---|---|---|---|
| `!f.begun_this_block` | false | 738 / 0 | DEF | VoiceSample::render checks E1 only in its ratio-1.0 branch, apart from every begin (weShouldBeTimeStretchingNow runs when the ratio is not 1.0 and its cache switches jump past the check; a fudge's begin sets fudging, which skips it), and render clears the flag |  |
| `!f.fudge` | false | 738 / 0 | DEF | VoiceSample::render calls may_end only when !fudging, and fudging is set by every successful fudge begin and cleared only with the stretcher (endTimeStretching) |  |
| `f.ratio.raw() == (1u << 24)` | false | 738 / 0 | CON | the only caller, VoiceSample::render, passes Ratio::from_raw(kMaxSampleValue) |  |

### `src/synth/sources/sample/stretch_ratio.h`

**deluge::synth::ratio_undoing_pitch**

| Condition | Side not taken | True / false | Category | Why | Pinned by |
|---|---|---|---|---|---|
| `ratio > std::numeric_limits<uint32_t>::max()` | true | 0 / 38 | OPEN | a sample pitched 8 octaves or more below its root while stretched to hold its speed (e.g. oscillator transpose -96 and a note below the root): the ratio saturates at 2^32 - 1 (decision 3); a difference from the legacy, specced by hand in stretch_ratio_spec | stretch_ratio_spec ("saturates a ratio past 2^32 instead of wrapping"); decision 3 |
