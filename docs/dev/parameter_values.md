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
