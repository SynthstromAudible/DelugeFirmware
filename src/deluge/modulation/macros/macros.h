/*
 * Copyright © 2014-2023 Synthstrom Audible Limited
 *
 * This file is part of The Synthstrom Audible Deluge Firmware.
 *
 * The Synthstrom Audible Deluge Firmware is free software: you can redistribute it and/or modify it under the
 * terms of the GNU General Public License as published by the Free Software Foundation,
 * either version 3 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY;
 * without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along with this program.
 * If not, see <https://www.gnu.org/licenses/>.
 */

#pragma once

#include "io/midi/learned_midi.h"
#include "modulation/params/param.h"
#include "util/d_string.h"
#include <cstdint>

class MIDICable;
class Action;
class Clip;
class Output;
class MelodicInstrument;
class ModelStackWithTimelineCounter;

// A macro: one source (learned CC, gold knob, or automation lane) broadcast to up to
// kNumTargetSlots target destinations on the active clip. On a MIDI clip a destination is an
// output CC; on a synth clip it is an internal sound parameter (the automation-view set). Target
// values are written into the clip's own params, so they reach the output/sound and record into
// each destination's automation lane. A source CC is consumed; to forward/record it too, include
// it as one of its macro's targets.
// Config is per-track: the four macros live on each MelodicInstrument and serialize with it
// (song + preset). Configured in the clip's menu.
namespace Macros {

constexpr int32_t kNumMacros = 4;
constexpr int32_t kNumTargetSlots = 8;
constexpr uint8_t kNoDestination = 255;
constexpr uint8_t kMaxMidiDestination = 127;
constexpr uint8_t kMaxValue = 127;  // from/to and CC output range, and the source value range
constexpr uint8_t kDefaultFrom = 0; // output at source = 0
constexpr uint8_t kDefaultTo = 127; // output at source = 127 (defaults give pass-through)

// Which flavor of destinations a clip's macros drive. MIDI clips target output CCs (raw 0..127
// byte). Synth clips target internal params, encoded as midi-follow's single-byte soundParamId:
// bytes below params::UNPATCHED_START (90) are Kind::PATCHED paramIDs, bytes from 90 up are
// Kind::UNPATCHED_SOUND paramIDs + 90. Everything keyed on the raw byte (ownership, shadowing,
// cascades) works identically in both domains.
enum class Domain : uint8_t { MIDI, SYNTH };

// Synth targets are knob positions 0..128 (center 64), so bipolar params (pan) get a true center
// and the +64 extreme is reachable; MIDI targets are CC values 0..127.
constexpr uint8_t kMaxValueSynth = 128;
inline uint8_t maxTargetValue(Domain domain) {
	return (domain == Domain::SYNTH) ? kMaxValueSynth : kMaxValue;
}

// The domain of a macro-capable output. Only call for outputs macroClipInstrument() accepts.
Domain domainForOutput(Output* output);

// Validates the clip belongs to a macro-capable track (MIDI or synth) and returns its instrument,
// else null. Kits, audio and CV tracks have no macros.
MelodicInstrument* macroClipInstrument(Clip* clip);

// Each macro also appears as an automatable "lane" in MIDI automation view, stored as a pseudo-CC
// MIDIParam at these IDs. They sit above the 0..127 CC byte range so no real target CC (max 127)
// can collide, and they never emit MIDI (suppressed in MIDIParamCollection::sendMIDI).
constexpr int32_t kMacroParamIDBase = 128;
constexpr int32_t kNumMacroParams = kNumMacros;
inline bool isMacroParamID(int32_t id) {
	return id >= kMacroParamIDBase && id < kMacroParamIDBase + kNumMacroParams;
}
inline int32_t macroIndexFromParamID(int32_t id) {
	return id - kMacroParamIDBase;
}
inline int32_t paramIDForMacro(int32_t idx) {
	return kMacroParamIDBase + idx;
}

// ── Macro Cascade ──
// A target may point at a HIGHER-indexed macro (via its pseudo-CC lane id) instead of a real CC:
// the slot scales the source into that macro's input, which then fans out again through its own
// targets. Strictly one-way (macro N may only target N+1..), so the graph is acyclic and fan-out
// recursion is bounded. A macro driven this way has its own source shadowed (one feed per macro),
// and its Active flag mutes its whole downstream branch live (baking follows the lane as usual).
inline int32_t numCascadeDestinations(int32_t macroIndex) {
	return kNumMacros - 1 - macroIndex;
}
inline bool isCascadeDestination(int32_t destination, int32_t macroIndex) {
	return isMacroParamID(destination) && macroIndexFromParamID(destination) > macroIndex;
}

// ── Destination byte space ──
// SYNTH destination byte <-> (Kind, paramID). Cascade ids (128-131) decode to the downstream
// macro's own lane param, which is where a cascade bake writes.
void decodeSynthDestination(uint8_t destination, deluge::modulation::params::Kind* kindOut, int32_t* idOut);
// The byte for a (kind, param) pair, or -1 if the pair isn't a valid macro destination (only
// PATCHED and UNPATCHED_SOUND params are, and never the macro lane params themselves).
int32_t synthDestinationForParam(deluge::modulation::params::Kind kind, int32_t paramID);

// Resolves a main-grid pad (x,y) to the destination byte it would pick in this domain, or -1 if the pad
// can't be a macro target. SYNTH reads the const param-shortcut grids (+ the shared second layer); MIDI
// reads midiFollow's cached CC-per-pad grid. Shared by the automation-lane picker and note-view picker.
int32_t macroDestinationForPad(Domain domain, int32_t x, int32_t y, bool secondLayer = false);

// The destination byte of macro macroIndex's own automation lane: the pseudo-CC id (128+idx) on
// MIDI clips, the UNPATCHED_MACRO_n soundParamId byte (117+idx) on synth clips.
uint8_t laneDestination(Domain domain, int32_t macroIndex);

// A macro's destinations form one contiguous dial/position space per domain: MIDI = CC 0..127 then
// this macro's cascade ids; SYNTH = the targetable param list (automation-view order) then the
// cascade ids. Used by the quick-edit dial and the menu picker.
int32_t numDestinations(Domain domain, int32_t macroIndex);
int32_t destinationForPosition(Domain domain, int32_t macroIndex, int32_t position);    // -1 if out of range
int32_t positionForDestination(Domain domain, int32_t macroIndex, uint8_t destination); // -1 if not in the space

// Whether `destination` may be assigned as a target of macros[macroIndex] in this domain: a real CC (MIDI),
// a targetable param byte (SYNTH - never the lane bytes 117-120), or a higher-indexed cascade id.
bool isValidTargetDestination(Domain domain, int32_t destination, int32_t macroIndex);
// MIDI-domain shorthand, kept for the pure-MIDI call sites.
inline bool isValidTargetDestination(int32_t destination, int32_t macroIndex) {
	return isValidTargetDestination(Domain::MIDI, destination, macroIndex);
}

// Appends a destination's display label: "Macro N" for a cascade id, the param name on a synth
// track, else the MIDI device's CC name if loaded, else "CC <n>".
void appendDestinationName(StringBuf& buf, MelodicInstrument* instrument, uint8_t destination);

// If an automation-lane selection (kind, paramID) on this output is a macro lane, returns the
// macro index, else -1. MIDI lanes are the pseudo-CC ids 128-131 (their clips track lanes by id
// alone, kind is ignored); synth lanes are the UNPATCHED_SOUND macro params.
int32_t macroIndexForLaneSelection(Output* output, deluge::modulation::params::Kind kind, int32_t paramID);

// One target CC slot: the source value 0..127 is scaled linearly onto [from, to]:
//   out = clamp(from + (to - from) * input / 127, 0, 127)
// Set from > to to invert the response; from == to sends a constant. from/to are the A/B captures.
struct MacroTargetSlot {
	uint8_t destination = kNoDestination; // kNoDestination = OFF, else 0..kMaxMidiDestination
	uint8_t from = kDefaultFrom;          // 0..kMaxValue, output when source is at 0 (capture A)
	uint8_t to = kDefaultTo;              // 0..kMaxValue, output when source is at 127 (capture B; from>to inverts)
	bool send = true;                     // the "Send" toggle: muted when false, but keeps destination/from/to
};

struct Macro {
	LearnedMIDI source;     // external-CC source (when sourceKnob < 0)
	int8_t sourceKnob = -1; // instead of a CC, one of the 2 physical gold knobs (0 or 1) can be the source; -1 = none
	uint8_t sourceKnobPos = 0; // live 0..kMaxValue position a gold-knob source accumulates into (not serialized)
	bool active = true;        // on by default; the macro fires only when active (and the feature is enabled)
	String name;               // optional user label; the lane shows "Macro N: <name>", or plain "Macro N" if empty
	MacroTargetSlot targets[kNumTargetSlots];
};

// Whether the macro feature is turned on in Community Features. Gates menu visibility and,
// with each macro's Active flag, whether macros fire. This is the global off-switch.
bool isEnabled();

// Returns the index of a macro (other than macros[exceptMacro], and other than the given slot) whose
// target already targets this CC, or -1 if none; if non-null, *slotOut receives that target's
// slot. Duplicate destination CCs are allowed but flagged.
int32_t findTargetDestinationOwner(const Macro* macros, uint8_t destination, int32_t exceptMacro, int32_t exceptSlot,
                                   int32_t* slotOut = nullptr);

// The OWNER of a destination CC is the target that drives it - it alone bakes automation and sends
// live, so exactly one target ever drives a CC. Ownership ranks targets of ACTIVE macros above
// inactive ones (an inactive macro must never block an active one from driving), then scan order
// (macros, then slots) breaks ties. Every other target on the same CC is SHADOWED: it keeps the CC
// in its config (and the UI warns), but it is inert. Because rank depends on `active`, flipping a
// macro's active flag can hand a contested CC between macros - use setMacroActive() for that, which
// re-bakes accordingly. Returns the macro owning `destination` if macros[macroIndex].targets[slot] holding
// it would be shadowed, or -1 if that position would be the owner (destination may be a pending value the
// slot doesn't hold yet).
int32_t findShadowingOwner(const Macro* macros, uint8_t destination, int32_t macroIndex, int32_t slot);
inline bool isTargetShadowed(const Macro* macros, int32_t macroIndex, int32_t slot) {
	uint8_t destination = macros[macroIndex].targets[slot].destination;
	return destination != kNoDestination && findShadowingOwner(macros, destination, macroIndex, slot) >= 0;
}

// True when an assigned target won't drive its CC (it isn't the CC's owner). These are the targets
// the UI flags with a blinking target-button LED.
bool targetHasConflict(const Macro* macros, int32_t macroIndex, int32_t slot);

// Shows a "<destination> used by Macro <ownerMacro+1>" popup (destination = device CC name or "CC n"), used by the
// quick-edit target readout when the held slot is shadowed. `persistent` keeps it up until
// cancelPopup() (used while a quick-edit button is held).
void showDestinationConflictPopup(uint8_t destination, int32_t ownerMacro, bool persistent = false);

// Persistent held-target readout: the destination name then the "From - To" range (synth params in
// their menu range 0-50/-25..+25, MIDI CCs raw 0-127), or "Target Assign" when the slot is OFF. On
// showConflict, an in-use (shadowed) target shows its owner instead of a range. Shared by the
// automation-lane quick-edit editor and the note-view target picker.
void showTargetRangeReadout(MelodicInstrument* instrument, int32_t macroIndex, int32_t target, uint8_t destination,
                            bool showConflict);
// Seeds the gold-knob LED bars to the target's From (knob 0) / To (knob 1) while its button is held.
void showTargetKnobIndicators(MelodicInstrument* instrument, int32_t macroIndex, int32_t target);
// Nudges a target's From (whichKnob 0) or To (whichKnob 1) by offset, clamped to the domain max,
// marks the instrument edited and re-bakes just that lane. Returns true if the endpoint changed.
bool editTargetEndpoint(Clip* clip, MelodicInstrument* instrument, int32_t macroIndex, int32_t slot, int32_t whichKnob,
                        int32_t offset);

// Which slot (first match, -1 if none) holds a pad's primary / second shortcut-layer destination as a
// target of `macro`. `primary`/`second` are the pad's two candidate destinations (-1 if the pad has none
// on that layer). Used by the note-view picker's overlay colouring and tap handling.
struct LayerAssignment {
	int32_t primarySlot = -1;
	int32_t secondSlot = -1;
};
LayerAssignment layerAssignment(const Macro& macro, int32_t primary, int32_t second);

// True if any of the four macros deviates from its defaults (a learned source, a target CC set,
// or deactivated). Gates serialization so an all-default instrument writes nothing.
bool anyMacroConfigured(Macro* macros);

// Folder for per-macro preset files under SETTINGS. Presets are portable target configs, shared.
constexpr char const* kPresetsFolder = "SETTINGS/MACRO_PRESETS";

// Which macro a preset browser (load/save) is acting on. Set by the menu item before opening the
// browser UI, read by the UI's performLoad()/performSave().
extern int32_t presetMacroIndex;

// If the incoming CC matches any active macro's learned source on the active MIDI clip's instrument
// (and that instrument has macros enabled), writes its value to that macro's target CCs. Returns
// whether it matched (and so consumed the message).
bool tryMacro(MIDICable& cable, int32_t channelOrZone, int32_t ccNumber, int32_t value);

// Turning physical gold knob whichKnob (0 or 1): if any active macro on the active MIDI clip has that
// knob as its source, accumulate its live position by offset and drive its targets. Returns whether
// it matched (so the caller suppresses the knob's normal action - a knob source is dedicated).
bool tryKnobMacro(int32_t whichKnob, int32_t offset);

// Note-view MACRO mode maps the four macros onto these mod-knob-mode buttons (the ones normally
// attack/release, delay time/amount, sidechain/reverb, mod rate/depth). The other four go dark.
constexpr uint8_t kMacroModButtons[kNumMacros] = {2, 3, 4, 5};

// Macro index (0..kNumMacros-1) for a mod-knob-mode button, or -1 if that button isn't a macro button.
inline int32_t macroFromModButton(int32_t button) {
	for (int32_t m = 0; m < kNumMacros; m++) {
		if (kMacroModButtons[m] == button) {
			return m;
		}
	}
	return -1;
}

// Drives macro `macroIndex` from a gold-knob turn while note-view MACRO mode is active: accumulates
// its live position by `offset` and (when the macro is active) fans out to its targets, on both synth
// and MIDI clips. Shows the param-style "<name-or-Macro N>: <value>" readout (or ": (Inactive)" when
// the macro won't fire). Always returns true - MACRO mode owns the knob regardless.
bool driveMacro(int32_t macroIndex, int32_t offset);

// Called each playback tick for a clip: for every macro whose automation lane is playing (has nodes
// and is active), reads the lane's current automated value and fans it out to the targets live. This
// makes a recorded/drawn macro lane drive its targets on playback - the lane is the source of truth,
// like a normal automated param. No-op while recording and for macros with no lane automation.
void applyMacroLaneAutomation(Clip* clip, ModelStackWithTimelineCounter* modelStack);

// Bakes macro macroIndex's automation-lane curve (pseudo-CC paramIDForMacro(idx)) into its target CC
// automation lanes on the given clip, scaled per target. Call after a user edits/clears the macro
// lane or changes a target's config. No-op if the macro lane's param was never created on this clip
// (nothing was ever baked, so the targets are left alone). If action != null, target clears are
// snapshotted into it so one BACK restores the macro lane and its targets together.
void reFanMacro(Clip* clip, int32_t macroIndex, Action* action);

// Re-bakes a single target slot (cheaper than reFanMacro when only its from/to/send changed).
void reFanTarget(Clip* clip, int32_t macroIndex, int32_t slot, Action* action);

// Repoints a target at a new destination CC: clears the bake left on the old CC (so no ghost lane
// keeps playing) unless another target still targets it, then bakes the macro curve into the new CC.
// Both writes are snapshotted into one undo action, so BACK restores any automation this replaced.
void changeTargetDestination(Clip* clip, int32_t macroIndex, int32_t slot, uint8_t newDestination);

// Flips macros[macroIndex].active. Ownership of a duplicated CC ranks active macros first, so a
// flip can hand a contested CC between macros: the old owner's bake is cleared and the new owner's
// re-baked (one undoable action), mirroring changeTargetDestination()'s handoff when a target leaves a CC.
// Always flip `active` through this rather than writing the field.
void setMacroActive(Clip* clip, int32_t macroIndex, bool active);

// Serializes/deserializes an instrument's macros as a <macros> block within the instrument's
// own XML. Reused for both the song file and standalone instrument presets.
void writeMacrosToFile(Serializer& writer, Macro* macros, Domain domain);
void readMacrosFromFile(Deserializer& reader, Macro* macros, Domain domain);

// Save/load a preset: a macro's targets only (not its source or active state), so a preset is a
// portable target configuration applicable to any macro. writeMacroPreset() writes the body between
// the browser's createXMLFile() and closeFileAfterWriting(); loadMacroPreset() replaces
// macros[macroIndex]'s targets (keeping its source and active state), clears baked lanes left on
// replaced CCs and re-bakes the macro lane into the new set on `clip`. Loaded CCs another macro
// also targets resolve by the usual ownership rank (shadowed slots warn via the target LEDs).
void writeMacroPreset(Serializer& writer, Macro* macros, int32_t macroIndex, Domain domain);
Error loadMacroPreset(FilePointer* fp, Clip* clip, Macro* macros, int32_t macroIndex);

// Snapshots the current value of each configured target CC on `clip` into that target's from
// (toMax=false, "Capture From") or to (toMax=true, "Capture To"). The source then morphs the targets
// between the two captured states. Targets whose CC has never been touched on this clip (no param
// exists, so no known value) keep their existing from/to. Returns whether anything was captured.
bool capture(Clip* clip, int32_t macroIndex, bool toMax);
}; // namespace Macros
