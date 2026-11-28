# The differential driver

`differential.h` plays one input sequence into a legacy root view and into the scene that ports it, records a frame
after every step on each side, and reports the first step where the two differ. A view's port is done when its cases
and its seeded walks agree with legacy over every arm the scene claims.

## How a run works

Legacy and the scene act on the same song, so they cannot run in lockstep. `run(case, legacy, scene)` plays the whole
case into legacy, then calls the scene side's `enter()` (which puts the song back) and plays it again into the scene,
comparing each frame with the one legacy recorded at the same step.

A `Frame` has four fields, compared in this order:

1. `oled`: the main panel as a PBM, without popups.
2. `pads`: all 18 × 8 pads, sidebar included, as the device would light them.
3. `model`: a text digest of the model the view edits, written by the view's spec.
4. `popups`: the text of each popup the step raised or cancelled, one per line.

The report names the case, the step, the field, both values and the inputs up to that step. For any field other than
`model`, it also prints both model digests, because a model difference often explains a pad one.

## Writing a view's sides

A view's spec writes two `Side`s over its rig (`audio_clip_differential_spec.cpp` is the worked example).

**`enter()`** must put everything the case can change back to the case's start, on that side:

- the model (the rig's `reseed`), and anything a case can detach, such as a clip's sample or colour;
- the view's own latches: legacy's markers and `currentUIMode`, and the scene's `on_enter`;
- held buttons: legacy's `Buttons` state, and a fresh `InputLowering` on the scene side;
- time. Both legacy views and scenes are single objects that outlive a case, and nothing moves the clock between
  cases in-process. Each side advances its own clock by `kGapBetweenCasesMs` on entry (legacy:
  `AudioEngine::audioSampleTimer`; the scene: an epoch added to every time it passes on), so a pause armed in one case
  has expired by the next, as it would on the device.

**`feed(step, at_ms)`** delivers one step. The legacy side calls the view's hooks with legacy's held-button state set
first. The scene side lowers the step through a real `InputLowering`, calls `Scene::handle`, and keeps each `Popup`
action's text and whether a repaint was asked for.

Legacy animations end on later frames that nothing runs in-process. After each step, the legacy side ends any that are
running (`scrollFinished` for `UI_MODE_HORIZONTAL_SCROLL`, `exitUIMode` for `UI_MODE_HORIZONTAL_ZOOM`). Otherwise
legacy ignores the input that follows, and the run reports differences no device would show.

**`frame(at_ms)`** reads what the side shows now. The scene side drains the notice queue into `popups`, honours a
requested repaint (`Shell::boot`), ticks the shell, and reads the rig's OLED and fake grid. The legacy side renders
`renderMainPads`, `renderSidebar` and `renderOLED` directly, and takes its popups from a recording `Display`.

## Cases and walks

- **Scripted cases** (`Case{name, steps}`) pin one gesture sequence each: scroll and zoom, the markers, the length
  edits.
- **Seeded walks** (`random_walk(name, seed, alphabet, length)`) draw gestures from the view's ported alphabet with a
  fixed generator, so a seed walks the same steps on every platform. The alphabet holds only the arms the scene has
  ported. An arm joins the alphabet in the task that ports it, and the completeness ledger lists the arms still outside.
- **Masks** (`Case::mask`) leave pads, the OLED or the model out of the comparison, each for a D-row. A mask is for a
  difference Kate has accepted, never for one not yet understood.

## Proving a case can fail

A walk that agrees may be agreeing about nothing. When a view's cases first go green, break the scene on purpose in
one arm the cases cover, for example an off-by-one column, and confirm the run goes red at the step that reaches it.
Then restore the scene.

## Freezing legacy before the view goes

The legacy view is deleted when its scene becomes the root, so its side is frozen first. `frozen.h` writes one file
per case (`frozen_encode`: every field of the first frame, then each later frame's changed fields; the OLED as hex
rows) and `FrozenSide` replays it. The view's spec carries the freezer as an example that writes the files only when
`AUDIO_CLIP_ORACLE_OUT` (or the view's own variable) names a directory, and checks each file decodes back to exactly
what it recorded; the comparison against the frozen files then runs beside the live one until the flip removes the
live side. Answers a spec compares outside the driver (a number, a pad image, an OLED page) are frozen the same way
into a table and pages (`support/audio_clip_answers.h`): the legacy half of each example records its answers once,
and the committed spec reads only the frozen rows, checking at its end that it asked every row and read every page.
Before committing the files, change one frozen value and confirm the spec goes red.

## Running

```sh
GOLDEN_SCENARIO=scenes_spec SCENES_SPEC=audio_clip_differential_spec SCENES_SPEC_CARD=tests/fixtures/card \
    ./build/cargo-harness/harness/golden_vt_render
```
