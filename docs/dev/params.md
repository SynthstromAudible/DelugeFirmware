# Params: the descriptor model

How a plugin (the Deluge instrument, an effect) describes its params, and what a host may do with them. The legacy
q31 instrument and any later instrument publish through the same model; only the plugin knows its stored words.

## Identity

A param is a `plugin::ParamId` in its plugin's table: `instrument::kParams` (`src/instruments/deluge/params.h`) for a
sound, `global_effectable::kParams` (`src/effects/global_effectable/params.h`) for the song, a kit's affect-entire and
an audio track. Ids are pinned forever by the tables' specs. The runtime's own numbering (the `params::` enums, a
collection `Kind` and index) maps to ids through `model/project_bridge/param_ids.cpp`, and
`params::paramInfoFor(kind, index)` returns a runtime param's row.

## Identity in the host

The host names a param without its owner as a `project::ParamTarget{ParamSpace space; plugin::ParamId id}`
(`src/deluge-project/param_address.h`; `ParamAddress::target()` drops an address's owner). In ENGINE space the id is in
the owner's plugin table, a sound's cable depth being the instrument's depth id (`instruments/deluge/cables.h`); a
MIDI_CC id is the CC number, an EXPRESSION id the MPE dimension. Ids are per plugin (116 is portamento in the
instrument's table and mod FX rate in the global effectable's), so a target is read only together with its owner's
`bridge::OwnerPlugin`, which the code reading it knows from its owner (a sound is the instrument; a global effectable,
the song, a kit's affect-entire or an audio clip, is the global effectable).

`bridge::runtimeParamOf(owner, target)` and `bridge::targetOf(owner, kind, index)` (`model/project_bridge/param_ids.h`)
map a target to and from the runtime's collection kind and index, a cable's index being its packed word. They are the
one way between the two numberings; until the model stack is retired, the views turn a target into a lane through it
(the automation view's `laneFor`).

What names params by target:

- the pad-grid shortcuts and the automation view's param orders (`modulation/params/param_targets.h`);
- MIDI follow's maps, with `MidiFollow::ccOf(owner, target)` for the CC that follows a param, and the performance view's
  params, layout and shortcuts (global-effectable `ParamId`s);
- mod and MIDI knobs (`Knob::target`, a `modulation::KnobTarget`; `knobWord` and `knobTargetOf` convert it to and from
  the descriptor word the files and the model stack use), the global effectable's default knob layout, and what a
  learning menu teaches a knob (`getLearningThing()`);
- the cable set and the patch-cable menus, by `synth::CableTarget` word.

A clip's and the song's automation selection (`lastSelectedParamKind`, `lastSelectedParamID`) stays the runtime pair:
it is what the files record, a MIDI clip's keeping whatever kind was there with its CC.

param_target_check round-trips every runtime param of both plugins and every cable word through the bridge, and holds
each of these tables to its recorded runtime cells
(`tests/integration/scenarios/drivers/param_target_tables_reference.h`), and MIDIFollow.XML to the bytes it was written
as.

## Edits and undo by address

A param Edit (`SetParamValue`, `DeleteParamAutomation`, `PasteParamAutomation`, `RecordExpressionValue`;
`model/edits/`) is built from a `project::ParamAddress` alone, and finds its param below the Edit, through the bridge in
`model/project_bridge/param_stack.h`, as its `SetParamValue::Resolve` says:

- READ (`paramStackAt`): the param as it stands; an address that names nothing is refused.
- WRITE (`paramStackForWrite`): as a gold knob writes it: what it turns is made (a cable, a MIDI CC or expression lane),
  the take is written while the arrangement records the clip, and the owner is marked edited.
- EXACT (`paramStackExact`): exactly what the address names, never following a take; a lane is made, a cable is not.
  A caller that already holds an engine stack for its own reads passes that stack's address
  (`edits::addressFor`) with EXACT, so the Edit reaches the lane the stack names.

A setup write (`SetParamValue::Write::SETUP`) sets the value alone, with no undo step and no automation: the defaults
the slicer and the sample browser set up for the user.

Every lane has an address. The owners are the song; a clip (an audio clip, a MIDI or CV clip's CC and expression
lanes); an instrument (a synth or a kit) or a drum of any type, with the clip whose params they are; and, with no clip,
an instrument's or a sound drum's own params, which an arranger row edits. `edits::ownerAddressFor` names the owner of
an engine stack; edit_undo_redo checks over the corpus that the exact lookup of every lane's address finds the lane its
stack named.

Undo records the address. `ConsequenceParamChange` holds the lane's address and the state not in it, and reverts by
swapping that state with the lane `paramStackExact` finds; an Action keeps one record per address. A cable topology
edit's record (`ConsequenceInstrumentDelta`) holds its sound's address and reverts through the sound
`soundStackExact` finds. No Edit and no undo record holds an engine stack; the bridge builds one where the engine needs
it. What still takes a stack is the engine's own record sites (`Action::recordParamChange*`, which take the address from
it) and the reads beside an edit, both for the model stack's retirement to move.

## Base and played value

A lane's base (`DocAutoParam::current_value`, written only through `project::set_base`) is the model's: what the param
plays where it has no automation, and what a file saves. Its played value (`AutoParam::played()`) is the engine
cursor's: the lane at the playhead, interpolated, latched or overridden. The cursor is written by the executor
(playback, interpolation, the latch), by the interaction tier's playhead moves (each with the render held off, reading
lanes the render's way: [playback_moves.md](playback_moves.md)), by an edit's `stopInterpolating()`, `overrideFromNow()`
and `cancelOverriding()` (each with the render held off), and by four named operations:

- `setBase(lane, v)`: an edit, a file read or a setup default; the value is the base and is played.
- `playBase(lane)`: the cursor projected from the model after a read, a rebuild or a model-first write.
- `setPlayed(v)`: the played value alone, base untouched: a transient effect (the stutter's restore, a sample preview's
  adopted volume), or what a lane played carried across a rebuild of its owner.
- `adoptPlayedAsBase(lane)`: the legacy instrument's rule before a kernel that works on the base (deleting nodes in a
  region, shifting values, CC to channel pressure): what plays becomes the base, then the result is played.

## Recording while playing

While a clip records, the render owns a working copy of each lane it records into
(`modulation/automation/working_copies.h`) and is its only writer; the model's lanes are written by the interaction
tier alone. A knob, MIDI or MPE write takes the undo snapshot on the interaction tier, makes the lane's working copy if
it has none (the lane copied, with room for what the write can add and for every write still queued), and posts a
`RecordEvent` (`modulation/automation/record_events.h`) with what the live-record kernel reads beyond the lane's nodes:
the play position, the clip's length, direction and cut position, how far ahead to clear, the tick length. The render
writes the queued events into the working copies at the start of its next block, before it ticks; what the kernel
decides from the nodes (whether the region's start interpolates, which node of a straight run to drop) it decides then,
so a second write before that block sees the first. A lane being recorded plays its working copy.

The commit (`commitRecording`) runs before the interaction tier itself reads or edits lanes (each recorded write,
every Edit, undo and redo, an automation delete, a recorded note's MPE values), and before every job
(`commitBeforeJob`): with the render excluded it writes what is still queued, then copies every working copy the render
changed into the model and marks it for publishing, so the interaction tier sees every value recorded and nothing lands
after its edit. When recording stops the copies are released (retired: the render may be in a block that reads one);
undo and redo end the take too, since they rewrite lanes a copy would overwrite at the next commit. An event made in
another song, or whose lane has no copy any more, is dropped; a song being freed hands its queue over or drops it, and
its collections' copies go with them. With the queue full, or a lane with no address, the interaction tier writes the
copy itself, with the render excluded, after what is queued. The kernel writes the base after reading the old one,
where the cleared stretch returns to; the interaction tier sets only the played value.

A copy is held for the take (a knob's lane, which the latch may write), or while written: an MPE lane's copy is
released by a commit that finds the render did not write it since the last, so a take across many notes does not fill
the table of 32. Out of memory to release copies, they are ended instead: neither tier plays or writes them, and a
later commit frees them.

The latch's drift-back writes the working copy and logs no undo: whoever made the copy took the take's snapshot. A
lane a knob overrides while recording gets its copy when the knob turns (the latch may drift back on it, armed clip or
not); when recording starts, and before the job after a take ended, the lanes a knob is overriding get theirs, their
cursors read with the render held off one param manager at a time. Only the commit before a job makes these copies: an
edit that ends a take and commits along the way gets none before it is whole. A latch on a lane without a copy plays on
and writes nothing.

An Edit on a lane being recorded (a value or region from the automation view, a paste, a delete, a cable's base)
changes the model at once and posts the same change to the render as a `LaneCommand`
(`modulation/automation/lane_command.h`), held by the interaction tier until the commit after the render has made it to
the working copy, in order with the values recorded; the next commit carries both. A delete posts its command from
`AutoParam::deleteAutomation`, so a shift-turn's delete while recording carries it too, and a base set outside an Edit
(a knob on an unarmed clip, a value set with no recording) posts one. Any other change to a recording lane ends the
take for its collection instead (`endTake`): an edit of every lane at once (shifting, trimming, repeating, deleting or
inserting time, nudging, reversing, stripping), and the engine's own setters that rewrite a lane (a region written
outside an Edit, a note's MPE values written or cleared, MPE moved with a note, a CC's automation moved to another CC).
What is queued is written and the copies committed and released first; the next commit before a job makes copies
again for lanes a knob still overrides.

### Arrangement takes

While the arrangement records a clip, the clip a knob turns is cloned into a take (on the interaction tier), and the
take repeats its source at every loop end; a row with its own length repeats at its own end
(`modulation/params/staged_repeat.h`). The render never writes a model lane or makes a clip or a collection for it:

- At the loop end the render appends the repeat's notes and the length, swaps in the repeat's lanes staged ahead, and
  counts the repeat (`TakeRepeats`, on the take clip and on each such row). Each collection the repeat appends to holds a
  staged snapshot (`ParamCollection` `staged_`), its lanes with the next repeat appended, built from copies at the end
  of each job (`restageArrangementTakes`) and again when the source's collection republishes. The render swaps in all
  of a take's staged snapshots or none (a miss, `stagedRepeatMisses`: the repeat plays its old lanes until the next job
  publishes it); the snapshot it replaces is superseded, for the interaction tier to retire. A publish of a collection
  first drops its staged snapshot and retires its superseded one, which share its lanes.
- A working copy of the take's lanes stays live across the loop end: the render appends the repeat into it from the
  source's published lanes, within room the restage reserved, and records on through the seam. Each copy records how
  many of its take's repeats it holds. A copy the render cannot append into (behind its take, without room, its source
  lane not published) has its take ended instead (counted, `copiesEndedAtLoopEnd`); the next commit carries it.
- The interaction tier appends each counted repeat into the model first thing in every commit
  (`catchUpArrangementTakes`, with the render held off until the copies are committed): into a lane's working copy if it
  has one not holding the repeat yet, else into the model's lane. A copy is made from a caught-up take, so it holds every
  repeat counted.
- An audio take's loop end takes a spare instead of cloning: a clone of the source made in the restage pass, its lanes
  published, with room reserved in the containers the loop end inserts it into (`AudioClip::spare`; a miss is counted,
  `audioSpareMisses`, and the take does not go on). A spare is in no container, so no save or undo sees it, and is
  remade when the source's lanes republish (not when its sample or settings change); it is deleted with its take or when
  recording ends. Its clip id is taken when it is made, so the last, unused one of a recording keeps its id: clips made
  after the recording have ids one higher than if the render had made the take.

None of the render's own edits ends a take now; `endTake` on the render (counted, `renderTakeEnds`) only ends the copies,
and the next commit carries them.

Alpha counters: `renderUndoCalls` (undo logged on the render tier), `renderNodeGrowths` (a recorded write the reserved
room did not cover), `recordRingFullFallbacks`, `recordWritesWithoutAddress` (written by the interaction tier),
`workingCopyOverflows` (a lane not recorded into: the table of 32 was full or out of memory) and
`latchWritesWithoutCopy`, and for arrangement takes `stagedRepeatMisses`, `stagedRepeatSwaps`, `copiesEndedAtLoopEnd`,
`renderTakeEnds`, `audioSpareMisses` and `renderMadeCollections` (a collection made on the render). param-record-diff
checks across its recorded cases, the latch included, that the render logs no undo, reads and writes no model lane,
allocates nothing and grows no lane; that 70
writes with no render block between them leave the lane, its base and its played value as the same writes a block
apart; that a write undone before the next block stays undone; that a lane overridden before recording starts, or
when its take is undone, is copied, latched and given back by undo; and that a region, a delete, a paste, a shift, a
length change, a region written outside an Edit with a value still queued and a knob's base on an unarmed clip all keep
what was recorded around them, and that a take the render ends writes no model lane and is committed by the next commit. edit-undo-redo checks the table: found,
committed only once written, held for the take or while written, dropped, released and refused past its size, and a
snapshot built apart from being published. arrangement-take records the generated song RS_ARRANGE_TAKE (a synth, a kit
with a half-bar row and an audio clip) into the arrangement with knobs turned, a late one across a loop end, and its
source edited mid-take: the takes' lanes and output against its baseline, what the render plays across each repeat
checked on the render at each loop end, every repeat staged and swapped, every audio take a spare, nothing left after
recording, every collection's published lanes its model's, and no model read, write or collection made on the render.

## How the render reaches a lane

The render plays lanes it cannot change: each collection's published snapshot
(`modulation/params/lane_snapshot.h`), an immutable copy of its lanes with nodes, one SDRAM allocation per lane and
one per snapshot, keyed by paramId (which no re-patch or MIDI learn changes). Every lane read on the render -- the
cursor's next node, the value at a position, whether a param is automated -- goes through `ParamCollection::playedLane()`
(or `ModelStackWithAutoParam::laneView()`), a read-only `project::LaneView`. A lane with no nodes is an empty view,
base 0; every reader stops there. A lane being recorded plays its working copy instead ("Recording while playing"
above). The interaction tier reads the model, a playhead move's section included: the render is held off for it, and
the model is what the operation's end publishes.

A collection not yet published is read in the model, where the collection keeps its lanes (`findDocParamForId`): no
address is built and no store created, and a lane nothing holds is the unheld lane (no nodes, base 0, never written).
That read is counted.

The interaction tier publishes (`modulation/params/lane_publication.h`). Every path that hands out a collection's lane
to write, or rebinds its store, marks the collection. The outermost `LaneOperation` -- an Edit applied, an undo or
redo -- publishes when it ends, and each app-loop job's end publishes and settles the marks: between the steps of an
operation a collection's storage may be mid-change, and a caller may hold a lane across several writes. Publishing
walks the song's param managers (the song's, each clip's, each note row's), so a collection parked in undo history is
never read; for each marked collection whose lanes differ from its snapshot it builds a new one, sharing the lanes that
did not change, swaps it in with the render held off, and brings the collection's next event forward so the render
re-reads at its next block. A song is published before it is handed to the render (the load's arm, a cleared song's
swap, the boot song). The replaced snapshot and lanes are retired (`AudioEngine::retireMemory`, freed two blocks on);
a destroyed collection's are freed at once, being out of the render's reach with it. Out of memory, the old snapshot
stays published and the collection stays marked.

The render writes no model lane. A node
at or past a published lane's length is played without it, and a value set on
the render (a stopped synth clip's mono expression cleared) is played at once; each is posted to the interaction tier
(`requestStrayNodeErased`, `requestLaneBase`), which does it before its next job, if the song still plays that
collection (its address and serial) and the lane is as the render saw it: still ending in that node, or still holding
the base the render replaced. A base the render does not change is not posted. On a working copy the render erases the stray node or sets the base
itself, since it owns the copy.

Only the interaction tier changes a collection's lane storage: a cable's or a CC's lane added or erased, a store
created, bound to the tree's or copied out of it. Each change runs inside `EditGuard{ModelEdit::PARAM_STORE}`, which
excludes the render, so a lookup sees the storage before or after the change; a store a change replaces is freed when
the change returns.

The render tier is what `deluge::memory::onRenderTier()` says: the audio tier, or a render block
(`deluge::memory::RenderBlock`) for the tier that entered it: `deluge_app_render`'s, a host harness's, and each block of
the offline export loop (its recorded values, ticks and render, as `deluge_app_render`'s block holds them). A storage
task that preempts an export block is not the render.

Alpha counters: `renderModelReads` (a lane the render read in the model), `renderModelWrites` (a lane it wrote in the
model), `staleLanes` and `publishFailures` (a snapshot that differed from its model after
a publish, or could not be built), `laneRequestOverflows` (a request the full queue dropped), `publishedBytes` (the bytes
published lanes hold, reported as `LANES-MEM` at each stop), `unheldLaneWrites` (a write found in the unheld lane at the
render's next miss, undone) and `laneSplits` (a lane the interaction tier resolved that is not the one the render's
lookup finds: playback would miss an edit to it). render-song (`RS-SNAPSHOT`), the goldens (`RP-SNAPSHOT`) and
param-record-diff fail on a model read or write, or a dropped request; edit-undo-redo checks every collection's snapshot
against its model after each edit, undo and redo (`EUR-SNAPSHOT`); playhead-moves checks the out-of-memory path
(`publish_oom`) and the two requests (`PM-SELFREPAIR`, `PM-CLIPSTOP`). The goldens (`RP-LANES`), render-song (`RS-LANES`), param-record-diff and edit-undo-redo fail on
either; edit-undo-redo also drives a render-tier miss on an unbound set and checks the write detection. The
structural-edit probe's `PARAM_STORE` render count gates the render's store changes (`RP-STORE`, `RS-STORE`,
param-record-diff per block, edit-undo-redo). edit-undo-redo checks over the corpus, before and after its targets'
mutations, that every unpatched and patched lane the render's lookup finds is the one the owner's address resolves to.

## The descriptor

Each row is a `plugin::ParamInfo` (`src/plugin-api/types/param.h`):

| Field | Meaning |
|---|---|
| `file_name` | the name files give the param where they name it (a cable's destination, a learned knob); `paramNameForFile` reads it |
| `scope` | per voice or per sound |
| `default_value` | the word a new instance starts with; each owner's `initParams` writes it |
| `range` | the words the param can take |
| `mapping` | its `MappingKind`: how its word becomes host positions |
| `flags` | `ParamFlags`: CENTRED, MIDDLE_DETENT, NO_UNDO, FLOAT_INTERPOLATION, SMOOTHED, SMOOTHED_USER_ONLY |

The runtime's predicates (`isParamBipolar`, `shouldParamIndicateMiddleValue`, `doesParamIdAllowAutomation`,
`shouldRecordUnautomatedParamChange`, `shouldInterpolateWithFloat`, `paramNeedsLPF`) read the flags. One meaning
depends on state and stays a hook: the stutter rate's middle detent (`stutterRateIndicatesMiddle`), which a quantized
stutter doesn't show.

`tests/integration/scenarios/drivers/param_table_check.cpp` pins every row against the runtime: names, defaults, flags
and mappings, each against an oracle recorded from the code the tables replaced.

## Host positions

A host converts only through the mapping kinds (`src/plugin-api/types/param_mapping.h`) and never switches on a param:

| Kind | Knob (−64..+64) | Menu | Params |
|---|---|---|---|
| STANDARD | the word's top 7 bits, +64 for the top half step | 0..50 over every word | most |
| UNIPOLAR | the word's top 8 bits, offset | 0..50 over the non-negative words | phase widths, compressor threshold |
| PAN | STANDARD's | −25..25 | pan |
| CABLE_DEPTH | steps of 2^23, +64 at 2^30 | −5000..5000 hundredths | cable depths |

`preset_word` turns a preset's 0..50 number into a STANDARD word without the menu's end snap (50 is 2147483602), and
`knob_display_number` is the number a popup shows for a knob. Settings that aren't params (an arpeggiator setting, the
reverb's pan) call the same functions with the kind they share. Words are the plugin's: q31 here, possibly float in a
later instrument; CLAP's double values come at the L3 boundary through the same conversions.

## Effects

Each effect in `src/effects` declares its params as `EffectParam` fragments (`effects/param_fragment.h`): the curve
its word goes through around a neutral value (or none, for a word used as stored), the mapping and the flags. An effect
with curved params computes them with a pure `params_from(words)` (`Filter`, `ModFx`) or `final_value(fragment, word)`
(`Delay::kRate`, `Delay::kFeedback`, `Stutter::kRate`, `Reverb::kSend`). The app's adapters
(`model/fx/chain_inputs.cpp` and the delay and stutter code) fill the words from the runtime's sets and know nothing of
the curves. The curves are dsp-next's (`dsp-next/fixed/curves/param_curve.h`); the kernels themselves take final values
and stay param-free.

param_table_check holds the fragments to the runtime: a curved fragment's neutral and curve are the instrument's for
the same param, and a global-effectable row's mapping and flags are its fragment's.

## Instrument-private rules

The instrument's patching (`synth::matrix::kParamRules`, `src/synth/matrix/descriptors.h`) keeps each patched param's
chain, curve, `patching_bipolar` rule, neutral and range. They are not published: a host never sees how cables
combine.

## Not yet descriptor-driven

Display names (l10n, the menus), the codec's attribute names (`model/project_bridge/param_serialization.cpp`, grouped
by element), the identity enums and `Kind` below the bridge, edits and undo by address, ModelStack, and composing the
global-effectable rows from the fragments (its library may include only `plugin-api/types/`).
