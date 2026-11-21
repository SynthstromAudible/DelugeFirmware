# Parameter value layers

Fixed parameters, MIDI CCs, and patch-cable strengths resolve values through three owners:

| Layer | Owner | Access |
| --- | --- | --- |
| Preset baseline | `ModControllableAudio` for audio owners (including sound drums); `MIDIInstrument` for CCs | `getPresetValue(...)`, `getPresetMidiValue(...)`, or `getPresetPatchCableValues()` |
| Preset + clip offset | `ClipParamValue` owned by a `ParamSet`, `MIDIParam`, or `PatchCable` | `getClipValueStorage(id)->getValue()` |
| Clip value + current automation offset | `AutoParamValue` in the lazily allocated `ParamNodeVector` | `AutoParam::getCurrentValue()` / `getAutomationOffset()` |

`AutoParam` retains the clip-scalar binding and supplies the scalar's current
value to the curve's runtime state when resolving automation.

`ParamSet::getValue()` selects the clip value when there are no automation nodes,
and the evaluated automation value otherwise. Reading the clip or preset layer
does not need an AutoParam. Expression sets also own their clip scalars, but do
not bind to an output preset. MIDI instruments own a CC baseline array; each
`MIDIParam` owns its clip scalar. Audio outputs and sound drums own a baseline
table keyed by patch-cable route; each `PatchCable` owns its clip scalar.
Routes absent from the preset have a zero baseline. Both collections expose
`getClipValueStorage(id)`, so clip and preset values can be read without AutoParam.

Offsets use signed 64-bit arithmetic because the difference between INT32_MIN
and INT32_MAX needs 33 signed bits. Consumers receive a clamped int32 value.
Baselines are established on load or first binding. Ordinary clip edits,
playback, clip-value undo, and clip deletion leave them unchanged. A kit captures separate baselines for its global
effects and for each sound drum.

## Lifecycle

Baseline binding happens when a preset manager is backed up, when a clip obtains
its manager or claims its output after loading, when a note row obtains its drum,
and when an output activates a clip. Binding preserves the clip value; it
recalculates its offset against the new owner's baseline. Parameter-set cloning
rebinds automation to the destination scalar array after the existing memcpy.
Clones without automation retain the source's effective value as their clip value.

Normal front-panel gold-knob and parameter-menu edits change
the clip value outside live automation recording, even when the parameter already
has a curve. Live automation recording remains clip-local.
When editing a fixed parameter's clip scalar, the menu uses `ParamSet::getClipValue()`;
turning a knob starts from that value rather than the momentary automated value.
`ParamCollection` owns the layer decision. Scalar reads do not inspect automation
nodes. The nodes, current automation offset and interpolation remain intact, so
playback follows the shifted curve.
During live recording the same controls write automation. Explicit pad/region
edits modify automation. With no pad held or selected, automation-editor encoders
use the same scalar/recording routing as normal controls.
MIDI/expression and performance automation APIs retain their existing behavior.
Removing automation from an inactive MIDI clip updates its parameter state
without sending its scalar to the active output.
Deleting automation clears its offset and reveals the underlying clip value.
Undo snapshots the clip, effective and unclipped curve values, plus the node
reference and nodes.

Automation nodes store signed 64-bit offsets from the clip value in memory.
Changing a clip scalar shifts its entire curve without rewriting the nodes.
Interpolation operates on those offsets and clamps only the resolved audible
value, so a shifted ramp keeps progressing even beyond a parameter limit.
Unbound temporary parameters and clipboard nodes use a zero reference to retain the existing absolute-value paste
behavior across different parameter types. Append and stolen-node operations
translate between source and destination references to preserve their sound.


## Persistence

Existing parameter attributes and automation records retain their format and
store effective values. Each saved node is `clip value + node offset`; loading
subtracts the clip value to reconstruct its offset. If the separate clip-value
tag arrives after the automation, loading rebases the nodes while preserving
their absolute values. The legacy file format is int32, so values outside the
parameter range are clamped in those legacy fields. Supplemental
`relativePatchedValues`, `relativeUnpatchedValues`, MIDI `relativeValue`, and cable
`relativeAmount` fields retain the unclamped scalar, live offset and node offsets.
Their hexadecimal payload starts with `0x`, two signed 64-bit values (scalar and
live offset), then pairs of 32-bit node positions and signed 64-bit offsets.
They follow the legacy data and restore only nodes retained by its timeline limit. Song output definitions additionally contain
`presetPatchedValues` and `presetUnpatchedValues` tags. Clip parameter blocks
contain `clipPatchedValues` and `clipUnpatchedValues` tags for automated parameters,
whose current value alone cannot recover their underlying clip value. Entries
are decimal int32 values keyed by `p` followed by the collection's parameter ID.
`presetMidiValues` stores output CC baselines, and an automated MIDI `param`
adds a decimal `clipValue`. `presetPatchCableValues` stores output route baselines
keyed by the unsigned route descriptor; automated cables add `clipAmount`,
including depth-control cables. MIDI vector relocation and patch-cable sorting
rebind AutoParams to their moved clip scalars.
These IDs are part of this format and must be migrated if the enums change.

Older firmware can skip these tags and read the effective values. New firmware
loads older files using their saved instrument/default values as the baseline,
or the first associated clip's values if no instrument defaults were saved.
It never rereads a preset file to guess a lost original baseline. Saving a preset
omits the song's historical baseline, so loading it establishes its saved sound
as a fresh baseline. After a successful instrument preset save, its saved values
also become the in-memory baseline. All associated clip, drum-row and backup
scalars are rebased without changing their effective values or relative curves.
Song saving does not change the song baseline. Preset saving temporarily records
the exact values serialized (8 bytes per entry on ARM plus array capacity), then
releases that snapshot. This prevents automation advancing during SD IO from
changing the new baseline.

A malformed/incomplete fixed-parameter baseline tag falls back to capture from
the saved values. Dynamic baseline tags are sparse: absent CCs/routes mean zero.
Clip metadata is used only for parameters whose automation was successfully
loaded. Unsupported metadata is skipped.

## Validation and costs

See [the parameter tests](../../tests/unit/parameters/README.md) for the
CI-executable production-code tests. Emulator A/B/C inspection uses layout
offsets compiled from each revision, not assumed shared object layouts.

Unautomated parameters retain only their lazy curve pointer and scalar binding:
`AutoParam` is 8 bytes on ARM. Each clip scalar is 12 bytes, storing its offset
normally as the difference of two int32 endpoints. Detachment or rebinding of
saturated values can need more than 33 bits; a second pointer tag selects a
signed 64-bit offset encoded in those same two words. This preserves the full
unclamped value without growing the object. The zero-offset tag still provides
a direct shared-preset read.
Automation playback state is allocated with the node vector only when needed.
The runtime value stores only the full-width offset and its cached base/result
(16 bytes on ARM); the binding and standalone scalar remain with `AutoParam`.
The complete `ParamNodeVector` is 88 bytes before allocator rounding. Render
reads with parameter-manager context can use its existing automation summary
to read the clip scalar directly for an unautomated parameter. No pointer to
that movable summary is retained in the parameter set.
Cloning copies curve data into independent storage. `AutoParam::cloneFrom()`
resets interpolation increments and override timing; `AutoParam::beenCloned()`
clears override timing. Undo swaps curve ownership while retaining the owning
parameter's clip binding and live interpolation/override context. Evaluated
automation values are cached against the actual clip value, so shared-baseline
changes do not require explicit cache invalidation.

Nodes remain 16 bytes for full-width offsets, compared with 12 bytes before the
layering refactor. Node buffers and fixed parameter collections use aligned,
tightly rounded allocator blocks to avoid doubling allocations at size-class
boundaries. Other allocation clients retain their existing rounding policy.
Audio-path collection getters share a non-inlined resolver to reduce duplicated
layer-selection code and instruction-cache pressure.
Preset arrays and additional firmware code still consume memory; emulator A/B
measurements must include those costs as well as heap allocations. Hardware
rendering performance needs separate verification before release.




## Control access boundary

Normal parameter controls call `ParamCollection::getValueForControl(id, context)`
and `setValueFromControl(value, parameterContext, position, length)`. The context
requires a parameter ID and collection, not an AutoParam pointer. A nonzero
region length with a timeline selects an explicit automation edit; otherwise
ordinary edits select the clip scalar,
and recording/expression select live automation input. Unchanged clip scalars return before the dispatcher resolves AutoParam. Changed scalar
edits still access automation for undo and effective-value notifications.

Automation drawing in the editor calls `automation().setValueForRegion` explicitly.
Empty regions are skipped, including endpoint remainders at the smallest zoom,
so drawing cannot fall through to clip-scalar editing.

Fixed, MIDI and cable reads share the production `readControlValue` operation.
Preset/clip reads do not dereference AutoParam. MIDI reads do not create missing
CC entries. The audio render getters and their automation-summary fast paths
are separate from this control API and unchanged.

### AutoParam access roles

AutoParam publicly exposes evaluated reads, scalar inspection, serialization
writes, read-only node queries, and an explicit `automation()` editing interface.
Ordinary parameter controls still use ParamCollection. Automation drawing uses
`automation().setValueForRegion(...)`; deletion uses `automation().erase(...)`
(`delete` is a C++ keyword).

Mutation roles live in `auto_param_access.h`. Their constructors are private to
the production owners that need them. Friendship applies to the small interface,
not to AutoParam itself, so a setup caller does not gain live-recording or
playback-state access.

| Interface | Permitted operations | Authorized callers |
| --- | --- | --- |
| `AutoParamSetup` | Initialization, load/restore, legacy conversion, preview and temporary engine values | Parameter owners/loaders, sound initialization, song tempo setup, sample preview and stutter |
| `AutoParamOwnership` | Bind/rebind, clone, undo capture/swap, release or forget owned nodes | Parameter containers, MIDI/patch-cable owners, undo/action code |
| `AutoParamPlayback` | Advance playback, interpolate, seek, clear live offsets and override state | Parameter collections, owning MIDI entries, clip/song playback |
| `AutoParamLiveInput` | Live recording, per-note expression, temporary performance-pad overrides | ParamCollection, NoteRow, PerformanceView only |
| `AutoParamAutomation` | Explicit curve operations, including region editing, copy/paste and erase | Public through `automation()`; no setup, binding or playback-state setters |

The mutable node vector is no longer public on AutoParam. `nodeAt()` returns
`const ParamNode*`; node counts, searches and allocation identity expose no
mutable vector or playback-state pointer. Curve operations stay behind the
explicit editor interface, and node ownership changes use the owning interface.

`AutoParamData` contains the independently owned data algorithms from
`auto_param_value.cpp` and `auto_param_storage.cpp`. AutoParam privately inherits
that storage and returns only `const AutoParamData&` from `data()`. A caller
cannot convert a live AutoParam to mutable data or use that view to edit nodes.
Standalone data objects remain usable for production data operations and unit
tests. Tests exercise these exact production implementations, not copies, and
have no test-only friendship. MIDI tests exercise the real owning entry/vector
APIs for loading, playback and cleanup. C++ assertions check the inaccessible
constructors, private setters, immutable live data view and hidden node storage.

The interfaces contain only transient references and inline forwarding methods.
They add no per-parameter fields or virtual dispatch. Compile-time size and layout
assertions retain AutoParam's two-pointer representation. As elsewhere in the
firmware, these boundaries do not defend against deliberate `const_cast` or
`reinterpret_cast` misuse, or independently modifying an owner's ClipParamValue.
