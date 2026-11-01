# Params: the descriptor model

How a plugin (the Deluge instrument, an effect) describes its params, and what a host may do with them. The legacy
q31 instrument and any later instrument publish through the same model; only the plugin knows its stored words.

## Identity

A param is a `plugin::ParamId` in its plugin's table: `instrument::kParams` (`src/instruments/deluge/params.h`) for a
sound, `global_effectable::kParams` (`src/effects/global_effectable/params.h`) for the song, a kit's affect-entire and
an audio track. Ids are pinned forever by the tables' specs. The runtime's own numbering (the `params::` enums, a
collection `Kind` and index) maps to ids through `model/project_bridge/param_ids.cpp`, and
`params::paramInfoFor(kind, index)` returns a runtime param's row.

## The descriptor

Each row is a `plugin::ParamInfo` (`src/plugin-api/types/param.h`):

| Field | Meaning |
|---|---|
| `file_name` | the name files give the param where they name it (a cable's destination, a learned knob); `paramNameForFile` reads it |
| `scope` | per voice or per sound |
| `default_value` | the word a new instance starts with; each owner's `initParams` writes it |
| `range` | the words the param can take |
| `mapping` | its `MappingKind`: how its word becomes host positions |
| `flags` | `ParamFlags`: CENTRED, MIDDLE_DETENT, NOT_AUTOMATABLE, NO_UNDO, FLOAT_INTERPOLATION, SMOOTHED, SMOOTHED_USER_ONLY |

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
by element), the identity enums and `Kind`, `ParamDescriptor`, ModelStack, and composing the global-effectable rows
from the fragments (its library may include only `plugin-api/types/`).
