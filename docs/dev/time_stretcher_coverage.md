# The time stretcher's coverage

How far the goldens reach the legacy time stretcher and its users' stretcher functions, and why each branch side they
do not reach is not reached. The rewrite in [time_stretcher_design.md](time_stretcher_design.md) is measured against
this list (its step 8).

**What was run.** `scripts/tasks/stretcher_coverage.sh` builds `golden_vt_render` with clang source coverage and runs,
through the gates' own checks, golden-synth (479 cases) and the 66 render-song gates whose output is compared with
`tests/golden/render/songs.json`. Only recorded renders count: a song gate's dry-twin and unswitched renders, and gates
that keep no audio record (streaming-margin, arrangement-take), are left out. The functions measured are the script's
`SCOPE` (`scripts/tasks/stretcher_coverage.py`): all of the stretcher's own files, and in its users the functions that
call into it, branch on whether a voice has one, or keep the hop budget and the culling bypass a hop sets; and every
function of the sample reader, which the rewrite replaces with a read head (the design note's decision 8). Its step 8
measures the read head in their place.

**Result.** 1,031 of the 1,374 branch sides are taken; the 343 untaken are listed below by file and function. A side is
named by its condition, the branch's source text; where one function tests the same text more than once, the call whose
result it tests is named (`after`), and repeats are counted in source order.

## Categories

| Category | Sides | Meaning |
|---|---|---|
| **Ruled out by the code** | 110 | |
| DEF | 78 | defensive: the code before it already rules the condition out (named per side) |
| CON | 32 | by construction: the condition is fixed by the code itself (named per side) |
| **Needs a fault or load** | 120 | |
| RES | 46 | residency: the sample data asked for is not resident (a card read still pending, a chunk evicted, or a card error). The goldens' card answers at once and their gates fail any underrun |
| MEM | 33 | memory: an allocation fails, or memory pressure steals a cache or perc-cache chunk; no golden runs out of memory |
| CULL | 41 | voice culling under CPU load, and the sinc window a reader shrinks to linear under it; the gates fail a render whose direness rises, so neither happens |
| **Reachable on the device, no gate scripts it** | 107 | |
| EDIT | 16 | an edit while a voice plays that the edit songs leave out: a clip-length edit of an unstretched, cached or releasing voice (`resumePlayback`), a clip reversed while it reads a cache, a long unreferenced sample auditioned, a reader reassessed past its end |
| LATE | 7 | a late start's paths render-audio-stretch-late leaves out: a late start while the previous sound fades, or one that restarts while releasing |
| STOP | 5 | a clip rendering after both clocks stop. A stop fast-releases the audio output's envelope at 65,536 per frame over a stage of 2^23, so it ends within the first 128-frame block and the voice is unassigned before it renders: a song stopped under a stretched clip renders no voice block after the stop. Only a render shorter than a block leaves a partial tail |
| REC | 13 | an audio clip recording, or an aborted recorder |
| EXPORT | 14 | the offline stem export's render |
| OPEN | 52 | reachable from a preset or song, not built: a third perc-cache chunk (a source over 2^23 frames), a crossfade from a cache whose read position is in a cache chunk's last frame, and the reader's content corners (a stereo sample read linearly while rendering in stereo, a cache written from a mono sample, tail zeros past a non-looping end, an end point on a chunk boundary, a pitch over four octaves up) |
| **Not a stretcher path** | 6 | |
| NS | 6 | a voice with no stretcher, or an unstretched cache; other goldens own it |

EDIT, LATE and STOP sit in the functions the rewrite changes (`AudioClip::processCurrentPos`,
`AudioClip::resumePlayback` and `AudioClip::render`, which reach the stretcher's queries and `end`). The design note's
decision 10 built render-song cases for them: render-audio-stretch-marker and render-audio-stretch-length resize a clip
while its stretched voice plays, and render-audio-stretch-late starts playback part-way through stretched clips. The
sides those leave are listed under EDIT and LATE; the STOP sides need a render shorter than a block, which no gate
makes. Those, the reversed-while-cached and auditioned EDIT sides, REC and EXPORT are accepted by name as unpinned.
How the RES, MEM, CULL and OPEN sides' legacy values are recorded is its decision 11. A side's reason is left as "—"
where its category's meaning gives it.

## Functions

| File (under `src/deluge/`) | Function | Calls | Lines run | Branch sides taken |
|---|---|---|---|---|
| `model/clip/audio_clip.cpp` | AudioClip::processCurrentPos | 10222 | 62/76 | 31/50 |
| `model/clip/audio_clip.cpp` | AudioClip::resumePlayback | 6 | 31/53 | 15/28 |
| `model/clip/audio_clip.cpp` | AudioClip::render | 223334 | 135/143 | 77/88 |
| `model/clip/audio_clip.cpp` | AudioClip::getLoopingType | 223337 | 6/6 | 8/8 |
| `model/clip/audio_clip.cpp` | AudioClip::getCullImmunity | 0 | 0/5 | 0/4 |
| `model/sample/sample.cpp` | Sample::fillPercCache | 21144 | 262/307 | 98/120 |
| `model/sample/sample.cpp` | Sample::getAveragesForCrossfade | 16974 | 68/89 | 29/36 |
| `model/sample/sample.cpp` | Sample::prepareToReadPercCache | 8864 | 41/46 | 20/22 |
| `model/sample/sample_low_level_reader.cpp` | SampleLowLevelReader::~SampleLowLevelReader | 12367 | 8/8 | 2/2 |
| `model/sample/sample_low_level_reader.cpp` | SampleLowLevelReader::ensureSource | 14565 | 8/12 | 5/6 |
| `model/sample/sample_low_level_reader.cpp` | SampleLowLevelReader::geometryFor | 9726 | 10/10 | 0/0 |
| `model/sample/sample_low_level_reader.cpp` | SampleLowLevelReader::unassignAllReasons | 40442 | 6/6 | 2/2 |
| `model/sample/sample_low_level_reader.cpp` | SampleLowLevelReader::getPlayByteLowLevel | 27325 | 12/12 | 6/6 |
| `model/sample/sample_low_level_reader.cpp` | SampleLowLevelReader::setupForPlayPosMovedIntoNewCluster | 20201 | 7/9 | 1/2 |
| `model/sample/sample_low_level_reader.cpp` | SampleLowLevelReader::misalignPlaybackParameters | 22552 | 5/5 | 0/0 |
| `model/sample/sample_low_level_reader.cpp` | SampleLowLevelReader::realignPlaybackParameters | 2351 | 4/4 | 0/0 |
| `model/sample/sample_low_level_reader.cpp` | SampleLowLevelReader::reassessReassessmentLocation | 2351 | 11/30 | 3/8 |
| `model/sample/sample_low_level_reader.cpp` | SampleLowLevelReader::setupReassessmentLocation | 22552 | 69/77 | 20/24 |
| `model/sample/sample_low_level_reader.cpp` | SampleLowLevelReader::setupClusersForInitialPlay | 2904 | 8/12 | 2/4 |
| `model/sample/sample_low_level_reader.cpp` | SampleLowLevelReader::setupClustersForPlayFromByte | 11945 | 11/17 | 3/6 |
| `model/sample/sample_low_level_reader.cpp` | SampleLowLevelReader::assignClusters | 14296 | 16/26 | 3/6 |
| `model/sample/sample_low_level_reader.cpp` | SampleLowLevelReader::moveOnToNextCluster | 8256 | 17/29 | 2/4 |
| `model/sample/sample_low_level_reader.cpp` | SampleLowLevelReader::changeClusterIfNecessary | 685088 | 27/38 | 10/14 |
| `model/sample/sample_low_level_reader.cpp` | SampleLowLevelReader::fillInterpolationBufferRetrospectively | 1144 | 20/23 | 7/8 |
| `model/sample/sample_low_level_reader.cpp` | SampleLowLevelReader::fillInterpolationBufferForward | 1144 | 16/21 | 6/8 |
| `model/sample/sample_low_level_reader.cpp` | SampleLowLevelReader::fillInterpolationBufferWithSilence | 0 | 0/11 | 0/4 |
| `model/sample/sample_low_level_reader.cpp` | SampleLowLevelReader::jumpBackSamples | 0 | 0/11 | 0/4 |
| `model/sample/sample_low_level_reader.cpp` | SampleLowLevelReader::considerUpcomingWindow | 630565 | 113/197 | 51/92 |
| `model/sample/sample_low_level_reader.cpp` | SampleLowLevelReader::bufferIndividualSampleForInterpolation | 159364 | 6/6 | 2/2 |
| `model/sample/sample_low_level_reader.cpp` | SampleLowLevelReader::bufferZeroForInterpolation | 17062 | 7/7 | 2/2 |
| `model/sample/sample_low_level_reader.cpp` | SampleLowLevelReader::jumpForwardZeroes | 11278 | 10/10 | 3/4 |
| `model/sample/sample_low_level_reader.cpp` | SampleLowLevelReader::jumpForwardLinear | 0 | 0/26 | 0/10 |
| `model/sample/sample_low_level_reader.cpp` | SampleLowLevelReader::readSamplesResampled | 123427 | 97/133 | 39/58 |
| `model/sample/sample_low_level_reader.cpp` | SampleLowLevelReader::readSamplesNative | 506040 | 31/31 | 8/8 |
| `model/sample/sample_low_level_reader.cpp` | SampleLowLevelReader::readSamplesForTimeStretching | 312989 | 23/25 | 7/8 |
| `model/sample/sample_low_level_reader.cpp` | SampleLowLevelReader::adoptResidencyFrom | 20748 | 17/17 | 5/6 |
| `model/sample/sample_low_level_reader.cpp` | SampleLowLevelReader::SampleLowLevelReader | 10374 | 6/6 | 0/0 |
| `model/sample/sample_low_level_reader.cpp` | SampleLowLevelReader::SampleLowLevelReader | 0 | 0/6 | 0/0 |
| `model/sample/sample_low_level_reader.cpp` | SampleLowLevelReader::operator= | 10374 | 14/16 | 1/2 |
| `model/sample/sample_low_level_reader.cpp` | void SampleLowLevelReader::readSamplesLinear<2, 2> | 0 | 54/54 | 0/14 |
| `model/sample/sample_low_level_reader.cpp` | void SampleLowLevelReader::readSamplesLinear<2, 2> | 0 | 17/20 | 0/0 |
| `model/sample/sample_low_level_reader.cpp` | void SampleLowLevelReader::readSamplesLinear<2, 1> | 1640 | 54/54 | 11/14 |
| `model/sample/sample_low_level_reader.cpp` | void SampleLowLevelReader::readSamplesLinear<2, 1> | 190126 | 17/20 | 0/0 |
| `model/sample/sample_low_level_reader.cpp` | void SampleLowLevelReader::readSamplesLinear<1, 1> | 588 | 54/54 | 11/14 |
| `model/sample/sample_low_level_reader.cpp` | void SampleLowLevelReader::readSamplesLinear<1, 1> | 68480 | 17/20 | 0/0 |
| `model/voice/voice_sample.cpp` | VoiceSample::beenUnassigned | 1943 | 4/4 | 0/0 |
| `model/voice/voice_sample.cpp` | VoiceSample::noteOn | 1943 | 10/10 | 0/0 |
| `model/voice/voice_sample.cpp` | VoiceSample::endTimeStretching | 2716 | 8/8 | 2/2 |
| `model/voice/voice_sample.cpp` | VoiceSample::fudgeTimeStretchingToAvoidClick | 112 | 19/28 | 3/6 |
| `model/voice/voice_sample.cpp` | VoiceSample::weShouldBeTimeStretchingNow | 197444 | 31/35 | 18/20 |
| `model/voice/voice_sample.cpp` | VoiceSample::render | 550692 | 598/695 | 240/304 |
| `model/voice/voice_sample.cpp` | VoiceSample::sampleZoneChanged | 22 | 104/118 | 50/60 |
| `model/voice/voice_sample.cpp` | VoiceSample::getPlaySample | 22666 | 9/9 | 2/2 |
| `processing/engines/audio_engine.cpp` | AudioEngine::cullVoices | 533154 | 27/41 | 11/20 |
| `processing/engines/audio_engine.cpp` | AudioEngine::renderAudio | 536397 | 32/32 | 4/4 |
| `processing/engines/audio_engine.cpp` | AudioEngine::renderAudioForStemExport | 0 | 0/38 | 0/14 |
| `processing/engines/audio_engine.cpp` | AudioEngine::solicitTimeStretcher | 1357 | 5/7 | 0/0 |
| `processing/engines/audio_engine.cpp` | AudioEngine::timeStretcherUnassigned | 1357 | 3/3 | 0/0 |
| `processing/engines/audio_engine.cpp` | AudioEngine::setDireness | 533158 | 14/23 | 6/10 |
| `dsp_legacy/timestretch/time_stretcher.h` | TimeStretcher::TimeStretcher | 1357 | 1/1 | 0/0 |
| `dsp_legacy/timestretch/time_stretcher.cpp` | TimeStretcher::init | 1357 | 46/51 | 10/12 |
| `dsp_legacy/timestretch/time_stretcher.cpp` | TimeStretcher::reInit | 260 | 7/7 | 2/2 |
| `dsp_legacy/timestretch/time_stretcher.cpp` | TimeStretcher::beenUnassigned | 1357 | 10/10 | 2/2 |
| `dsp_legacy/timestretch/time_stretcher.cpp` | TimeStretcher::unassignAllReasonsForPercLookahead | 1379 | 6/6 | 2/2 |
| `dsp_legacy/timestretch/time_stretcher.cpp` | TimeStretcher::unassignAllReasonsForPercCacheClusters | 1357 | 8/8 | 4/4 |
| `dsp_legacy/timestretch/time_stretcher.cpp` | TimeStretcher::unassignAllReasonsForHopBack | 1357 | 6/6 | 2/2 |
| `dsp_legacy/timestretch/time_stretcher.cpp` | TimeStretcher::keepClusterForHopRetry | 0 | 0/11 | 0/2 |
| `dsp_legacy/timestretch/time_stretcher.cpp` | TimeStretcher::releaseClusterForHopRetry | 10285 | 3/6 | 1/2 |
| `dsp_legacy/timestretch/time_stretcher.cpp` | TimeStretcher::hopEnd | 9017 | 361/388 | 129/140 |
| `dsp_legacy/timestretch/time_stretcher.cpp` | TimeStretcher::setupNewPlayHead | 9040 | 13/20 | 3/6 |
| `dsp_legacy/timestretch/time_stretcher.cpp` | TimeStretcher::allocateBuffer | 580 | 4/4 | 0/0 |
| `dsp_legacy/timestretch/time_stretcher.cpp` | TimeStretcher::readFromBuffer | 887 | 24/24 | 10/10 |
| `dsp_legacy/timestretch/time_stretcher.cpp` | TimeStretcher::rememberPercCacheCluster | 2598 | 9/11 | 5/6 |
| `dsp_legacy/timestretch/time_stretcher.cpp` | TimeStretcher::updateClustersForPercLookahead | 19877 | 11/11 | 2/2 |
| `dsp_legacy/timestretch/time_stretcher.cpp` | TimeStretcher::updateClustersForHopBack | 10173 | 19/21 | 5/6 |
| `dsp_legacy/timestretch/time_stretcher.cpp` | TimeStretcher::setupCrossfadeFromCache | 580 | 40/59 | 13/20 |
| `dsp_legacy/timestretch/time_stretcher.cpp` | TimeStretcher::getSamplePos | 205136 | 8/8 | 2/2 |
| `dsp_legacy/timestretch/time_stretcher.cpp` | getHopParams | 19190 | 44/44 | 10/10 |
| `dsp_legacy/timestretch/time_stretcher.cpp` | TimeStretcher::hopEnd (lambda) | 16684 | 8/10 | 3/4 |
| `dsp_legacy/timestretch/time_stretcher.cpp` | mixed_in_floor | 155422 | 6/6 | 0/0 |

`AudioEngine::cullVoices`'s counts after its early return read as a wrapped 64-bit value: the profile counters are not
atomic, and the harness renders on more than one thread, so a counter computed as a difference went below 0. Its
lines there count as run in the table above; they never ran (the render always fits its period).

## Untaken sides

### `src/deluge/dsp_legacy/timestretch/time_stretcher.cpp`

**TimeStretcher::init**

| Condition | Side not taken | True / false | Category | Why |
|---|---|---|---|---|
| `(newBytePos - startByte) * playDirection >= 0` | false | 112 / 0 | DEF | AudioClip::render offers a fudge only when the samples to the loop are within the pre-margin it measured |
| `!success` after `setupNewPlayHead` | true | 0 / 112 | RES | — |

**TimeStretcher::keepClusterForHopRetry**

| Condition | Side not taken | True / false | Category | Why |
|---|---|---|---|---|
| `hopRetry_ == nullptr` | both | 0 / 0 | RES | hop retry follows only a failed setupNewPlayHead |

**TimeStretcher::releaseClusterForHopRetry**

| Condition | Side not taken | True / false | Category | Why |
|---|---|---|---|---|
| `hopRetry_ != nullptr` | true | 0 / 10285 | RES | hop retry follows only a failed setupNewPlayHead |

**TimeStretcher::hopEnd**

| Condition | Side not taken | True / false | Category | Why |
|---|---|---|---|---|
| `!deluge::cluster::lease_count(percCacheClustersNearby[l]->resource_slot)` | true | 0 / 643 | DEF | i036: a remembered perc-cache cluster with no lease |
| `playbackHandler.isEitherClockActive()` | false | 8306 / 0 | STOP | a hop falling due with the clocks stopped; a stop's tail never renders a block (see STOP) |
| `sourceSamplesTilLoop >= 0` | false | 1899 / 0 | STOP | stretch position past the loop end, which the comment ties to stopping playback |
| `samplesTilHopEnd < 1` | true | 0 / 8864 | DEF | the beam width is multiplied by the phase increment and divided by it again; below 1 needs a phase increment under about 5,600 (11.5 octaves down), where the ratio 2^48 / increment has long overflowed and Sample::getOrCreateCache divides by zero first (SIGFPE: TODO.md, and the design note's decision 3) |
| `newHeadBytePos < (int32_t)sample->audioDataStartPosBytes` | true | 0 / 8104 | DEF | E285: the new head is clamped to the waveform's start above |
| `false` | true | 0 / 7909 | CON | `if (false)`, a label holder |
| `window.frames == nullptr` | true | 0 / 84471 | RES | — |
| `!success` | true | 0 / 8928 | RES | setupNewPlayHead fails only when its cluster is not ready |
| `deluge_sample_peek(deluge::sample::source_id_for(*sample), wantedFrame, static_cast<int8_t>(playDirection)) .frames == nullptr` | both | 0 / 0 | RES | as above |
| `!olderHeadReadingFromBuffer` | false | 25 / 0 | CON | olderHeadReadingFromBuffer is set false at the top of hopEnd, as its comment says |

**TimeStretcher::hopEnd (lambda)**

| Condition | Side not taken | True / false | Category | Why |
|---|---|---|---|---|
| `deluge_sample_peek(deluge::sample::source_id_for(*sample), frame, static_cast<int8_t>(playDirection)) .frames == nullptr` | true | 0 / 460 | RES | the region before the start marker not yet loaded |

**TimeStretcher::setupNewPlayHead**

| Condition | Side not taken | True / false | Category | Why |
|---|---|---|---|---|
| `voiceSample->setupClustersForPlayFromByte(guide, sample, newHeadBytePos, priorityRating) != RegionOutcome::Ready` | true | 0 / 9040 | RES | — |
| `!success` after `changeClusterIfNecessary` | true | 0 / 9040 | RES | — |
| `!voiceSample->hasCurrentRegion()` | true | 0 / 9040 | RES | a Ready setup that holds no region |

**TimeStretcher::rememberPercCacheCluster**

| Condition | Side not taken | True / false | Category | Why |
|---|---|---|---|---|
| `percCacheClustersNearby[0]` | true | 0 / 6 | OPEN | a third distinct perc-cache cluster remembered: playback crossing two perc-cache cluster boundaries, so a source over 2^23 frames (16 MB mono); LONG.WAV is 2^22 + 65,536 |

**TimeStretcher::updateClustersForHopBack**

| Condition | Side not taken | True / false | Category | Why |
|---|---|---|---|---|
| `lastSample < 0` | true | 0 / 10173 | DEF | a sample with no frames; it cannot load |

**TimeStretcher::setupCrossfadeFromCache**

| Condition | Side not taken | True / false | Category | Why |
|---|---|---|---|---|
| `bytesTilCacheEnd <= kCacheByteDepth * numChannels` | true | 0 / 580 | DEF | nothing left in the cache at the switch: the near-end switch fires at least 72 frames early (200-frame threshold, 128-frame blocks), and a finished cache wraps at its loop end |
| `!cacheCluster` after `getCluster` | true | 0 / 580 | DEF | E178 |
| `bytesTilCacheClusterEnd <= kCacheByteDepth * numChannels` | true | 0 / 580 | OPEN | the read position in the last frame of a cache cluster at the switch: an alignment of 1 in about 5,400 frames |
| `!buffer` | false | 580 / 0 | CON | both callers' stretcher was just solicited (switching to read a cache ends stretching), and init() leaves no buffer |
| `!success` after `allocateBuffer` | true | 0 / 580 | MEM | — |
| `originalCacheWriteBytePos != cache->writeBytePos` | true | 0 / 580 | MEM | the buffer allocation stole from the cache |
| `numSamplesThisCacheRead <= 0` | true | 0 / 580 | DEF | E179: the window is at least a frame and a fresh stretcher's first hop at least 168 frames |

### `src/deluge/model/clip/audio_clip.cpp`

**AudioClip::processCurrentPos**

| Condition | Side not taken | True / false | Category | Why |
|---|---|---|---|---|
| `modelStack->getTimelineCounter() != this` | true | 0 / 10222 | REC | the clip replaced while it processes its position: an arrangement take (arrangement-take, which compares no audio, reached it) |
| `recorder` | true | 0 / 10222 | REC | — |
| `recorder->status == RecorderStatus::ABORTED` | both | 0 / 0 | REC | — |
| `getCurrentlyRecordingLinearly()` (first of 2) | true | 0 / 8101 | REC | pos 0 only at a linear recording's restart |
| `lastProcessedPos == nextSampleRestartPos` | both | 0 / 0 | REC | — |
| `getCurrentlyRecordingLinearly()` (second of 2) | true | 0 / 2121 | REC | — |
| `originalLength != 0` | both | 0 / 0 | REC | — |
| `originalLength < playbackHandler.swungTicksTilNextEvent` | both | 0 / 0 | REC | — |
| `sampleHolder.audioFile` | false | 2121 / 0 | DEF | a clip with no file or an unplayable one; every generated song's samples load |
| `!((Sample*)sampleHolder.audioFile)->unplayable` | false | 2121 / 0 | DEF | as above |
| `((AudioOutput*)output)->envelope.stage() >= Envelope::Stage::RELEASE` | true | 0 / 1957 | LATE | the envelope releasing at the clip's start, after a restart part-way |
| `!doingLateStart` | both | 0 / 0 | LATE | — |
| `!voiceSample` after `AudioEngine::solicitVoiceSample` | true | 0 / 164 | MEM | the voice pool empty |
| `outcome != RegionOutcome::Ready` | true | 0 / 1471 | RES | — |

**AudioClip::resumePlayback**

| Condition | Side not taken | True / false | Category | Why |
|---|---|---|---|---|
| `!playbackHandler.isEitherClockActive()` | true | 0 / 6 | EDIT | resumePlayback runs at an arrangement instance's start or after a clip-length edit (Song::setClipLength); a resume with the clocks stopped or the clip inactive |
| `!modelStack->song->isClipActive(this)` | true | 0 / 6 | EDIT | as above |
| `!sampleHolder.audioFile` | true | 0 / 6 | DEF | an unplayable sample |
| `((Sample*)sampleHolder.audioFile)->unplayable` | true | 0 / 6 | DEF | as above |
| `voiceSample->cache` | true | 0 / 2 | EDIT | a clip-length edit while its live voice reads a cache: the edit songs' voices stretch without one |
| `!success` after `stopUsingCache` | both | 0 / 0 | EDIT | as above |
| `sequenceSyncStartedNumTicksAgo < 0` | true | 0 / 6 | DEF | 'Shouldn't happen' |
| `!mayMakeSound` | true | 0 / 6 | REC | a resume of a clip that may not sound: an arrangement take, as `processCurrentPos`'s replaced-clip check |
| `voiceSample->timeStretcher` | false | 2 / 0 | EDIT | a clip-length edit while its live voice plays unstretched (render-audio-stretch-marker and -length take the stretching side) |
| `((AudioOutput*)output)->envelope.stage() < Envelope::Stage::RELEASE` | false | 2 / 0 | EDIT | a clip-length edit while the clip's envelope releases |
| `voiceSample` (third of 3) | true | 0 / 4 | EDIT | as the two above: the stretching, non-releasing voice returns before it |
| `!voiceSample` after `AudioEngine::solicitVoiceSample` | true | 0 / 4 | MEM | the voice pool empty |

**AudioClip::render**

| Condition | Side not taken | True / false | Category | Why |
|---|---|---|---|---|
| `!voiceSample` | true | 0 / 223334 | DEF | AudioOutput renders the clip only while it has a voice |
| `static_cast<AudioOutput*>(this->output)->envelope.stage() < Envelope::Stage::FAST_RELEASE` | false | 2 / 0 | LATE | a late start while the clip's previous sound fades out: a second <> + play in quick succession |
| `result == LateStartAttemptStatus::FAILURE` | true | 0 / 1 | RES | the late start's chunk failing, not just pending (render-audio-stretch-late takes the wait) |
| `!playbackHandler.isEitherClockActive()` | true | 0 / 16409 | STOP | a near-native clip rendering with the clocks stopped: a stop's fast release ends within its first 128-frame block, so the voice is gone before it renders (see STOP); `doingLateStart` is LATE's |
| `doingLateStart` (second of 2) | true | 0 / 16409 | LATE | — |
| `playbackHandler.isEitherClockActive()` | false | 6770 / 0 | STOP | a tied clip without pitch change rendering with the clocks stopped, as above |
| `!doingLateStart` | false | 6770 / 0 | LATE | — |
| `playbackHandler.playbackState` | false | 223333 / 0 | STOP | playbackState false: a clip rendering after playback stopped (arrangement-take, which compares no audio, reached it) |
| `!success` after `fudgeTimeStretchingToAvoidClick` | true | 0 / 112 | MEM | fudge setup failing (no stretcher, or its play-head's cluster not ready: RES) |
| `cutPos < 2147483647` | false | 14 / 0 | NS | a clip that neither loops nor ever cuts; not a stretcher path |
| `!everythingOk` after `possiblySetUpCache` | true | 0 / 1471 | MEM | cache setup failing (or RES) |

**AudioClip::getCullImmunity**

| Condition | Side not taken | True / false | Category | Why |
|---|---|---|---|---|
| `voiceSample` | both | 0 / 0 | CULL | — |
| `voiceSample->timeStretcher` | both | 0 / 0 | CULL | — |

### `src/deluge/model/sample/sample.cpp`

**Sample::fillPercCache**

| Condition | Side not taken | True / false | Category | Why |
|---|---|---|---|---|
| `startPosSamples >= lengthInSamples` | true | 0 / 20691 | DEF | the caller does not call again once filled to the waveform's end (filledToEndOfWaveform) |
| `startPosSamples < 0` | true | 0 / 453 | DEF | as above, reversed |
| `!percCacheClusters[reversed]` | true | 0 / 2 | MEM | — |
| `(mgr != nullptr)` | false | 2 / 0 | DEF | no resource manager |
| `percCacheAssetId[reversed] == DELUGE_RESOURCE_NO_ASSET` | true | 0 / 2 | DEF | RPC1: asset table full |
| `isProjectReferenced()` | false | 2 / 0 | EDIT | a long sample not referenced by the project: auditioned from the browser |
| `!percCacheMemory[reversed]` | true | 0 / 102 | MEM | — |
| `percClusterIndexStart >= numPercCacheClusters` | true | 0 / 1280 | DEF | E138 |
| `!clusterHere` | true | 0 / 1280 | MEM | a perc-cache cluster stolen |
| `startPosSamples & ((1 << (Cluster::size_magnitude + kPercBufferReductionMagnitude)) - 1)` | both | 0 / 0 | MEM | as above |
| `clusterHere` | false | 1280 / 0 | MEM | as above |
| `percClusterIndexEnd >= numPercCacheClusters` | true | 0 / 6 | DEF | E140 |
| `!percCacheClusters[reversed][percClusterIndexEnd]` | true | 0 / 6 | MEM | E141, a stolen cluster |
| `numSamples <= 0` | true | 0 / 19877 | DEF | nothing to fill |
| `sourceClusterIndex >= getFirstClusterIndexWithNoAudioData()` | true | 0 / 21217 | DEF | a source cluster outside the audio data |
| `sourceClusterIndex < getFirstClusterIndexWithAudioData()` | true | 0 / 21217 | DEF | as above |
| `percClusterIndex >= numPercCacheClusters` | true | 0 / 1312 | DEF | perc-cache cluster index past the end |
| `p == nullptr` | true | 0 / 4 | MEM | — |
| `window.frames == nullptr` | true | 0 / 21217 | RES | — |
| `true` | false | 11571017 / 0 | CON | `while (true)` |
| `percCacheZone->endPos == percCacheZone->startPos` | true | 0 / 19877 | DEF | an empty zone |

**Sample::getAveragesForCrossfade**

| Condition | Side not taken | True / false | Category | Why |
|---|---|---|---|---|
| `startBytePos < (int32_t)audioDataStartPosBytes` | true | 0 / 16974 | DEF | ALPHA check |
| `readByteOutOfRange()` (first of 2) | true | 0 / 48039 | DEF | hopEnd bounds the read |
| `readByteOutOfRange()` (second of 2) | true | 0 / 48209 | DEF | as above |
| `whichCluster < getFirstClusterIndexWithAudioData()` | true | 0 / 48209 | DEF | as above |
| `whichCluster >= getFirstClusterIndexWithNoAudioData()` | true | 0 / 48209 | DEF | as above |
| `window.frames == nullptr` | true | 0 / 48209 | RES | — |
| `numSamplesLeftThisAverage < 0` | true | 0 / 48209 | DEF | ALPHA check |

**Sample::prepareToReadPercCache**

| Condition | Side not taken | True / false | Category | Why |
|---|---|---|---|---|
| `i >= static_cast<int32_t>(percCacheZones[reversed].size())` | true | 0 / 8862 | DEF | no zone at the beam: hopEnd reads only after a fill |
| `!percCacheClusters[reversed][ourCluster]` | true | 0 / 435 | MEM | a stolen perc-cache cluster |

### `src/deluge/model/sample/sample_low_level_reader.cpp`

**SampleLowLevelReader::readSamplesForTimeStretching**

| Condition | Side not taken | True / false | Category | Why |
|---|---|---|---|---|
| `hasCurrentRegion()` | true | 0 / 1036 | DEF | every failed window check leaves the reader holding no region, so this card-error exit is never taken (the design note's findings) |

**SampleLowLevelReader::ensureSource**

| Condition | Side not taken | True / false | Category | Why |
|---|---|---|---|---|
| `source_backing_ != backing` | true | 0 / 4839 | DEF | a reader opens its source once per life: the pools run its destructor on recycle, and no reader moves to another sample |

**SampleLowLevelReader::setupForPlayPosMovedIntoNewCluster**

| Condition | Side not taken | True / false | Category | Why |
|---|---|---|---|---|
| `!hasCurrentRegion()` | true | 0 / 20201 | DEF | i022: both callers pass the region they have just acquired |

**SampleLowLevelReader::reassessReassessmentLocation**

| Condition | Side not taken | True / false | Category | Why |
|---|---|---|---|---|
| `!hasCurrentRegion()` | true | 0 / 2351 | EDIT | a reassessment of a reader past the waveform's end: a start marker moved (`sampleZoneChanged`) or a cache dropped during its tail |
| `(clusterIndex - finalClusterIndex) * guide->playDirection > 0` | true | 0 / 2351 | EDIT | the reader past its final chunk after a cache read is abandoned mid-note (a tempo or pitch change, a reversal) |
| `!deluge_sample_region_acquire(source_, static_cast<uint32_t>(finalClusterIndex), guide->playDirection, static_cast<uint32_t>(1), &finalRegion)` | both | 0 / 0 | EDIT | inside the past-the-final-chunk branch above |
| `assignClusters(guide, sample, clusterIndex, priorityRating) != RegionOutcome::Ready` | true | 0 / 2351 | MEM | the chunk was pinned a moment before, so only `ensureSource` finding the source pool exhausted fails it |

**SampleLowLevelReader::setupReassessmentLocation**

| Condition | Side not taken | True / false | Category | Why |
|---|---|---|---|---|
| `!hasCurrentRegion()` | true | 0 / 22552 | DEF | i021: both callers reach it straight after a Ready acquire |
| `bytePosWithinClusterToStopAt == 0` | true | 0 / 5872 | OPEN | a forwards end point exactly on a chunk boundary: an end marker or the audio data ending at a multiple of the chunk size |
| `bytePosWithinClusterToStopAt > Cluster::size - bytesPerSample` | true | 0 / 1059 | OPEN | a reversed stop point in a chunk's last frame: a start marker or the audio data starting on a chunk boundary |
| `(endPosWithinCurrentCluster + currentClusterIndex * Cluster::size - sample->audioDataStartPosBytes) % bytesPerSample` | true | 0 / 15439 | DEF | E163: `excess` is the frame remainder, so the end position always lands on a frame boundary |

**SampleLowLevelReader::setupClusersForInitialPlay**

| Condition | Side not taken | True / false | Category | Why |
|---|---|---|---|---|
| `sample->unplayable` | true | 0 / 2904 | DEF | `note_on` and AudioClip test it first; it is set only on a card swap, after `killAllVoices` |
| `outcome != RegionOutcome::Ready` | true | 0 / 2904 | RES | — |

**SampleLowLevelReader::setupClustersForPlayFromByte**

| Condition | Side not taken | True / false | Category | Why |
|---|---|---|---|---|
| `startPlaybackAtByte < sample->audioDataStartPosBytes` | true | 0 / 11945 | OPEN | a reversed low-level loop restart whose overshoot carries past the data start: a loop near it shorter than one output frame's jump |
| `startPlaybackAtByte >= sample->audioDataStartPosBytes + sample->audioDataLengthBytes` | true | 0 / 11945 | OPEN | as above, forwards: a loop ending at the data end, shorter than the overshoot at a high pitch |
| `outcome != RegionOutcome::Ready` | true | 0 / 11945 | RES | — |

**SampleLowLevelReader::assignClusters**

| Condition | Side not taken | True / false | Category | Why |
|---|---|---|---|---|
| `hasCurrentRegion()` | true | 0 / 14296 | DEF | i024: every caller unassigns the reader first |
| `source_ == nullptr` | true | 0 / 14296 | MEM | the source pool exhausted: `deluge_sample_source_open` returns null |
| `outcome != RegionOutcome::Ready` | true | 0 / 14296 | RES | — |

**SampleLowLevelReader::moveOnToNextCluster**

| Condition | Side not taken | True / false | Category | Why |
|---|---|---|---|---|
| `!hasCurrentRegion()` | true | 0 / 8256 | DEF | i019: it is reached only on a NEXT_CLUSTER reassessment, which `setupReassessmentLocation` sets while a region is held |
| `!deluge_sample_region_acquire(source_, static_cast<uint32_t>(newClusterIndex), guide->playDirection, static_cast<uint32_t>(priorityRating), &region)` | true | 0 / 8256 | RES | the next chunk not ready; also a null-source copy (a unison clone after a unison-count edit) at its first chunk boundary |

**SampleLowLevelReader::changeClusterIfNecessary**

| Condition | Side not taken | True / false | Category | Why |
|---|---|---|---|---|
| `true` | false | 694306 / 0 | CON | `while (true)`: the loop leaves only by break or return |
| `!success` after `moveOnToNextCluster` | true | 0 / 8256 | RES | — |
| `setupClusersForInitialPlay(guide, sample, byteOvershoot, true, priorityRating) != RegionOutcome::Ready` | true | 0 / 961 | RES | a loop restart's chunk not ready |
| `count >= 1024` | true | 0 / 9217 | DEF | E227: each pass crosses a chunk or restarts a loop at least a frame long, and one call's overshoot is under 128 frames |

**SampleLowLevelReader::fillInterpolationBufferRetrospectively**

| Condition | Side not taken | True / false | Category | Why |
|---|---|---|---|---|
| `!hasCurrentRegion()` | true | 0 / 1144 | RES | a head starting to interpolate with no region, which follows only a setup that came back holding none |

**SampleLowLevelReader::fillInterpolationBufferForward**

| Condition | Side not taken | True / false | Category | Why |
|---|---|---|---|---|
| `!hasCurrentRegion()` | true | 0 / 1144 | RES | as above |
| `!stillGoing` | true | 0 / 8886 | OPEN | a resampled start within half a sinc window (8 frames) of a non-looping end, e.g. a stretch hop landing there; else RES |

**SampleLowLevelReader::fillInterpolationBufferWithSilence**

| Condition | Side not taken | True / false | Category | Why |
|---|---|---|---|---|
| `feed.remaining() != 0` | both | 0 / 0 | OPEN | 0 calls: reached only from the forward fill's no-region and end-of-waveform exits, above |
| `(uintptr_t)currentPlayPos >= interpolationBufferSize` | both | 0 / 0 | OPEN | as above |

**SampleLowLevelReader::jumpBackSamples**

| Condition | Side not taken | True / false | Category | Why |
|---|---|---|---|---|
| `numToJumpBack--` | both | 0 / 0 | OPEN | 0 calls: a resampling voice returning to its native rate mid-note, or its sinc window shrinking under CPU direness (CULL) |
| `bytesPastClusterStart < 0` | both | 0 / 0 | OPEN | as above, with the jump back crossing the chunk's start |

**SampleLowLevelReader::considerUpcomingWindow**

| Condition | Side not taken | True / false | Category | Why |
|---|---|---|---|---|
| `phaseIncrement < 0` | true | 0 / 630565 | DEF | E228: the note-on drops any phase increment at or above 2^31 |
| `!success` (first of 2) | true | 0 / 1144 | RES | the forward fill fails only after a loop restart's chunk is not ready, which leaves the play pointer stale |
| `hasCurrentRegion()` (first of 7) | false | 1144 / 0 | OPEN | a forward fill reaching a non-looping end succeeds holding no region (see `!stillGoing`) |
| `bytesLeftWhichMayBeRead < 0` (first of 9) | true | 0 / 1144 | DEF | E222: each read follows `changeClusterIfNecessary`, which leaves the play position short of the reassessment point; one frame on reaches it at most |
| `interpolationBufferSizeLastTime != interpolationBufferSize` | true | 0 / 122331 | CULL | the window changes size only when CPU direness drops sinc to linear mid-note (or the interpolation mode is edited mid-note) |
| `interpolationBufferSize < interpolationBufferSizeLastTime` | both | 0 / 0 | CULL | as above |
| `hasCurrentRegion()` (second of 7) | both | 0 / 0 | CULL | as above |
| `bytesLeftWhichMayBeRead < 0` (second of 9) | both | 0 / 0 | CULL | as above |
| `hasCurrentRegion()` (third of 7) | both | 0 / 0 | CULL | as above |
| `bytesLeftWhichMayBeRead < 0` (third of 9) | both | 0 / 0 | CULL | as above |
| `hasCurrentRegion()` (fourth of 7) | both | 0 / 0 | CULL | as above |
| `bytesLeftWhichMayBeRead < 0` (fourth of 9) | both | 0 / 0 | CULL | as above |
| `!success` (second of 2) | both | 0 / 0 | CULL | as above |
| `hasCurrentRegion()` (fifth of 7) | both | 0 / 0 | CULL | as above |
| `bytesLeftWhichMayBeRead < 0` (fifth of 9) | both | 0 / 0 | CULL | as above |
| `!hasCurrentRegion()` (second of 6) | false | 689 / 0 | DEF | every false return from `changeClusterIfNecessary` leaves the reader holding no region |
| `!hasCurrentRegion()` (third of 6) | true | 0 / 159364 | DEF | E225: the walk returned true for a reader that held a region |
| `bytesLeftWhichMayBeRead <= 0` (first of 2) | true | 0 / 159364 | DEF | E226: the walk just left the play position short of the reassessment point |
| `bytesLeftWhichMayBeRead < 0` (sixth of 9) | true | 0 / 159364 | DEF | E185: one frame on from short of the reassessment point reaches it at most |
| `hasCurrentRegion()` (seventh of 7) | false | 645 / 0 | OPEN | a voice pitched below its native rate running its tail zeros after a non-looping end |
| `bytesLeftWhichMayBeRead < 0` (seventh of 9) | true | 0 / 645 | DEF | E223: nothing moved since the last window, which ended at or short of the reassessment point |
| `bytesLeftWhichMayBeRead < 0` (eighth of 9) | true | 0 / 121562 | DEF | the jump loop's walk and one-frame step, or the last shortened window, leave the play position at or short of the reassessment point |
| `phaseIncrementingLeftWhichMayBeDone < 0` | true | 0 / 4078 | DEF | E000/E143: (frames left + 1) << 24, less an `oscPos` under 2^24, less 1, is never negative |
| `!hasCurrentRegion()` (fifth of 6) | both | 0 / 0 | DEF | as above |
| `interpolationBufferSizeLastTime` | true | 0 / 507090 | OPEN | a resampling voice returning to exactly its native rate mid-note, e.g. a pitch bend back to zero on a note at the sample's root |
| `!hasCurrentRegion()` (sixth of 6) | both | 0 / 0 | OPEN | as above |
| `bytesLeftWhichMayBeRead < 0` (ninth of 9) | true | 0 / 506037 | DEF | `changeClusterIfNecessary` just left the play position short of the reassessment point |
| `bytesLeftWhichMayBeRead <= 0` (second of 2) | true | 0 / 506037 | DEF | E001: as above, so at least one byte is left |
| `*numSamples <= 0` | true | 0 / 7492 | DEF | E147: reassessment points sit on frame boundaries, so the bytes left hold at least one frame |

**SampleLowLevelReader::jumpForwardZeroes**

| Condition | Side not taken | True / false | Category | Why |
|---|---|---|---|---|
| `numSamplesToJumpForward` | false | 11278 / 0 | OPEN | a voice pitched below its native rate playing tail zeros past its end: a phase step that crosses no frame |

**SampleLowLevelReader::jumpForwardLinear**

| Condition | Side not taken | True / false | Category | Why |
|---|---|---|---|---|
| `numSamplesToJumpForward != 0` | both | 0 / 0 | DEF | 0 calls: its only call sits under `hasCurrentRegion()` inside the no-region linear branch, which rules it out |
| `numSamplesToJumpForward > 2` | both | 0 / 0 | DEF | as above |
| `numChannels == 2` | both | 0 / 0 | DEF | as above |
| `numSamplesToJumpForward >= 2` (first of 2) | both | 0 / 0 | DEF | as above |
| `numSamplesToJumpForward >= 2` (second of 2) | both | 0 / 0 | DEF | as above |

**void SampleLowLevelReader::readSamplesLinear<1, 1>**

| Condition | Side not taken | True / false | Category | Why |
|---|---|---|---|---|
| `kStereo` (first of 2) | true | 0 / 588 | CON | `kStereo` is false in the mono instantiation |
| `kStereo` (second of 2) | true | 0 / 588 | CON | `kStereo` is false in the mono instantiation |
| `skipFirstJump` | false | 588 / 0 | CON | both callers of `readSamplesResampled` pass a fresh `doneAnySamplesYet = false`, so the first jump is always skipped |

**void SampleLowLevelReader::readSamplesLinear<2, 1>**

| Condition | Side not taken | True / false | Category | Why |
|---|---|---|---|---|
| `kStereo` (first of 2) | false | 1640 / 0 | CON | `kStereo` is true in the stereo instantiation |
| `kStereo` (second of 2) | false | 1640 / 0 | CON | `kStereo` is true in the stereo instantiation |
| `skipFirstJump` | false | 1640 / 0 | CON | as above |

**void SampleLowLevelReader::readSamplesLinear<2, 2>**

| Condition | Side not taken | True / false | Category | Why |
|---|---|---|---|---|
| `kStereo` (first of 2) | both | 0 / 0 | OPEN | 0 calls: a stereo sample read with linear interpolation while rendering in stereo (`renderInStereo`: headphones or line out); the goldens mostly render mono |
| `kStereo` (second of 2) | both | 0 / 0 | OPEN | 0 calls: a stereo sample read linearly while rendering in stereo, as above |
| `skipFirstJump` | both | 0 / 0 | OPEN | 0 calls: a stereo sample read linearly while rendering in stereo |
| `out != oscBufferEnd` | both | 0 / 0 | OPEN | as above |
| `numSamplesToJumpForward != 0` | both | 0 / 0 | OPEN | as above |
| `numSamplesToJumpForward > 2` | both | 0 / 0 | OPEN | as above |
| `numSamplesToJumpForward >= 2` | both | 0 / 0 | OPEN | as above |

**SampleLowLevelReader::readSamplesResampled**

| Condition | Side not taken | True / false | Category | Why |
|---|---|---|---|---|
| `!*doneAnySamplesYet` (first of 2) | false | 121199 / 0 | CON | both callers pass a fresh `doneAnySamplesYet = false` |
| `numSamplesToJumpForward > kInterpolationMaxNumSamples` | true | 0 / 14336967 | OPEN | a sinc read stepping over 16 source frames per output frame: a sample pitched more than four octaves up |
| `true` (first of 2) | false | 18707111 / 0 | CON | `while (true)`: the loop leaves only by goto |
| `true` (second of 2) | false | 26652 / 0 | CON | as above |
| `numChannels == 2` (second of 5) | false | 12753370 / 0 | OPEN | a cache written from a mono sample; every cache the goldens write is from a stereo sample |
| `hasCurrentRegion()` (second of 3) | false | 2228 / 0 | OPEN | a linear-interpolation voice playing tail zeros after a non-looping end |
| `numChannelsAfterCondensing == 2` (second of 3) | true | 0 / 1640 | OPEN | a stereo sample read linearly while rendering in stereo (`renderInStereo`: headphones or line out) |
| `!*doneAnySamplesYet` (second of 2) | both | 0 / 0 | OPEN | the no-region linear branch never runs: a linear voice playing tail zeros after a non-looping end |
| `hasCurrentRegion()` (third of 3) | both | 0 / 0 | OPEN | as above; its true side is also ruled out by the branch's own no-region test |
| `numChannels == 2` (fifth of 5) | both | 0 / 0 | OPEN | the no-region linear branch never runs, as above |
| `numChannelsAfterCondensing == 1` (second of 2) | both | 0 / 0 | OPEN | as above |
| `numChannelsAfterCondensing == 2` (third of 3) | both | 0 / 0 | OPEN | as above |
| `oscBufferPosNow != oscBufferEnd` (second of 2) | both | 0 / 0 | OPEN | as above |

**SampleLowLevelReader::adoptResidencyFrom**

| Condition | Side not taken | True / false | Category | Why |
|---|---|---|---|---|
| `region_.lease != 0` | false | 1245 / 0 | OPEN | a non-stealing copy of a reader holding no region: a unison clone or a stretch starting during a voice's tail past its end |

**SampleLowLevelReader::operator=**

| Condition | Side not taken | True / false | Category | Why |
|---|---|---|---|---|
| `this == &other` | true | 0 / 10374 | DEF | every move-assignment takes a freshly built temporary `SampleLowLevelReader(...)` |

### `src/deluge/model/voice/voice_sample.cpp`

**VoiceSample::fudgeTimeStretchingToAvoidClick**

| Condition | Side not taken | True / false | Category | Why |
|---|---|---|---|---|
| `!timeStretcher` after `AudioEngine::solicitTimeStretcher` | true | 0 / 112 | MEM | — |
| `!success` | true | 0 / 112 | RES | — |
| `!success` after `reassessReassessmentLocation` | true | 0 / 112 | RES | — |

**VoiceSample::weShouldBeTimeStretchingNow**

| Condition | Side not taken | True / false | Category | Why |
|---|---|---|---|---|
| `!timeStretcher` after `AudioEngine::solicitTimeStretcher` | true | 0 / 1245 | MEM | — |
| `!success` after `reassessReassessmentLocation` | true | 0 / 1245 | RES | — |

**VoiceSample::render**

| Condition | Side not taken | True / false | Category | Why |
|---|---|---|---|---|
| `phaseIncrement != kMaxSampleValue` (first of 2) | false | 152517 / 0 | CON | a voice sets up a cache only off its native rate |
| `interpolationBufferSize <= 2` | true | 0 / 152516 | CULL | an interpolation buffer of 2 or less while writing a cache: the interpolation-mode test before it short-circuits for linear, and the voice's buffer shrinks only under CPU direness |
| `writingToCache` (first of 2) | both | 0 / 0 | CULL | as above |
| `cache->timeStretchRatio != kMaxSampleValue` | false | 4 / 0 | NS | leaving an unstretched cache |
| `!success` after `stopUsingCache` (first of 5) | true | 0 / 5 | RES | — |
| `!success` after `weShouldBeTimeStretchingNow` (first of 2) | true | 0 / 4 | RES | — |
| `cache->writeBytePos < cacheBytePos` (first of 4) | true | 0 / 152516 | MEM | a cache cluster stolen |
| `!success` after `stopUsingCache` (second of 5) | both | 0 / 0 | MEM | — |
| `cache->writeBytePos > cacheBytePos` | true | 0 / 108735 | DEF | 'Could this really happen?': a second writer of the same cache |
| `!success` after `stopReadingFromCache` | true | 0 / 576 | RES | — |
| `!success` after `weShouldBeTimeStretchingNow` (second of 2) | true | 0 / 576 | RES | — |
| `cache->writeBytePos < cacheBytePos` (second of 4) | true | 0 / 576 | MEM | the cache stolen during the switch |
| `phaseIncrement != kMaxSampleValue` (second of 2) | false | 576 / 0 | CON | as the same test where the cache's parameters are checked: a voice sets up a cache only off its native rate |
| `interpolationBufferSize != kInterpolationMaxNumSamples` (first of 2) | true | 0 / 576 | CON | a change to linear interpolation is caught first, where the cache's parameters are checked at the render's start |
| `!stillGoing` after `weShouldBeTimeStretchingNow` | true | 0 / 196864 | RES | or MEM |
| `writingToCache` (second of 2) | false | 7897 / 0 | CON | inside `!cache || writingToCache`, a cache is being written |
| `cache->writeBytePos < cacheBytePos` (third of 4) | true | 0 / 7897 | MEM | — |
| `!success` after `stopUsingCache` (third of 5) | both | 0 / 0 | MEM | — |
| `!stillGoing` (first of 2) | true | 0 / 10 | RES | — |
| `!stillGoing` (second of 2) | true | 0 / 10 | RES | — |
| `timeStretchRatio != kMaxSampleValue` | true | 0 / 1 | DEF | E240 |
| `phaseIncrement == kMaxSampleValue` | true | 0 / 1 | DEF | E241 |
| `!stopReadingFromCache()` | true | 0 / 1 | RES | — |
| `interpolationBufferSize != kInterpolationMaxNumSamples` (second of 2) | true | 0 / 1 | NS | linear interpolation at the written end of an unstretched cache |
| `!stillGoing` after `reassessReassessmentLocation` | true | 0 / 1 | RES | — |
| `!stillGoing` after `changeClusterIfNecessary` | true | 0 / 1 | RES | — |
| `bytesTilCacheEnd < 0` | true | 0 / 43537 | DEF | the read's earlier checks keep the cache position at or before the cache's written end |
| `!cacheCluster` after `getCluster` (first of 2) | true | 0 / 43537 | DEF | a stolen cache cluster, caught earlier |
| `numSamplesThisCacheRead <= 0` | true | 0 / 43537 | DEF | ALPHA check |
| `true` (first of 2) | false | 5207162 / 0 | CON | `while (true)` |
| `uncachedClusterIndex < sample->getFirstClusterIndexWithAudioData() - 1` | true | 0 / 43537 | RES | resuming writing past the cache with the uncached cluster outside the audio data; slow storage (streaming-margin, not a golden) reached the `!hasCurrentRegion()` check after it |
| `!hasCurrentRegion()` | true | 0 / 43503 | RES | resuming writing past the cache before the reader holds the uncached cluster: only slow storage reaches it (streaming-margin, not a golden, did) |
| `deluge_sample_region_state(source_, static_cast<uint32_t>(uncachedClusterIndex)) != DELUGE_REGION_LOADING` | false | 267 / 0 | RES | — |
| `deluge_sample_region_acquire_ex(source_, static_cast<uint32_t>(uncachedClusterIndex), static_cast<int8_t>(playDirection), 0u, &region) == DELUGE_REGION_READY` | false | 267 / 0 | RES | — |
| `hasCurrentRegion()` | false | 43503 / 0 | RES | as the `!hasCurrentRegion()` check above: only slow storage reaches it |
| `kCacheByteDepth == 3` | false | 110977 / 0 | CON | kCacheByteDepth is a constant 3 |
| `!setupSuccess` after `setupNewCluster` | true | 0 / 2558 | RES | — |
| `!stopSuccess` | both | 0 / 0 | RES | — |
| `!cacheCluster` after `getCluster` (second of 2) | true | 0 / 110977 | DEF | ALPHA check |
| `numSamplesThisUncachedRead <= 0` | true | 0 / 110977 | DEF | ALPHA check |
| `count >= 1024` | true | 0 / 260 | DEF | E169: 1,024 loops in one block |
| `combinedIncrementsLeft > numSamplesThisUncachedRead` | true | 0 / 30 | DEF | E151 |
| `cache->writeBytePos < cacheBytePos` (fourth of 4) | true | 0 / 104434 | MEM | — |
| `!success` after `stopUsingCache` (fourth of 5) | both | 0 / 0 | MEM | — |
| `!success` after `hopEnd` | true | 0 / 8981 | CON | hopEnd always returns true |
| `timeStretcher == nullptr` | true | 0 / 325 | CON | hopEnd never ends stretching |
| `!timeStretcher->playHeadStillActive[PLAY_HEAD_OLDER]` | true | 0 / 325 | CON | a hop runs only with the newer head active, which becomes the older head |
| `!timeStretcher->playHeadStillActive[PLAY_HEAD_NEWER]` | both | 0 / 0 | CON | as above |
| `swappingOrder` (first of 3) | true | 0 / 209208 | CON | swappingOrder is false: TIME_STRETCH_ENABLE_BUFFER is 0 |
| `!success` after `readSamplesForTimeStretching` (first of 2) | true | 0 / 207355 | DEF | readSamplesForTimeStretching fails only at its card-error exit, which is never taken |
| `swappingOrder` (second of 3) | true | 0 / 209207 | CON | as above |
| `!success` after `readSamplesForTimeStretching` (second of 2) | true | 0 / 105635 | DEF | as above |
| `swappingOrder` (third of 3) | true | 0 / 106522 | CON | as above |
| `cache->writeBytePos != cacheBytePos` | true | 0 / 6181 | MEM | — |
| `!success` after `stopUsingCache` (fifth of 5) | both | 0 / 0 | MEM | — |
| `true` (second of 2) | false | 741084 / 0 | CON | `while (true)` |
| `cache` | false | 6181 / 0 | MEM | cache abandoned mid-write |

**VoiceSample::sampleZoneChanged**

| Condition | Side not taken | True / false | Category | Why |
|---|---|---|---|---|
| `cache->reversed != reversed` | true | 0 / 6 | EDIT | a cached voice reversed under it: a synth's `reversed` change kills every voice first (setting_apply.cpp), so only an audio clip reversed while its voice reads a cache reaches it |
| `!success` after `stopUsingCache` (first of 2) | both | 0 / 0 | EDIT | as above |
| `!success` after `stopUsingCache` (second of 2) | true | 0 / 1 | RES | stopUsingCache failing on a start-marker move |
| `((VoiceSamplePlaybackGuide*)voiceSource)->shouldObeyLoopEndPointNow()` | false | 1 / 0 | NS | an unstretched voice's loop end moved ahead of it, or after its release |
| `(int32_t)(getPlayByteLowLevel(sample, voiceSource) - ((VoiceSamplePlaybackGuide*)voiceSource)->loopEndPlaybackAtByte) * voiceSource->playDirection >= 0` | false | 1 / 0 | NS | as above |
| `markerType == MarkerType::END` | false | 13 / 0 | CON | the four marker types are exhausted |
| `voiceSource->endPlaybackAtByte` | false | 8 / 0 | CON | endPlaybackAtByte is always set, as the comment suspects |
| `(int32_t)(getPlayByteLowLevel(sample, voiceSource) - voiceSource->endPlaybackAtByte) * voiceSource->playDirection >= 0` | false | 2 / 0 | NS | an unstretched voice whose end moves ahead of it |
| `false` | true | 0 / 17 | CON | `if (false)`, a label holder |

### `src/deluge/processing/engines/audio_engine.cpp`

**AudioEngine::cullVoices**

| Condition | Side not taken | True / false | Category | Why |
|---|---|---|---|---|
| `bypassed` | true | 0 / 16 | CULL | the render fits its period in every gate, so the cull returns before this check; a hop's bypass is also cleared at the block's end, before the next block's cull reads it (the design note's findings) |
| `currentSong` | true | 0 / 16 | CULL | as above: no cull runs |
| `i < plan.kill` | true | 0 / 16 | CULL | as above |
| `i < plan.terminate` | true | 0 / 16 | CULL | as above |
| `i < plan.force_release` | true | 0 / 16 | CULL | as above |
| `plan.kill + plan.terminate > 0` | true | 0 / 16 | CULL | as above |
| `culled != 0` | true | 0 / 16 | CULL | as above |
| `FlashStorage::highCPUUsageIndicator` | both | 0 / 0 | CULL | as above |

**AudioEngine::setDireness**

| Condition | Side not taken | True / false | Category | Why |
|---|---|---|---|---|
| `numSamples >= direnessThreshold` | true | 0 / 533158 | CULL | direness never rises: the gates fail a render whose direness rises |
| `newDireness >= cpuDireness` | both | 0 / 0 | CULL | as above |
| `cpuDireness < 0` | false | 11735 / 0 | CULL | as above: direness stays 0, so every decrement goes below 0 |

**AudioEngine::renderAudioForStemExport**

| Condition | Side not taken | True / false | Category | Why |
|---|---|---|---|---|
| `sideChainHitPending` | both | 0 / 0 | EXPORT | the offline stem export's render: no recorded gate exports stems |
| `currentSong != nullptr` | both | 0 / 0 | EXPORT | as above |
| `stemExport.includeSongFX` | both | 0 / 0 | EXPORT | as above |
| `recorder` | both | 0 / 0 | EXPORT | as above |
| `recorder->mode == AudioInputChannel::OFFLINE_OUTPUT` | both | 0 / 0 | EXPORT | as above |
| `recorder->mode == AudioInputChannel::MIX` | both | 0 / 0 | EXPORT | as above |
| `recorder->status < RecorderStatus::FINISHED_CAPTURING_BUT_STILL_WRITING` | both | 0 / 0 | EXPORT | as above |
