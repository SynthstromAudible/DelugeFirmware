#include "scenes/bind/knob_assignment.h"

#include "effects/global_effectable/params.h"
#include "instruments/deluge/cables.h"
#include "instruments/deluge/params.h"

#include <algorithm>

namespace deluge::scenes::bind {

namespace {

using project::OwnerId;
using project::ParamAddress;
using project::ParamSpace;
namespace ge = global_effectable::param;

/// The compressor settings the lower knob edits in the song's compressor view, in its order.
constexpr std::string_view kCompPaths[] = {"audioCompressor/ratio", "audioCompressor/attack", "audioCompressor/release",
                                           "audioCompressor/compHPF", "audioCompressor/compBlend"};
/// The filter views: low-pass, high-pass, EQ.
enum class FilterView : uint8_t { LPF, HPF, EQ };
/// The mod-FX views: depth, feedback, offset.
enum class ModFxView : uint8_t { DEPTH, FEEDBACK, OFFSET };

std::optional<uint32_t> clip_id(const std::optional<project::ClipAddress>& clip) {
	return clip ? std::optional{clip->clip_id} : std::nullopt;
}

/// A sound's knob: its controls name the param of its mode's button.
KnobAssignment sound_knob(const project::Song& song, const SoundFocus& focus, uint8_t mode, int32_t knob) {
	const std::vector<project::ControllerMapping>* controls = nullptr;
	OwnerId owner{};
	if (focus.slot.kind == project::SlotAddress::Kind::DRUM) {
		const project::DocDrum* drum = song.drum_structure.find(focus.slot.id);
		controls = drum != nullptr ? &drum->controls : nullptr;
		owner = OwnerId::drum(focus.slot.id);
	}
	else {
		const project::DocOutput* output = song.output_structure.find(focus.slot.id);
		controls = output != nullptr ? &output->controls : nullptr;
		owner = OwnerId::instrument(focus.slot.id);
	}
	if (controls == nullptr) {
		return {};
	}
	const project::ModKnobControl control{.button = mode, .knob = static_cast<uint8_t>(knob)};
	const auto it =
	    std::ranges::find_if(*controls, [&](const project::ControllerMapping& m) { return m.control == control; });
	if (it == controls->end()) {
		return {};
	}
	const ParamAddress address{
	    .owner = owner, .space = ParamSpace::ENGINE, .id = it->target, .clip_id = clip_id(focus.clip)};
	const plugin::ParamKind kind = instrument::is_cable_depth(it->target) ? plugin::ParamKind::PATCH_CABLE
	                               : it->target.value < instrument::kUnpatchedFirstId
	                                   ? plugin::ParamKind::PATCHED
	                                   : plugin::ParamKind::UNPATCHED_SOUND;
	return {.target = address, .kind = kind};
}

/// A chain's knob: the global effectable's table over the mode, the chain's filter and mod-FX views, and on the song
/// its compressor view. A clip's chain trades the song's compressor knobs for the sidechain volume and pitch.
KnobAssignment chain_knob(const KnobFocus& focus, OwnerId owner, std::optional<uint32_t> clip,
                          const project::FxChain* chain, bool is_song, uint8_t mode, int32_t knob) {
	const auto param = [&](plugin::ParamId id) {
		return KnobAssignment{.target =
		                          ParamAddress{.owner = owner, .space = ParamSpace::ENGINE, .id = id, .clip_id = clip},
		                      .kind = plugin::ParamKind::UNPATCHED_GLOBAL};
	};
	const auto filter = chain != nullptr ? static_cast<FilterView>(chain->filter_type) : FilterView::LPF;
	const auto fx = chain != nullptr ? static_cast<ModFxView>(chain->mod_fx_param) : ModFxView::FEEDBACK;
	const bool upper = knob != 0;
	switch (mode) {
	case 0:
		return param(upper ? ge::kVolume : ge::kPan);
	case 1:
		switch (filter) {
		case FilterView::LPF:
			return param(upper ? ge::kLpfFrequency : ge::kLpfResonance);
		case FilterView::HPF:
			return param(upper ? ge::kHpfFrequency : ge::kHpfResonance);
		default:
			return param(upper ? ge::kTreble : ge::kBass);
		}
	case 3:
		return param(upper ? ge::kDelayRate : ge::kDelayFeedback);
	case 4:
		if (upper) {
			if (!is_song) {
				return param(ge::kSidechainCompressorVolume);
			}
			return {.target = CompArm{"audioCompressor/thresh"}};
		}
		if (is_song && focus.comp.editing) {
			return {.target = CompArm{kCompPaths[focus.comp.param % std::size(kCompPaths)]}};
		}
		return param(ge::kReverbAmount);
	case 5:
		if (upper) {
			return param(ge::kModFXRate);
		}
		return param(fx == ModFxView::DEPTH    ? ge::kModFXDepth
		             : fx == ModFxView::OFFSET ? ge::kModFXOffset
		                                       : ge::kModFXFeedback);
	case 6:
		if (upper) {
			return param(ge::kStutterRate);
		}
		return is_song ? KnobAssignment{} : param(ge::kPitchAdjust);
	case 7:
		return param(upper ? ge::kSampleRateReduction : ge::kBitcrushAmount);
	default: // 2: the envelopes, which a chain has none of
		return {};
	}
}

/// A MIDI track's knob: the CC its record names for the mode's button, in its clip.
KnobAssignment midi_knob(const project::Song& song, const MidiFocus& focus, uint8_t mode, int32_t knob) {
	const project::DocOutput* output = song.output_structure.find(focus.track.output_id);
	if (output == nullptr || !focus.clip) {
		return {};
	}
	const size_t at = static_cast<size_t>(mode) * 2 + static_cast<size_t>(knob);
	if (at >= output->mod_knob_ccs.size()) {
		return {};
	}
	const project::ModKnobCc& cc = output->mod_knob_ccs[at];
	const auto in = [&](ParamSpace space, uint32_t id, plugin::ParamKind kind) {
		return KnobAssignment{.target =
		                          ParamAddress{.owner = OwnerId::clip(focus.clip->clip_id), .space = space, .id = {id}},
		                      .kind = kind};
	};
	switch (cc.kind) {
	case project::ModKnobCc::Kind::BEND:
		return in(ParamSpace::EXPRESSION, 0, plugin::ParamKind::EXPRESSION);
	case project::ModKnobCc::Kind::AFTERTOUCH:
		return in(ParamSpace::EXPRESSION, 2, plugin::ParamKind::EXPRESSION);
	case project::ModKnobCc::Kind::NUMBER:
		if (cc.number == project::ModKnobCc::kYAxisNumber) {
			return in(ParamSpace::EXPRESSION, 1, plugin::ParamKind::EXPRESSION);
		}
		return cc.number < project::ModKnobCc::kYAxisNumber
		           ? in(ParamSpace::MIDI_CC, cc.number, plugin::ParamKind::MIDI)
		           : KnobAssignment{};
	case project::ModKnobCc::Kind::NONE:
		break;
	}
	return {};
}

} // namespace

KnobAssignment knob_assignment(const project::Song& song, const KnobFocus& focus, int32_t knob) {
	if (knob < 0 || knob > 1) {
		return {};
	}
	const uint8_t mode = knob_mode(song, focus);
	if (const auto* sound = std::get_if<SoundFocus>(&focus.owner)) {
		return sound_knob(song, *sound, mode, knob);
	}
	if (std::holds_alternative<SongFocus>(focus.owner)) {
		return chain_knob(focus, OwnerId::song(), std::nullopt, &song.master_effects, true, mode, knob);
	}
	if (const auto* chain = std::get_if<ChainFocus>(&focus.owner)) {
		const ParamAddress owner = *knob_owner(focus);
		return chain_knob(focus, owner.owner, owner.clip_id, song.resolveChain(chain->chain), false, mode, knob);
	}
	if (const auto* midi = std::get_if<MidiFocus>(&focus.owner)) {
		return midi_knob(song, *midi, mode, knob);
	}
	return {};
}

} // namespace deluge::scenes::bind
