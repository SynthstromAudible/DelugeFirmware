# notify migration — on-device checklist

Run after any PR that moves a `display->`/`uiNeedsRendering`/RootUI-hook site onto
`deluge_notify`. Every item below only ran through compile + host sim + host tests during
development (`.superpowers/sdd/2026-08-26-ui-notifications/task-5-report.md`,
`task-7-report.md`); none of it has been confirmed on a real Deluge yet.

## Boot

- [ ] Boot: the logo appears, then the "Loading" working animation appears and clears.
- [ ] Force a missing or renamed startup song: the "Song missing" / "Using template" /
      "Startup fault F1..F4" messages still appear, in order, not lost or reordered (they now
      queue during boot and render at the first drain).
- [ ] Trigger a factory reset: the "FACTORY RESET" popup is still seen before flash is wiped
      (it now renders at the next drain rather than painting immediately beforehand).

## Playback / count-in

- [ ] Record with count-in: the popup shows **4, 3, 2, 1** (right-aligned, blinking at speed
      2), then clears when the count-in ends; recording starts.
- [ ] After the count-in ends, the UI is out of `UI_MODE_RECORD_COUNT_IN` — auditioning still
      works and buttons respond (exercises the masked `currentUIMode` clear).
- [ ] Hold an audition pad (or use the keyboard) through a record count-in, then play notes
      for the rest of the take: they land at their real positions, not swallowed by
      `earlyNotes`.
- [ ] Abort a count-in recording: the count-in digit popup clears once (the queued
      `notice_cancel_popup()` and the drain's own cancel on `count_in_visual() == 0` must not
      fight or double-fire).
- [ ] Nudge sync / adjust tempo / adjust swing: the "SYNC NUDGED" popup still appears, and the
      SWING/TEMPO `popupTextTemporary` popups still interact sanely with the now-deferred
      neighbouring popups (no flicker, no popup left stuck).

## Recording

- [ ] Finish a linear recording in clip view (both an InstrumentClip and an AudioClip): the
      clip redraws at its new length in session view, clip view, and arranger view.
- [ ] Abort an audio recording: the clip redraws in session, clip, and arranger views.
- [ ] Let a looping clip auto-extend past its end while recording: the clip redraws with the
      new length.
- [ ] Abort a long audio recording while scrolled right in AudioClipView: the view scrolls
      back left, instead of sitting past the end of the now-shortened clip.

## Loading

- [ ] Load a song: the "Loading" working animation appears and clears cleanly, with no stuck
      frame (verifies the two synchronous-load show/clear pairs that were deliberately kept as
      direct `display->` calls — `InstrumentClip`'s and `Song`'s `changeOutputType()` paths).
- [ ] Load a song with a missing sample: an error popup with the E-code still appears.
- [ ] Swap songs, and force a preset-save or preset-load error: the four working-animation
      calls that remain queued (`Song`'s instrument-preset replace clear,
      `storage_manager.cpp`'s XML/JSON create-file clears, `PlaybackHandler`'s song-swap show)
      cost at most one extra UI tick each — confirm no stuck or missing animation frame.

## Popups / errors / console

- [ ] Undo / redo: "Undo"/"Redo" console lines still appear (now one UI tick later than
      before).
- [ ] Alpha/beta build only: the E336 late-note-start popup, and the E073/"MORE" audio-path
      error popups, still appear if their conditions are reachable.
- [ ] Turn the mod encoders in the delay / filter / compressor / reverb sections, on both a
      Sound and a GlobalEffectable (kit, audio clip, song): the value popups still appear, and
      the held-button settings popups (a deliberately-deferred show paired with a migrated
      clear) still show on press and vanish cleanly on release — no flicker, no stuck popup.
- [ ] Trigger the transpose-interval and root-note popups: the full text shows, untruncated
      (`Notice::text` was widened from 24 to 48 bytes specifically for these).
- [ ] Select a threshold recording mode, then press Record: the threshold-recording popup
      still shows and still edits live turning the select encoder (this site is deliberately
      not migrated to `deluge_notify`).

## LEDs

- [ ] Play a heavy song until voices cull (High CPU indicator enabled in settings): the Play
      LED alert blinks once; sustained culling does not restart or stutter the blink.

## Clip views

- [ ] Stop playback: the playhead tick squares clear on the pads, in session, arranger, and
      clip views.
- [ ] MIDI transpose, or a kit note arriving over MIDI: the grid updates.
- [ ] Arm a clip or change a section's launch state: the OLED still refreshes
      (loops-remaining / section text).

## Arranger

- [ ] End arrangement playback while in **grid** session layout: the clip pads lose their
      stale colours (rather than being left showing the last-played state).
