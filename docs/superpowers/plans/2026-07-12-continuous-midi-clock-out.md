# Continuous (free-running) MIDI clock output — implementation plan

## Context

The Deluge only emits MIDI clock (0xF8) while its transport is running. `PlaybackHandler::endPlayback()` sets `playbackState = 0`, and the clock-out driver in `AudioEngine::tickSongFinalizeWindows()` (`src/deluge/processing/engines/audio_engine.cpp:707`) is wrapped in `if (playbackHandler.isEitherClockActive())` — so the clock stream dies the moment you hit stop.

Most synths and grooveboxes free-run their clock: 0xF8 keeps flowing at the current tempo whether or not the transport is rolling, and 0xFA/0xFC gate the transport separately. Followers therefore stay tempo-locked across a stop, so tempo-synced delays, LFOs and arps on downstream gear don't stall or have to re-lock on every play. This adds that behavior behind a new user-selectable mode.

Scope is MIDI clock out only. The analog/trigger clock out (`cvEngine.isTriggerClockOutputEnabled()`) keeps its current playing-only behavior and must not be touched.

## Global Constraints

These bind every task. A violation of any of them is a defect regardless of what a task's own text says.

- **Default behavior is unchanged.** With the setting at its default (`PLAYING`), the emitted MIDI byte stream must be bit-for-bit identical to today's. This is the most important constraint in the plan.
- **Do not add a second emit path.** All 0xF8 emission continues to flow through `PlaybackHandler::doMIDIClockOutTick()` (`src/deluge/playback/playback_handler.cpp:950`) and out through the existing sample-accurate `scheduleMidiGateOutISR()` mechanism in `audio_engine.cpp:950`. The free-running clock supplies a *schedule*, not a new emitter.
- **Exactly one 0xF8 at the play seam.** When transport starts from a free-running clock, the tick that lands on the alignment boundary is out-tick 0. No duplicated and no dropped tick.
- **No drift.** Free-running tick times accumulate in 32.32 fixed point by adding the period to the previous tick time. Never re-derive the next tick time from `AudioEngine::audioSampleTimer` — rounding error would compound every tick.
- **Never dereference a null `currentSong`.** The free-running clock runs while stopped, including across song loads and swaps.
- **Trigger/analog clock out is out of scope.** Its behavior must remain playing-only.
- The audio-thread code path (`tickSongFinalizeWindows`) is hard-realtime. No allocation, no blocking, no locks beyond the existing `CriticalSectionGuard` usage.
- Follow surrounding code style. This codebase has no unit-test harness covering the playback clock; **verification for every task is `./dbt build Debug` completing clean.** Do not invent test scaffolding for these paths. State the build result in your report.

## Design decisions (already settled with the user — do not relitigate)

- Setting is a 3-way mode: `OFF` / `PLAYING` (current behavior, default) / `ALWAYS`.
- 0xFC is still sent on stop in `ALWAYS` mode. Followers stop their transport but stay tempo-locked off the continuing 0xF8 stream.
- Transport start is aligned to the *next* free-running clock tick, so tick spacing stays even across the seam. The resulting play-button latency (up to one out-tick) is accepted.

## Existing code to build on

- Emit + schedule: `PlaybackHandler::doMIDIClockOutTick()` (`playback_handler.cpp:950`), `scheduleMIDIClockOutTick()` (`playback_handler.cpp:872`). The normal scheduler derives the next tick from the timer-tick grid (`lastTimerTickActioned`, `timeLastTimerTickBig`, `nextTimerTickScheduled`) — state explicitly documented as invalid while stopped (`playback_handler.h:113-118`). That is why the free-running clock needs its own schedule.
- Tick period: `getMIDIClockOutTicksToInternalTicksRatio()` (`playback_handler.cpp:634`) plus `currentSong->timePerTimerTickBig` (32.32 fixed point).
- Byte senders: `MidiEngine::sendClock/sendStart/sendStop/sendPositionPointer` (`src/deluge/io/midi/midi_engine.cpp:449-471`). Per-cable gating already exists via `MIDICable::sendClock`; no work needed there.
- Position/continue: `sendOutPositionViaMIDI()` (`playback_handler.cpp:2320`).

---

## Task 1: Replace the MIDI-clock-out bool with a 3-way mode

Purely the setting infrastructure. **No behavioral change** beyond the new `ALWAYS` value existing and being selectable — `ALWAYS` behaves exactly like `PLAYING` after this task. The free-running behavior arrives in Task 2.

### Requirements

1. Add `enum class MIDIClockOutMode : uint8_t { OFF = 0, PLAYING = 1, ALWAYS = 2 };` in `src/deluge/playback/playback_handler.h`, near the `PlaybackHandler` class.

2. Replace the member `bool midiOutClockEnabled;` (`playback_handler.h:160`) with `MIDIClockOutMode midiOutClockMode;`.

3. Keep `bool currentlySendingMIDIOutputClocks()` (`playback_handler.cpp:461`) as the accessor; it returns `midiOutClockMode != MIDIClockOutMode::OFF`. Add `bool isContinuousClockOutEnabled()` returning `midiOutClockMode == MIDIClockOutMode::ALWAYS`.

4. Update the call sites that read the old member directly:
   - `src/deluge/playback/mode/session.cpp:1486` reads `playbackHandler.midiOutClockEnabled` — switch it to `playbackHandler.currentlySendingMIDIOutputClocks()`.
   - `src/deluge/gui/ui/menus.cpp:1148` passes the member to the menu item.
   - The four call sites already using `currentlySendingMIDIOutputClocks()` (`song.cpp:2592`, `song.cpp:5701`, `audio_engine.cpp:778`, and inside `playback_handler.cpp`) need no logic change.

5. Flash storage (`src/deluge/storage/flash_storage.cpp`): byte 34 now holds the enum's integer value.
   - Line 277 (defaults): default is `MIDIClockOutMode::PLAYING`.
   - Line 418 (read): convert `buffer[34]` to the enum. Existing saves hold 0 or 1, which map exactly onto `OFF`/`PLAYING`, so no migration is needed — but **clamp/validate** an out-of-range byte to `PLAYING` rather than casting blindly.
   - Line 831 (write): store the enum's integer value.

6. Menu (`src/deluge/gui/menu_item/midi/send_clock.h`, instantiated at `src/deluge/gui/ui/menus.cpp:1148`): change from `ToggleBool` to a `Selection` over the three modes. Follow how other `Selection`-based menu items in this codebase are written — find one and match it. Add the needed l10n strings (both the full OLED name and the 7SEG short name) in the language files, matching how existing MIDI-menu strings are declared.

7. `PlaybackHandler::setMidiOutClockMode()` (`playback_handler.cpp:2368`) takes `MIDIClockOutMode` instead of `bool`. Preserve its existing live-toggle semantics exactly, expressed in terms of "was sending clocks" vs "is now sending clocks" (i.e. compare `OFF` vs not-`OFF`):
   - transitioning from not-sending to sending while internal clock is active → `resyncMIDIClockOutTicksToInternalTicks()` then `sendOutPositionViaMIDI(getCurrentInternalTickCount(), true)`, as today;
   - transitioning from sending to not-sending while internal clock is active → clear `midiClockOutTickScheduled` and `midiEngine.sendStop(this)`, as today.
   - A `PLAYING` ↔ `ALWAYS` transition is not a change in "sending clocks" and must not emit any of the above during playback. Handling of that transition *while stopped* is Task 2's job — leave a clean seam, don't stub it.

### Verification

`./dbt build Debug` completes clean. Confirm by inspection that with the setting at `PLAYING` every code path behaves exactly as before this change.

---

## Task 2: Free-running clock while the transport is stopped

Makes `ALWAYS` mean something: 0xF8 keeps flowing at the song tempo while stopped. Transport start/stop behavior is otherwise untouched — the aligned handover on play is Task 3. After this task, pressing play from a free-running clock may produce one uneven tick interval at the seam; that is expected and Task 3 fixes it.

### Requirements

1. New state in `PlaybackHandler` (`playback_handler.h`), alongside the existing MIDI-clock-out block at lines 95-101:
   ```cpp
   bool     freeRunningClockActive;     // clock ticking while transport stopped
   uint64_t timeNextFreeRunningTickBig; // 32.32 sample time of next free-running tick
   ```
   Initialize both in the constructor alongside the existing clock-out members.

2. `void startFreeRunningClock()` — anchors the free-running schedule and sets `freeRunningClockActive`. It must be a no-op unless `midiOutClockMode == MIDIClockOutMode::ALWAYS`, `currentSong != nullptr`, and `!isEitherClockActive()`.
   - When called from `endPlayback()`, anchor the phase to the previous out-tick so the seam across the stop is even: next tick = `timeLastMIDIClockOutTickSent + period`.
   - When called with no meaningful previous tick (enabling `ALWAYS` from the menu while already stopped, or after a song load), anchor to `audioSampleTimer + period`.

3. `void stopFreeRunningClock()` — clears `freeRunningClockActive` and `midiClockOutTickScheduled`.

4. `void scheduleFreeRunningMIDIClockOutTick()` — the stopped-mode analogue of `scheduleMIDIClockOutTick()`. Compute the tick period in 32.32 from `getMIDIClockOutTicksToInternalTicksRatio()` and `currentSong->timePerTimerTickBig`, advance `timeNextFreeRunningTickBig` by exactly one period (**accumulate — see the no-drift global constraint**), and set `midiClockOutTickScheduled` / `timeNextMIDIClockOutTick` from it. Recompute the period on every call so tempo changes while stopped are picked up for free. The emit itself stays `doMIDIClockOutTick()`, unchanged.

5. Drive it from the audio engine (`src/deluge/processing/engines/audio_engine.cpp`, `tickSongFinalizeWindows()`):
   - Lift the existing MIDI-clock-out block (lines 777-793) into a small helper so the playing branch and the new stopped branch share one emitter. The playing branch's behavior must not change.
   - Add a stopped branch — `else if (playbackHandler.freeRunningClockActive)` on the existing `if (playbackHandler.isEitherClockActive())` at line 711 — that runs the same helper but schedules via `scheduleFreeRunningMIDIClockOutTick()`.
   - The stopped branch must **not** shorten the audio window (`numSamples`); window shortening exists only for timer/swung ticks. Setting `timeWithinWindowAtWhichMIDIOrGateOccurs` is what gets the byte out at the right sample offset, and it is sufficient.
   - Keep everything inside the existing `if (!stemExport.renderingOffline())` guard so offline stem rendering emits no clock.

6. Start/stop the free-running clock at the right moments:
   - `endPlayback()` (`playback_handler.cpp:578`): keep the existing 0xFC send exactly as-is, then call `startFreeRunningClock()` **after** `playbackState = 0` (the guard in `startFreeRunningClock` depends on playback state already being cleared).
   - `setupPlayback()` / the start-of-playback path: `stopFreeRunningClock()` so the normal scheduler takes over. (Task 3 refines the *timing* of this handover; here, just make it correct.)
   - `setMidiOutClockMode()`: switching to `ALWAYS` while stopped starts the free-running clock; switching away from `ALWAYS` while stopped stops it. Do **not** send an extra 0xFC when stopping it — one was already sent at `endPlayback()`.
   - Song load / song swap: re-anchor via `startFreeRunningClock()` so a replaced `currentSong` (and its new tempo) is picked up and never dereferenced stale. Find the existing song-load/swap completion point and hook there.
   - If an external clock becomes active, the free-running clock must stop and the existing follow-clock path takes over. The `!isEitherClockActive()` guard covers the start side; make sure there is no path where `freeRunningClockActive` stays set once a clock is active.

### Verification

`./dbt build Debug` completes clean. In your report, state explicitly: which call sites start and stop the free-running clock, and why `freeRunningClockActive` cannot remain set while `isEitherClockActive()` is true.

---

## Task 3: Aligned handover from free-running clock to transport start

Currently (after Task 2) pressing play while the clock free-runs resets the clock-out phase to the moment of the button press, producing one short or long tick interval at the seam. This task anchors timer tick 0 to the next free-running clock boundary so tick spacing stays perfectly even, and sends 0xFA immediately ahead of the tick that lands on that boundary.

### Requirements

1. New state in `PlaybackHandler`: `bool startMessagePending;` — set when a Start/Continue must be emitted at the aligned out-tick rather than at button-press time. Initialize in the constructor; clear it in `setupPlayback()` alongside the other clock-out resets (`playback_handler.cpp:519-527`).

2. In `setupPlaybackUsingInternalClock()` (`playback_handler.cpp:380-459`), when `freeRunningClockActive` is set and this is a normal internal-clock start (i.e. not a tempoless record):
   - **Do not** send 0xFA/0xFB at the existing send site (lines 430-440). Instead set `startMessagePending = true`.
   - Replace the unconditional `timeNextTimerTickBig = (uint64_t)AudioEngine::audioSampleTimer << 32;` (line 455) with the free-running boundary: `timeNextTimerTickBig = timeNextFreeRunningTickBig;`. Everything downstream (swung ticks, notes, out-tick 0) derives from this grid, so nothing sounds before the boundary and out-tick 0 lands exactly on it.
   - Leave `freeRunningClockActive` **set** through `setupPlayback()`, so the free-running scheduler stays in charge for the gap between the button press and the boundary. This is essential: the normal scheduler reads `lastTimerTickActioned` / `timeLastTimerTickBig`, which are stale during that gap. Clear `freeRunningClockActive` in `actionTimerTick()` when timer tick 0 is actioned — i.e. this supersedes the `stopFreeRunningClock()` call site Task 2 put in the start-of-playback path for the free-running case.
   - `setupPlayback()` already resets `lastMIDIClockOutTickDone = -1`. The free-running tick that fires at the boundary therefore takes it -1 → 0, becoming out-tick 0, and the normal scheduler picks up cleanly from out-tick 1. **This is the "exactly one 0xF8 at the seam" global constraint — make sure no second tick is emitted at the boundary by the normal scheduler.**

3. In `doMIDIClockOutTick()` (`playback_handler.cpp:950`): if `startMessagePending` is set, emit the Start (or, for a non-zero start position, SPP + Continue via the existing `sendOutPositionViaMIDI()`) **immediately before** `midiEngine.sendClock()`, then clear the flag. This puts 0xFA/0xFB directly ahead of the 0xF8 for position 0 in the output buffer, which is what followers expect. Preserve the existing `skipMidiClocks` behavior.

4. Count-in: `actionSwungTick()` already defers Start/Continue until the count-in ends (`playback_handler.cpp:983-1015`) while letting clocks flow. Do not change that. Ensure `startMessagePending` is **not** also set when there is a count-in, so exactly one Start is sent, at count-in end.

5. Tempoless record must be unaffected — it sends no clocks and no Start today.

### Verification

`./dbt build Debug` completes clean.

Trace and state in your report, for each case, exactly which MIDI bytes are emitted and in what order:
- play from stopped with mode `ALWAYS`, from position 0;
- play from stopped with mode `ALWAYS`, from a non-zero position;
- play from stopped with mode `ALWAYS` **and** a count-in enabled;
- play from stopped with mode `PLAYING` (must be identical to pre-change behavior).
