# The time stretcher's coverage

How far the goldens reach the legacy time stretcher and its users' stretcher functions, and why each branch side they
do not reach is not reached. The rewrite in [time_stretcher_design.md](time_stretcher_design.md) is measured against
this list (its step 8).

**What was run.** `scripts/tasks/stretcher_coverage.sh` builds `golden_vt_render` with clang source coverage and runs,
through the gates' own checks, golden-synth (479 cases) and the 63 render-song gates whose output is compared with
`tests/golden/render/songs.json`. Only recorded renders count: a song gate's dry-twin and unswitched renders, and gates
that keep no audio record (streaming-margin, arrangement-take), are left out. The functions measured are the script's
`SCOPE` (`scripts/tasks/stretcher_coverage.py`): all of the stretcher's own files, and in its users the functions that
call into it, branch on whether a voice has one, or keep the hop budget and the culling bypass a hop sets. The sample
reader's own functions are outside it (`readSamplesNative` and `readSamplesResampled` only pass the stretcher along);
the rewrite replaces the reader with a read head too (the design note's decision 8), so its step 1 adds them to this
list before the read head lands, and its step 8 measures the read head in their place.

**Result.** 815 of the 1,040 branch sides are taken; the 225 untaken are listed below by file and function. A side is
named by its condition, the branch's source text; where one function tests the same text more than once, the call whose
result it tests is named (`after`), and repeats are counted in source order.

## Categories

| Category | Sides | Meaning |
|---|---|---|
| **Ruled out by the code** | 67 | |
| DEF | 45 | defensive: the code before it already rules the condition out (named per side) |
| CON | 22 | by construction: the condition is fixed by the code itself (named per side) |
| **Needs a fault or load** | 88 | |
| RES | 37 | residency: the sample data asked for is not resident (a card read still pending, a chunk evicted, or a card error). The goldens' card answers at once and their gates fail any underrun |
| MEM | 31 | memory: an allocation fails, or memory pressure steals a cache or perc-cache chunk; no golden runs out of memory |
| CULL | 20 | voice culling under CPU load; the gates fail a render whose direness rises, so the cull never runs |
| **Reachable on the device, no gate scripts it** | 64 | |
| EDIT | 18 | an edit while a voice plays: an audio clip's marker moved (`sampleZoneChanged`), a clip-length edit with a live voice (`resumePlayback`), a clip reversed while it reads a cache, a long unreferenced sample auditioned |
| LATE | 12 | a late start: playback started part-way through a clip (`doingLateStart`) |
| STOP | 5 | a clip rendering after both clocks stop, its release tail; render-song fails a render whose playback stops |
| REC | 13 | an audio clip recording, or an aborted recorder |
| EXPORT | 14 | the offline stem export's render |
| OPEN | 2 | reachable from a preset or song, not built: a third perc-cache chunk (a source over 2^23 frames) and a crossfade from a cache whose read position is in a cache chunk's last frame |
| **Not a stretcher path** | 6 | |
| NS | 6 | a voice with no stretcher, or an unstretched cache; other goldens own it |

EDIT, LATE and STOP sit in the functions the rewrite changes (`AudioClip::processCurrentPos`,
`AudioClip::resumePlayback` and `AudioClip::render`, which reach the stretcher's queries and `end`); render-song cases
are built for them before the rewrite changes those functions, all but the reversed-while-cached and auditioned EDIT
sides, which with REC and EXPORT are accepted by name as unpinned (the design note's decision 10). How the RES, MEM,
CULL and OPEN sides' legacy values are recorded is its decision 11. A side's reason is left as "—" where its category's
meaning gives it.

## Functions

| File (under `src/deluge/`) | Function | Calls | Lines run | Branch sides taken |
|---|---|---|---|---|
| `model/clip/audio_clip.cpp` | AudioClip::processCurrentPos | 10072 | 62/76 | 31/50 |
| `model/clip/audio_clip.cpp` | AudioClip::resumePlayback | 3 | 28/53 | 10/28 |
| `model/clip/audio_clip.cpp` | AudioClip::render | 213654 | 127/143 | 72/88 |
| `model/clip/audio_clip.cpp` | AudioClip::getLoopingType | 213654 | 6/6 | 8/8 |
| `model/clip/audio_clip.cpp` | AudioClip::getCullImmunity | 0 | 0/5 | 0/4 |
| `model/sample/sample.cpp` | Sample::fillPercCache | 20933 | 262/307 | 98/120 |
| `model/sample/sample.cpp` | Sample::getAveragesForCrossfade | 16232 | 68/89 | 29/36 |
| `model/sample/sample.cpp` | Sample::prepareToReadPercCache | 8487 | 41/46 | 20/22 |
| `model/sample/sample_low_level_reader.cpp` | SampleLowLevelReader::readSamplesForTimeStretching | 300146 | 23/25 | 7/8 |
| `model/voice/voice_sample.cpp` | VoiceSample::beenUnassigned | 1910 | 4/4 | 0/0 |
| `model/voice/voice_sample.cpp` | VoiceSample::noteOn | 1910 | 10/10 | 0/0 |
| `model/voice/voice_sample.cpp` | VoiceSample::endTimeStretching | 2678 | 8/8 | 2/2 |
| `model/voice/voice_sample.cpp` | VoiceSample::fudgeTimeStretchingToAvoidClick | 112 | 19/28 | 3/6 |
| `model/voice/voice_sample.cpp` | VoiceSample::weShouldBeTimeStretchingNow | 187764 | 31/35 | 18/20 |
| `model/voice/voice_sample.cpp` | VoiceSample::render | 541009 | 598/695 | 240/304 |
| `model/voice/voice_sample.cpp` | VoiceSample::sampleZoneChanged | 18 | 104/118 | 49/60 |
| `model/voice/voice_sample.cpp` | VoiceSample::getPlaySample | 22666 | 9/9 | 2/2 |
| `processing/engines/audio_engine.cpp` | AudioEngine::cullVoices | 524125 | 27/41 | 11/20 |
| `processing/engines/audio_engine.cpp` | AudioEngine::renderAudio | 527167 | 32/32 | 4/4 |
| `processing/engines/audio_engine.cpp` | AudioEngine::renderAudioForStemExport | 0 | 0/38 | 0/14 |
| `processing/engines/audio_engine.cpp` | AudioEngine::solicitTimeStretcher | 1324 | 5/7 | 0/0 |
| `processing/engines/audio_engine.cpp` | AudioEngine::timeStretcherUnassigned | 1324 | 3/3 | 0/0 |
| `processing/engines/audio_engine.cpp` | AudioEngine::setDireness | 524145 | 14/23 | 6/10 |
| `dsp_legacy/timestretch/time_stretcher.h` | TimeStretcher::TimeStretcher | 1324 | 1/1 | 0/0 |
| `dsp_legacy/timestretch/time_stretcher.cpp` | TimeStretcher::init | 1324 | 46/51 | 10/12 |
| `dsp_legacy/timestretch/time_stretcher.cpp` | TimeStretcher::reInit | 260 | 7/7 | 2/2 |
| `dsp_legacy/timestretch/time_stretcher.cpp` | TimeStretcher::beenUnassigned | 1324 | 10/10 | 2/2 |
| `dsp_legacy/timestretch/time_stretcher.cpp` | TimeStretcher::unassignAllReasonsForPercLookahead | 1345 | 6/6 | 2/2 |
| `dsp_legacy/timestretch/time_stretcher.cpp` | TimeStretcher::unassignAllReasonsForPercCacheClusters | 1324 | 8/8 | 4/4 |
| `dsp_legacy/timestretch/time_stretcher.cpp` | TimeStretcher::unassignAllReasonsForHopBack | 1324 | 6/6 | 2/2 |
| `dsp_legacy/timestretch/time_stretcher.cpp` | TimeStretcher::keepClusterForHopRetry | 0 | 0/11 | 0/2 |
| `dsp_legacy/timestretch/time_stretcher.cpp` | TimeStretcher::releaseClusterForHopRetry | 9843 | 3/6 | 1/2 |
| `dsp_legacy/timestretch/time_stretcher.cpp` | TimeStretcher::hopEnd | 8608 | 361/388 | 129/140 |
| `dsp_legacy/timestretch/time_stretcher.cpp` | TimeStretcher::setupNewPlayHead | 8631 | 13/20 | 3/6 |
| `dsp_legacy/timestretch/time_stretcher.cpp` | TimeStretcher::allocateBuffer | 580 | 4/4 | 0/0 |
| `dsp_legacy/timestretch/time_stretcher.cpp` | TimeStretcher::readFromBuffer | 887 | 24/24 | 10/10 |
| `dsp_legacy/timestretch/time_stretcher.cpp` | TimeStretcher::rememberPercCacheCluster | 2598 | 9/11 | 5/6 |
| `dsp_legacy/timestretch/time_stretcher.cpp` | TimeStretcher::updateClustersForPercLookahead | 19702 | 11/11 | 2/2 |
| `dsp_legacy/timestretch/time_stretcher.cpp` | TimeStretcher::updateClustersForHopBack | 9731 | 19/21 | 5/6 |
| `dsp_legacy/timestretch/time_stretcher.cpp` | TimeStretcher::setupCrossfadeFromCache | 580 | 40/59 | 13/20 |
| `dsp_legacy/timestretch/time_stretcher.cpp` | TimeStretcher::getSamplePos | 195418 | 8/8 | 2/2 |
| `dsp_legacy/timestretch/time_stretcher.cpp` | getHopParams | 18339 | 44/44 | 10/10 |
| `dsp_legacy/timestretch/time_stretcher.cpp` | TimeStretcher::hopEnd (lambda) | 15982 | 8/10 | 3/4 |
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
| `hopRetry_ != nullptr` | true | 0 / 9843 | RES | hop retry follows only a failed setupNewPlayHead |

**TimeStretcher::hopEnd**

| Condition | Side not taken | True / false | Category | Why |
|---|---|---|---|---|
| `!deluge::cluster::lease_count(percCacheClustersNearby[l]->resource_slot)` | true | 0 / 643 | DEF | i036: a remembered perc-cache cluster with no lease |
| `playbackHandler.isEitherClockActive()` | false | 7897 / 0 | STOP | — |
| `sourceSamplesTilLoop >= 0` | false | 1660 / 0 | STOP | stretch position past the loop end, which the comment ties to stopping playback |
| `samplesTilHopEnd < 1` | true | 0 / 8487 | DEF | the beam width is multiplied by the phase increment and divided by it again; below 1 needs a phase increment under about 5,600 (11.5 octaves down), where the ratio 2^48 / increment has long overflowed and Sample::getOrCreateCache divides by zero first (SIGFPE: TODO.md, and the design note's decision 3) |
| `newHeadBytePos < (int32_t)sample->audioDataStartPosBytes` | true | 0 / 7771 | DEF | E285: the new head is clamped to the waveform's start above |
| `false` | true | 0 / 7584 | CON | `if (false)`, a label holder |
| `window.frames == nullptr` | true | 0 / 81767 | RES | — |
| `!success` | true | 0 / 8519 | RES | setupNewPlayHead fails only when its cluster is not ready |
| `deluge_sample_peek(deluge::sample::source_id_for(*sample), wantedFrame, static_cast<int8_t>(playDirection)) .frames == nullptr` | both | 0 / 0 | RES | as above |
| `!olderHeadReadingFromBuffer` | false | 25 / 0 | CON | olderHeadReadingFromBuffer is set false at the top of hopEnd, as its comment says |

**TimeStretcher::hopEnd (lambda)**

| Condition | Side not taken | True / false | Category | Why |
|---|---|---|---|---|
| `deluge_sample_peek(deluge::sample::source_id_for(*sample), frame, static_cast<int8_t>(playDirection)) .frames == nullptr` | true | 0 / 358 | RES | the region before the start marker not yet loaded |

**TimeStretcher::setupNewPlayHead**

| Condition | Side not taken | True / false | Category | Why |
|---|---|---|---|---|
| `voiceSample->setupClustersForPlayFromByte(guide, sample, newHeadBytePos, priorityRating) != RegionOutcome::Ready` | true | 0 / 8631 | RES | — |
| `!success` after `changeClusterIfNecessary` | true | 0 / 8631 | RES | — |
| `!voiceSample->hasCurrentRegion()` | true | 0 / 8631 | RES | a Ready setup that holds no region |

**TimeStretcher::rememberPercCacheCluster**

| Condition | Side not taken | True / false | Category | Why |
|---|---|---|---|---|
| `percCacheClustersNearby[0]` | true | 0 / 6 | OPEN | a third distinct perc-cache cluster remembered: playback crossing two perc-cache cluster boundaries, so a source over 2^23 frames (16 MB mono); LONG.WAV is 2^22 + 65,536 |

**TimeStretcher::updateClustersForHopBack**

| Condition | Side not taken | True / false | Category | Why |
|---|---|---|---|---|
| `lastSample < 0` | true | 0 / 9731 | DEF | a sample with no frames; it cannot load |

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
| `modelStack->getTimelineCounter() != this` | true | 0 / 10072 | REC | the clip replaced while it processes its position: an arrangement take (arrangement-take, which compares no audio, reached it) |
| `recorder` | true | 0 / 10072 | REC | — |
| `recorder->status == RecorderStatus::ABORTED` | both | 0 / 0 | REC | — |
| `getCurrentlyRecordingLinearly()` (first of 2) | true | 0 / 8085 | REC | pos 0 only at a linear recording's restart |
| `lastProcessedPos == nextSampleRestartPos` | both | 0 / 0 | REC | — |
| `getCurrentlyRecordingLinearly()` (second of 2) | true | 0 / 1987 | REC | — |
| `originalLength != 0` | both | 0 / 0 | REC | — |
| `originalLength < playbackHandler.swungTicksTilNextEvent` | both | 0 / 0 | REC | — |
| `sampleHolder.audioFile` | false | 1987 / 0 | DEF | a clip with no file or an unplayable one; every generated song's samples load |
| `!((Sample*)sampleHolder.audioFile)->unplayable` | false | 1987 / 0 | DEF | as above |
| `((AudioOutput*)output)->envelope.stage() >= Envelope::Stage::RELEASE` | true | 0 / 1827 | LATE | the envelope releasing at the clip's start, after a restart part-way |
| `!doingLateStart` | both | 0 / 0 | LATE | — |
| `!voiceSample` after `AudioEngine::solicitVoiceSample` | true | 0 / 160 | MEM | the voice pool empty |
| `outcome != RegionOutcome::Ready` | true | 0 / 1439 | RES | — |

**AudioClip::resumePlayback**

| Condition | Side not taken | True / false | Category | Why |
|---|---|---|---|---|
| `!playbackHandler.isEitherClockActive()` | true | 0 / 3 | EDIT | resumePlayback runs at an arrangement instance's start or after a clip-length edit (Song::setClipLength); a resume with the clocks stopped or the clip inactive |
| `!modelStack->song->isClipActive(this)` | true | 0 / 3 | EDIT | as above |
| `!sampleHolder.audioFile` | true | 0 / 3 | DEF | an unplayable sample |
| `((Sample*)sampleHolder.audioFile)->unplayable` | true | 0 / 3 | DEF | as above |
| `voiceSample` (first of 3) | true | 0 / 3 | EDIT | a resume that finds the clip's voice alive needs a clip-length edit while it plays: at an instance's start the voice is gone (back-to-back instances were probed: still none) |
| `voiceSample->cache` | both | 0 / 0 | EDIT | as above |
| `!success` after `stopUsingCache` | both | 0 / 0 | EDIT | as above |
| `sequenceSyncStartedNumTicksAgo < 0` | true | 0 / 3 | DEF | 'Shouldn't happen' |
| `!mayMakeSound` | true | 0 / 3 | REC | a resume of a clip that may not sound: an arrangement take, as `processCurrentPos`'s replaced-clip check |
| `voiceSample` (second of 3) | true | 0 / 3 | EDIT | as the live-voice check above: a clip-length edit while the voice plays |
| `voiceSample->timeStretcher` | both | 0 / 0 | EDIT | as the live-voice check above: a clip-length edit while the voice plays |
| `((AudioOutput*)output)->envelope.stage() < Envelope::Stage::RELEASE` | both | 0 / 0 | EDIT | as the live-voice check above: a clip-length edit while the voice plays |
| `voiceSample` (third of 3) | true | 0 / 3 | EDIT | as the live-voice check above: a clip-length edit while the voice plays |
| `!voiceSample` after `AudioEngine::solicitVoiceSample` | true | 0 / 3 | MEM | the voice pool empty |

**AudioClip::render**

| Condition | Side not taken | True / false | Category | Why |
|---|---|---|---|---|
| `!voiceSample` | true | 0 / 213654 | DEF | AudioOutput renders the clip only while it has a voice |
| `doingLateStart` (first of 2) | true | 0 / 213654 | LATE | — |
| `static_cast<AudioOutput*>(this->output)->envelope.stage() < Envelope::Stage::FAST_RELEASE` | both | 0 / 0 | LATE | — |
| `result != LateStartAttemptStatus::SUCCESS` | both | 0 / 0 | LATE | — |
| `result == LateStartAttemptStatus::FAILURE` | both | 0 / 0 | LATE | — |
| `!playbackHandler.isEitherClockActive()` | true | 0 / 16409 | STOP | the clocks stopped; the `\|\|`'s other operand, `doingLateStart`, is LATE's |
| `doingLateStart` (second of 2) | true | 0 / 16409 | LATE | — |
| `playbackHandler.isEitherClockActive()` | false | 6770 / 0 | STOP | a tied clip without pitch change rendering with the clocks stopped |
| `!doingLateStart` | false | 6770 / 0 | LATE | — |
| `playbackHandler.playbackState` | false | 213654 / 0 | STOP | playbackState false: a clip rendering after playback stopped (arrangement-take, which compares no audio, reached it) |
| `!success` after `fudgeTimeStretchingToAvoidClick` | true | 0 / 112 | MEM | fudge setup failing (no stretcher, or its play-head's cluster not ready: RES) |
| `cutPos < 2147483647` | false | 14 / 0 | NS | a clip that neither loops nor ever cuts; not a stretcher path |
| `!everythingOk` after `possiblySetUpCache` | true | 0 / 1439 | MEM | cache setup failing (or RES) |

**AudioClip::getCullImmunity**

| Condition | Side not taken | True / false | Category | Why |
|---|---|---|---|---|
| `voiceSample` | both | 0 / 0 | CULL | — |
| `voiceSample->timeStretcher` | both | 0 / 0 | CULL | — |

### `src/deluge/model/sample/sample.cpp`

**Sample::fillPercCache**

| Condition | Side not taken | True / false | Category | Why |
|---|---|---|---|---|
| `startPosSamples >= lengthInSamples` | true | 0 / 20491 | DEF | the caller does not call again once filled to the waveform's end (filledToEndOfWaveform) |
| `startPosSamples < 0` | true | 0 / 442 | DEF | as above, reversed |
| `!percCacheClusters[reversed]` | true | 0 / 2 | MEM | — |
| `(mgr != nullptr)` | false | 2 / 0 | DEF | no resource manager |
| `percCacheAssetId[reversed] == DELUGE_RESOURCE_NO_ASSET` | true | 0 / 2 | DEF | RPC1: asset table full |
| `isProjectReferenced()` | false | 2 / 0 | EDIT | a long sample not referenced by the project: auditioned from the browser |
| `!percCacheMemory[reversed]` | true | 0 / 98 | MEM | — |
| `percClusterIndexStart >= numPercCacheClusters` | true | 0 / 1280 | DEF | E138 |
| `!clusterHere` | true | 0 / 1280 | MEM | a perc-cache cluster stolen |
| `startPosSamples & ((1 << (Cluster::size_magnitude + kPercBufferReductionMagnitude)) - 1)` | both | 0 / 0 | MEM | as above |
| `clusterHere` | false | 1280 / 0 | MEM | as above |
| `percClusterIndexEnd >= numPercCacheClusters` | true | 0 / 6 | DEF | E140 |
| `!percCacheClusters[reversed][percClusterIndexEnd]` | true | 0 / 6 | MEM | E141, a stolen cluster |
| `numSamples <= 0` | true | 0 / 19702 | DEF | nothing to fill |
| `sourceClusterIndex >= getFirstClusterIndexWithNoAudioData()` | true | 0 / 21028 | DEF | a source cluster outside the audio data |
| `sourceClusterIndex < getFirstClusterIndexWithAudioData()` | true | 0 / 21028 | DEF | as above |
| `percClusterIndex >= numPercCacheClusters` | true | 0 / 1312 | DEF | perc-cache cluster index past the end |
| `p == nullptr` | true | 0 / 4 | MEM | — |
| `window.frames == nullptr` | true | 0 / 21028 | RES | — |
| `true` | false | 11454935 / 0 | CON | `while (true)` |
| `percCacheZone->endPos == percCacheZone->startPos` | true | 0 / 19702 | DEF | an empty zone |

**Sample::getAveragesForCrossfade**

| Condition | Side not taken | True / false | Category | Why |
|---|---|---|---|---|
| `startBytePos < (int32_t)audioDataStartPosBytes` | true | 0 / 16232 | DEF | ALPHA check |
| `readByteOutOfRange()` (first of 2) | true | 0 / 46065 | DEF | hopEnd bounds the read |
| `readByteOutOfRange()` (second of 2) | true | 0 / 46234 | DEF | as above |
| `whichCluster < getFirstClusterIndexWithAudioData()` | true | 0 / 46234 | DEF | as above |
| `whichCluster >= getFirstClusterIndexWithNoAudioData()` | true | 0 / 46234 | DEF | as above |
| `window.frames == nullptr` | true | 0 / 46234 | RES | — |
| `numSamplesLeftThisAverage < 0` | true | 0 / 46234 | DEF | ALPHA check |

**Sample::prepareToReadPercCache**

| Condition | Side not taken | True / false | Category | Why |
|---|---|---|---|---|
| `i >= static_cast<int32_t>(percCacheZones[reversed].size())` | true | 0 / 8485 | DEF | no zone at the beam: hopEnd reads only after a fill |
| `!percCacheClusters[reversed][ourCluster]` | true | 0 / 435 | MEM | a stolen perc-cache cluster |

### `src/deluge/model/sample/sample_low_level_reader.cpp`

**SampleLowLevelReader::readSamplesForTimeStretching**

| Condition | Side not taken | True / false | Category | Why |
|---|---|---|---|---|
| `hasCurrentRegion()` | true | 0 / 884 | DEF | every failed window check leaves the reader holding no region, so this card-error exit is never taken (the design note's findings) |

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
| `!timeStretcher` after `AudioEngine::solicitTimeStretcher` | true | 0 / 1212 | MEM | — |
| `!success` after `reassessReassessmentLocation` | true | 0 / 1212 | RES | — |

**VoiceSample::render**

| Condition | Side not taken | True / false | Category | Why |
|---|---|---|---|---|
| `phaseIncrement != kMaxSampleValue` (first of 2) | false | 152517 / 0 | CON | a voice sets up a cache only off its native rate |
| `interpolationBufferSize <= 2` | true | 0 / 152516 | CULL | an interpolation buffer of 2 or less while writing a cache: the interpolation-mode test before it short-circuits for linear, and the voice's buffer shrinks only under CPU direness |
| `writingToCache` (first of 2) | both | 0 / 0 | CULL | as above |
| `cache->timeStretchRatio != kMaxSampleValue` | false | 4 / 0 | NS | leaving an unstretched cache |
| `!success` after `stopUsingCache` (first of 5) | true | 0 / 5 | RES | — |
| `!success` after `weShouldBeTimeStretchingNow` (first of 2) | true | 0 / 4 | RES | — |
| `cache->writeBytePos < cacheBytePos` (first of 4) | true | 0 / 152515 | MEM | a cache cluster stolen |
| `!success` after `stopUsingCache` (second of 5) | both | 0 / 0 | MEM | — |
| `cache->writeBytePos > cacheBytePos` | true | 0 / 108735 | DEF | 'Could this really happen?': a second writer of the same cache |
| `!success` after `stopReadingFromCache` | true | 0 / 576 | RES | — |
| `!success` after `weShouldBeTimeStretchingNow` (second of 2) | true | 0 / 576 | RES | — |
| `cache->writeBytePos < cacheBytePos` (second of 4) | true | 0 / 576 | MEM | the cache stolen during the switch |
| `phaseIncrement != kMaxSampleValue` (second of 2) | false | 576 / 0 | CON | as the same test where the cache's parameters are checked: a voice sets up a cache only off its native rate |
| `interpolationBufferSize != kInterpolationMaxNumSamples` (first of 2) | true | 0 / 576 | CON | a change to linear interpolation is caught first, where the cache's parameters are checked at the render's start |
| `!stillGoing` after `weShouldBeTimeStretchingNow` | true | 0 / 187184 | RES | or MEM |
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
| `true` (first of 2) | false | 5207416 / 0 | CON | `while (true)` |
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
| `!success` after `hopEnd` | true | 0 / 8572 | CON | hopEnd always returns true |
| `timeStretcher == nullptr` | true | 0 / 315 | CON | hopEnd never ends stretching |
| `!timeStretcher->playHeadStillActive[PLAY_HEAD_OLDER]` | true | 0 / 315 | CON | a hop runs only with the newer head active, which becomes the older head |
| `!timeStretcher->playHeadStillActive[PLAY_HEAD_NEWER]` | both | 0 / 0 | CON | as above |
| `swappingOrder` (first of 3) | true | 0 / 199121 | CON | swappingOrder is false: TIME_STRETCH_ENABLE_BUFFER is 0 |
| `!success` after `readSamplesForTimeStretching` (first of 2) | true | 0 / 197269 | DEF | readSamplesForTimeStretching fails only at its card-error exit, which is never taken |
| `swappingOrder` (second of 3) | true | 0 / 199120 | CON | as above |
| `!success` after `readSamplesForTimeStretching` (second of 2) | true | 0 / 102875 | DEF | as above |
| `swappingOrder` (third of 3) | true | 0 / 103762 | CON | as above |
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
| `markerType == MarkerType::END` | false | 9 / 0 | CON | the four marker types are exhausted |
| `voiceSource->endPlaybackAtByte` | false | 4 / 0 | CON | endPlaybackAtByte is always set, as the comment suspects |
| `forAudioClip` | true | 0 / 4 | EDIT | an audio clip's marker moved while its voice plays |
| `(int32_t)(getPlayByteLowLevel(sample, voiceSource) - voiceSource->endPlaybackAtByte) * voiceSource->playDirection >= 0` | false | 2 / 0 | NS | an unstretched voice whose end moves ahead of it |
| `false` | true | 0 / 13 | CON | `if (false)`, a label holder |

### `src/deluge/processing/engines/audio_engine.cpp`

**AudioEngine::cullVoices**

| Condition | Side not taken | True / false | Category | Why |
|---|---|---|---|---|
| `bypassed` | true | 0 / (see below) | CULL | the render fits its period in every gate, so the cull returns before this check; a hop's bypass is also cleared at the block's end, before the next block's cull reads it (the design note's findings) |
| `currentSong` | true | 0 / (see below) | CULL | as above: no cull runs |
| `i < plan.kill` | true | 0 / (see below) | CULL | as above |
| `i < plan.terminate` | true | 0 / (see below) | CULL | as above |
| `i < plan.force_release` | true | 0 / (see below) | CULL | as above |
| `plan.kill + plan.terminate > 0` | true | 0 / (see below) | CULL | as above |
| `culled != 0` | true | 0 / (see below) | CULL | as above |
| `FlashStorage::highCPUUsageIndicator` | both | 0 / 0 | CULL | as above |

**AudioEngine::setDireness**

| Condition | Side not taken | True / false | Category | Why |
|---|---|---|---|---|
| `numSamples >= direnessThreshold` | true | 0 / 524145 | CULL | direness never rises: the gates fail a render whose direness rises |
| `newDireness >= cpuDireness` | both | 0 / 0 | CULL | as above |
| `cpuDireness < 0` | false | 11531 / 0 | CULL | as above: direness stays 0, so every decrement goes below 0 |

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
