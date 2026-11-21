# Parameter value layers

Fixed parameter sets own clip scalars independently from AutoParam. Outputs and
sound drums retain loaded patched/unpatched baselines. Each clip offset is stored
normally as two int32 endpoints and evaluated with signed 64-bit arithmetic. A second pointer tag permits a wider offset in the same two words when detachment or rebinding must preserve a saturated value. Automation
nodes store signed 64-bit offsets from the clip scalar in memory; editing that
scalar shifts the relative curve, and deleting automation reveals the scalar.
Cloning with automation and undo preserve the clip scalar and relative curve.
Cloning without automation retains the source's effective value as the new clip
scalar.

Normal menus and unselected mod encoders read/edit the clip scalar. Recording and
explicit selected-region edits use automation. Stopped indicators show the scalar.

Song files retain effective parameter/node values for legacy readers and save
separate baseline and clip-scalar metadata. Existing files use their saved values
as the baseline. Preset files establish a new baseline from their saved sound.

The CppUTest suites compile production code. They cover the value model, node
evaluation and attribute IO; full UI/audio behavior requires emulator testing.

Automation playback state is allocated with the curve. On ARM, AutoParam is
8 bytes and each clip scalar is 12 bytes. Undo retains live interpolation and
override context while exchanging curve ownership. Fixed collections and node
buffers use tightly rounded allocations; shared audio getters limit duplicated
layer-resolution code.

The runtime value stores only the full-width offset and its cached base/result
(16 bytes on ARM); the binding and standalone scalar remain with `AutoParam`.
The complete `ParamNodeVector` is 88 bytes before allocator rounding. Render
reads with parameter-manager context can use its existing automation summary
to read the clip scalar directly for an unautomated parameter. No pointer to
that movable summary is retained in the parameter set.

Expression controls remain live overrides rather than shifting the scalar and automation curve.

Supplemental fixed-parameter metadata stores the unclamped scalar and live offset as signed 64-bit hexadecimal values, followed by node-position/offset pairs. Legacy absolute attributes remain available to released firmware.

After a successful instrument preset save, its saved values
also become the in-memory baseline. All associated clip, drum-row and backup
scalars are rebased without changing their effective values or relative curves.
Song saving does not change the song baseline. Preset saving temporarily records
the exact values serialized (8 bytes per entry on ARM plus array capacity), then
releases that snapshot. This prevents automation advancing during SD IO from
changing the new baseline.


## Control access boundary

Normal parameter controls call `ParamCollection::getValueForControl(parameterContext)`
and `setValueFromControl(value, parameterContext, position, length)`. The context
includes AutoParam, but fixed-parameter scalar reads do not dereference it. A nonzero
region length with a timeline selects an explicit automation edit; otherwise
ordinary edits select the clip scalar,
and recording/expression select live automation input. Unchanged clip scalars return before the dispatcher resolves AutoParam. Changed scalar
edits still access automation for undo and effective-value notifications.

Automation drawing in the editor calls `AutoParam::setValueForRegion` explicitly.
Empty regions are skipped, including endpoint remainders at the smallest zoom,
so drawing cannot fall through to clip-scalar editing.

Fixed-parameter reads use the production `readControlValue` operation.
Preset/clip reads do not dereference AutoParam. The audio render getters and their automation-summary fast paths
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
have no test-only friendship. C++ assertions check the inaccessible
constructors, private setters, immutable live data view and hidden node storage.

The interfaces contain only transient references and inline forwarding methods.
They add no per-parameter fields or virtual dispatch. Compile-time size and layout
assertions retain AutoParam's two-pointer representation. As elsewhere in the
firmware, these boundaries do not defend against deliberate `const_cast` or
`reinterpret_cast` misuse, or independently modifying an owner's ClipParamValue.
