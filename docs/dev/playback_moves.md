# Moving the playhead from the interaction tier

The render ticks the sequencer: `PlaybackHandler::actionTimerTick` and `actionSwungTick` run inside the audio render
(`tickSongFinalizeWindows`, `processing/engines/audio_engine.cpp`). So the playback position state is the render's: the
lane cursors (`AutoParam` played value and interpolation), each clip's position (`lastProcessedPos`, `repeatCount`,
`currentlyPlayingReversed`), each note row's independent position, the next-event schedule (`swungTicksTilNextEvent`,
the scheduled swung tick), and what decides what the tick walks (the playback mode, the playback state, each clip's
activeness and its output's active clip).

The interaction tier still moves the playhead: starting, restarting and continuing playback, a MIDI song position
pointer, switching between session and arrangement, a song swap, undo, an edit to a playing clip (length, shift,
direction, mute, instrument, drum, quantize), a launch, an unsolo, and recording into the arranger. Each such move
excludes the render, so a render block never sees it half made.

## A move's section

`deluge::playback::MoveSection` (`playback/move_section.h`) is one move:

- an `AudioExcludedSection`: the render is held off (on the device, interrupts are masked for the section's length);
- a `NoAllocationScope`: nothing in it allocates;
- a `MoveEffects::Scope`: what the move sets off that may allocate or log undo is recorded, and run once the section
  ends, in order. Only a notify of a param its owner's delay reacts to is deferred
  (`deluge::model::delayParamRole`): its reaction can grow the delay's reserve or log an Edit. MIDI sends (CC,
  expression, program change, a late note-on) and notices stay inside: the MIDI queue takes its own critical section
  and never waits, and a notice is a lock-free ring push;
- a `MaskedSpan`: the time it held the render off, per site (ALPHA builds and the host).

Its members end in reverse: the render runs again, then the effects. On the render, which calls the same move code,
a section holds nothing off and collects nothing. Inside one, `ModelStackWithAutoParam::doc()` reads the model's
lanes the render's way (through the collection, never creating or relocating a store): what a move positions the
cursors on is what the end of its Edit or its job publishes for the render ([params.md](params.md)).

What runs inside is only what the render's own tick does: cursors, positions, activeness, `expectEvent`, voices from
the pool, late note-ons. Allocation, the card, undo and waits run before or after. An arpeggiator reserves room for
eight held notes when its instrument is made, so a note-on does not allocate; an audio clip resumed with the
`VoiceSample` pool empty still falls back to the heap, as the render does.

## Each kind of move

- **Playback start** (`setupPlayback`, then the mode's `resetPlayPos`). The render's ticks are held
  (`PlaybackHandler::ticksHeld()`) from the start of setup until `releaseTicks()` publishes the clock: the clips are
  positioned and may begin recording before any tick reads them. `playbackState` keeps its place, so everything that
  reads it during setup is unchanged.
- **A reset** (`Session` / `Arrangement::resetPlayPos`): `moveToPos()` in one section, then `afterMoveToPos()` --
  linear recording, arrangement recording, launch timing. A start from 0 sets each clip's output's active clip after
  the clip's chance to record, which may give it a new output.
- **A switch or song swap**: the ticks are held across it (`holdTicks()` / `resumeTicks()`), each of its writes in a
  section, since the session ends (overdubs deleted, sound stopped) between them.
- **A resync, undo, an edit**: read where the render's tick has reached, move, resume, in one section per call.
  `PlaybackHandler::expectEvent()` takes its own.
- **A launch**: the gesture reads the launch position and stamps the swung-tick count with it in one section; the late
  start moves on by the ticks counted since, in one section. An unsolo restarts the other clips in one section.
- **Recording into the arranger**: a clone's allocations come first; the clone takes over, and later the original takes
  back over, each in one section.

## Checks

- `strayPositionWrites` (`playback/playhead_checks.h`): a position write made off the render while a clock runs and no
  section is held. `maskedActions`: an undo Action begun inside a move. Host `maskedAllocations()`
  (`memory/heaps.h`): an allocation inside a move.
- The `playhead-moves` gate (tests/integration/scenarios/playhead_moves_scenario.cpp) drives each kind of move and
  requires all three at 0, the launched and restarted clips in phase, and no tick inside a start. A host-only hook
  (`setRenderInjection`) runs a render block at the points where it could land between steps.
- render-song fails on `RS-POSITIONS`, the count over a song played as the device plays it.

## Toward commands

The direction is for the render to be the only writer of position state: the interaction tier posts a playhead command
and the render applies it at the start of a block, before it ticks. Each move is shaped for that: its position work is
one function taking plain values (`moveToPos(newPos, doingComplete)`, a late start's position and stamp), which a
command would carry instead of a call, and its deferred effects would come back through `playback/render_requests.h`
as the render's requests. What that still needs: a completion for work that waits on a move (linear recording, launch
timing, the arranger's playhead), a timing ruling per kind of move (a command lands up to a block later), and the
render's own start of linear recording at a count-in's end taken off the render first.
