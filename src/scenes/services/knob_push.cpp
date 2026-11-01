#include "scenes/services/knob_push.h"

#include "scenes/bind/knob_assignment.h"
#include "scenes/services/source_names.h"

#include "effects/chain.h"
#include "effects/compressor.h"
#include "effects/global_effectable/params.h"
#include "gui/l10n/l10n.h"
#include "gui/l10n/strings.h"
#include "instruments/deluge/cables.h"
#include "instruments/deluge/params.h"
#include "model/mod_controllable/filters/filter_config.h"
#include "model/mod_controllable/mod_fx_names.h"
#include "model/song/reverb_presets.h"
#include "modulation/params/param.h"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <limits>

namespace deluge::scenes::services {

namespace {

using deluge::l10n::String;
using deluge::project::ControllerMapping;
using deluge::project::ModKnobControl;
namespace ge = deluge::global_effectable::param;
namespace ip = deluge::instrument::param;
namespace params = deluge::modulation::params;

/// The song compressor's lower-knob settings, by name, in the compressor view's order.
constexpr std::array<const char*, 5> kCompParamNames{"ratio", "attack", "release", "hpf", "blend"};

const char* text(String s) {
	return deluge::l10n::get(s);
}

/// The song's input tick magnitude, as its transport records it.
int32_t tick_magnitude(const project::Song& song) {
	return song.transport.inside_world_tick_magnitude + song.transport.inside_world_tick_magnitude_offset_from_bpm;
}

const char* filter_type_name(FilterType type) {
	switch (type) {
	case FilterType::LPF:
		return text(String::STRING_FOR_LPF);
	case FilterType::HPF:
		return text(String::STRING_FOR_HPF);
	case FilterType::EQ:
		return text(String::STRING_FOR_EQ);
	default:
		return text(String::STRING_FOR_NONE);
	}
}

const char* lpf_mode_name(effects::FilterMode mode) {
	switch (mode) {
	case effects::FilterMode::TRANSISTOR_12DB:
		return text(String::STRING_FOR_12DB_LADDER);
	case effects::FilterMode::TRANSISTOR_24DB:
		return text(String::STRING_FOR_24DB_LADDER);
	case effects::FilterMode::TRANSISTOR_24DB_DRIVE:
		return text(String::STRING_FOR_DRIVE);
	case effects::FilterMode::SVF_BAND:
		return text(String::STRING_FOR_SVF_BAND);
	case effects::FilterMode::SVF_NOTCH:
		return text(String::STRING_FOR_SVF_NOTCH);
	default:
		return text(String::STRING_FOR_NONE);
	}
}

const char* hpf_mode_name(effects::FilterMode mode) {
	switch (mode) {
	case effects::FilterMode::HPLADDER:
		return text(String::STRING_FOR_HPLADDER);
	case effects::FilterMode::SVF_BAND:
		return text(String::STRING_FOR_SVF_BAND);
	case effects::FilterMode::SVF_NOTCH:
		return text(String::STRING_FOR_SVF_NOTCH);
	default:
		return text(String::STRING_FOR_NONE);
	}
}

const char* filter_mode_name(FilterType type, const effects::EffectStates& fx) {
	switch (type) {
	case FilterType::LPF:
		return lpf_mode_name(fx.filter.lpf_mode);
	case FilterType::HPF:
		return hpf_mode_name(fx.filter.hpf_mode);
	default:
		return text(String::STRING_FOR_NONE);
	}
}

const char* delay_type_name(effects::DelayType type) {
	switch (type) {
	case effects::DelayType::OFF:
		return text(String::STRING_FOR_DELAY_OFF);
	case effects::DelayType::ANALOG:
		return text(String::STRING_FOR_ANALOG_DELAY);
	case effects::DelayType::DIGITAL:
		break;
	}
	return text(String::STRING_FOR_DIGITAL_DELAY);
}

const char* sync_type_name(plugin::SyncType type) {
	switch (type) {
	case plugin::SyncType::TRIPLET:
		return "Triplet";
	case plugin::SyncType::DOTTED:
		return "Dotted";
	default:
		return "Even";
	}
}

const char* ping_pong_name(bool ping_pong) {
	return text(ping_pong ? String::STRING_FOR_ENABLED : String::STRING_FOR_DISABLED);
}

/// The delay's sync level as a note length. Naming it writes nothing (legacy's getter wrapped 256th to off).
std::string delay_sync_level_name(const project::Song& song, const effects::EffectStates& fx) {
	const int32_t level = plugin::index_of(fx.delay.sync_level);
	return note_length_name(uint32_t{3} << (plugin::kMaxSyncLevel - level), tick_magnitude(song));
}

/// The sidechain's sync level the push toggles to, between the song's slow and fast levels.
int32_t next_sidechain_level(const project::Song& song, const effects::EffectStates& fx) {
	const int32_t magnitude = tick_magnitude(song);
	return plugin::index_of(fx.sidechain.sync_level) == 7 - magnitude ? 9 - magnitude : 7 - magnitude;
}

const char* sidechain_name(const project::Song& song, const effects::EffectStates& fx) {
	return text(plugin::index_of(fx.sidechain.sync_level) == 7 - tick_magnitude(song) ? String::STRING_FOR_SLOW
	                                                                                  : String::STRING_FOR_FAST);
}

std::string filter_popup(FilterType type, const effects::EffectStates& fx) {
	std::string out = filter_type_name(type);
	if (type != FilterType::EQ) {
		out += "\n";
		out += filter_mode_name(type, fx);
	}
	return out;
}

std::string delay_popup(const project::Song& song, const effects::EffectStates& fx, bool alt_delay) {
	if (alt_delay) {
		return std::string{"Type: "} + sync_type_name(fx.delay.sync_type)
		       + "\nSync: " + delay_sync_level_name(song, fx);
	}
	return std::string{"Ping pong: "} + ping_pong_name(fx.delay.ping_pong) + "\n" + delay_type_name(fx.delay.type);
}

std::string sidechain_reverb_popup(const project::Song& song, const effects::EffectStates& fx) {
	return std::string{"Sidechain: "} + sidechain_name(song, fx) + "\n"
	       + deluge::model::reverbPresetName(deluge::model::reverbPresetOf(song));
}

std::string comp_reverb_popup(const project::Song& song, const bind::CompView& comp) {
	std::string out = std::string{"Comp Mode: "} + (comp.editing ? "FULL" : "ONE") + "\n";
	if (comp.editing) {
		return out + "Comp Param: " + kCompParamNames[comp.param % kCompParamNames.size()];
	}
	return out + deluge::model::reverbPresetName(deluge::model::reverbPresetOf(song));
}

std::string mod_fx_popup(effects::ModFXType type, ModFXParam param) {
	return std::string{"Type: "} + modfx::modFXToString(type) + "\nParam: " + modfx::getParamName(type, param);
}

/// The chain's record, as the focus names it.
const project::FxChain* chain_of(const project::Song& song, const bind::KnobFocus& focus) {
	if (std::holds_alternative<bind::SongFocus>(focus.owner)) {
		return &song.master_effects;
	}
	if (const auto* chain = std::get_if<bind::ChainFocus>(&focus.owner)) {
		return const_cast<project::Song&>(song).resolveChain(chain->chain);
	}
	return nullptr;
}

/// A sound's knob mappings, as its record holds them.
const std::vector<ControllerMapping>* controls_of(const project::Song& song, const bind::SoundFocus& sound) {
	if (sound.slot.kind == project::SlotAddress::Kind::DRUM) {
		const project::DocDrum* drum = song.drum_structure.find(sound.slot.id);
		return drum != nullptr ? &drum->controls : nullptr;
	}
	const project::DocOutput* output = song.output_structure.find(sound.slot.id);
	return output != nullptr ? &output->controls : nullptr;
}

std::optional<plugin::ParamId> control_target(const std::vector<ControllerMapping>& controls, uint8_t mode,
                                              int32_t knob) {
	const ModKnobControl control{.button = mode, .knob = static_cast<uint8_t>(knob)};
	const auto it = std::ranges::find_if(controls, [&](const ControllerMapping& m) { return m.control == control; });
	return it != controls.end() ? std::optional{it->target} : std::nullopt;
}

/// Whether @p id is a cable from the sidechain alone.
bool sidechain_cable(plugin::ParamId id) {
	if (!instrument::is_cable_depth(id) || instrument::detail::depth_level(id.value) != 1) {
		return false;
	}
	const std::optional<instrument::CableRoute> route = instrument::cable_route(id);
	return route && route->source == instrument::PatchSource::SIDECHAIN;
}

/// Knob @p knob of @p mode's mapping moved from @p from to @p to, with its partner moved from @p partner_from to
/// @p partner_to.
std::vector<ControllerMapping> cycled(std::vector<ControllerMapping> controls, uint8_t mode, int32_t knob,
                                      plugin::ParamId to, plugin::ParamId partner_from, plugin::ParamId partner_to) {
	const ModKnobControl ours{.button = mode, .knob = static_cast<uint8_t>(knob)};
	const ModKnobControl partner{.button = mode, .knob = static_cast<uint8_t>(1 - knob)};
	for (ControllerMapping& mapping : controls) {
		if (mapping.control == ours) {
			mapping.target = to;
		}
		else if (mapping.control == partner && mapping.target == partner_from) {
			mapping.target = partner_to;
		}
	}
	return controls;
}

/// Show @p popup when the mode's button is held, else @p notice.
void say(KnobPush& out, const PushInputs& inputs, std::string popup, std::string notice) {
	if (inputs.button_held) {
		out.popup = std::move(popup);
	}
	else {
		out.notice = std::move(notice);
	}
}

/// The delay knobs' push: the rate's (@p rate) or the feedback's.
void push_delay(KnobPush& out, const project::Song& song, effects::EffectStates fx, bool rate,
                const PushInputs& inputs) {
	std::string notice;
	if (rate && inputs.alt_delay) {
		fx.delay.sync_type = fx.delay.sync_type == plugin::SyncType::TRIPLET  ? plugin::SyncType::DOTTED
		                     : fx.delay.sync_type == plugin::SyncType::DOTTED ? plugin::SyncType::EVEN
		                                                                      : plugin::SyncType::TRIPLET;
		out.effects.push_back(
		    {"delay/syncType", serialization::Value::makeNumber(std::to_underlying(fx.delay.sync_type))});
		notice = sync_type_name(fx.delay.sync_type);
	}
	else if (rate) {
		fx.delay.ping_pong = !fx.delay.ping_pong;
		out.effects.push_back({"delay/pingPong", serialization::Value::makeNumber(fx.delay.ping_pong ? 1 : 0)});
		notice = ping_pong_name(fx.delay.ping_pong);
	}
	else if (inputs.alt_delay) {
		// SYNC_LEVEL_NONE is skipped: the cycle runs 1 to 9.
		const int32_t level = plugin::index_of(fx.delay.sync_level) % plugin::kMaxSyncLevel + 1;
		fx.delay.sync_level = plugin::sync_level_from_index(level);
		out.effects.push_back({"delay/syncLevel", serialization::Value::makeNumber(level)});
		notice = delay_sync_level_name(song, fx);
	}
	else {
		fx.delay.type = effects::next_delay_type(fx.delay.type);
		out.effects.push_back(
		    {"delay/type", serialization::Value::makeScalar(std::string{effects::to_string(fx.delay.type)})});
		notice = delay_type_name(fx.delay.type);
	}
	say(out, inputs, delay_popup(song, fx, inputs.alt_delay), std::move(notice));
	out.edited = true;
}

/// The filter-mode knob's push: the next mode of the low-pass (@p lpf) or the high-pass filter.
void push_filter_mode(KnobPush& out, effects::EffectStates fx, bool lpf, const PushInputs& inputs) {
	const FilterType type = lpf ? FilterType::LPF : FilterType::HPF;
	if (lpf) {
		fx.filter.lpf_mode =
		    static_cast<effects::FilterMode>((std::to_underlying(fx.filter.lpf_mode) + 1) % kNumLPFModes);
		out.effects.push_back(
		    {"lpfMode", serialization::Value::makeScalar(std::string{effects::to_string(fx.filter.lpf_mode)})});
	}
	else {
		fx.filter.hpf_mode = static_cast<effects::FilterMode>(
		    (std::to_underlying(fx.filter.hpf_mode) + 1) % kNumHPFModes + kFirstHPFMode);
		out.effects.push_back(
		    {"hpfMode", serialization::Value::makeScalar(std::string{effects::to_string(fx.filter.hpf_mode)})});
	}
	say(out, inputs, filter_popup(type, fx), filter_mode_name(type, fx));
	out.edited = true;
}

/// The next reverb preset, as two song settings.
void push_reverb(KnobPush& out, const project::Song& song) {
	const size_t next = (deluge::model::reverbPresetOf(song) + 1) % deluge::model::kNumReverbPresets;
	out.song.emplace_back("reverb/roomSize", deluge::model::kReverbPresetRoomSize[next]);
	out.song.emplace_back("reverb/dampening", deluge::model::kReverbPresetDamping[next]);
}

/// The reverb name after @p out's preset change.
const char* pushed_reverb_name(const project::Song& song) {
	return deluge::model::reverbPresetName((deluge::model::reverbPresetOf(song) + 1)
	                                       % deluge::model::kNumReverbPresets);
}

KnobPush push_sound(const project::Song& song, const bind::KnobFocus& focus, const bind::SoundFocus& sound,
                    int32_t knob, bool on, const PushInputs& inputs) {
	KnobPush out;
	const std::vector<ControllerMapping>* controls = controls_of(song, sound);
	const uint8_t mode = bind::knob_mode(song, focus);
	const std::optional<plugin::ParamId> target =
	    controls != nullptr ? control_target(*controls, mode, knob) : std::nullopt;
	if (!target) {
		return out;
	}
	const effects::EffectStates& fx = inputs.effects;
	const plugin::ParamId p = *target;
	if (p == ip::kStutterRate) {
		out.stutter = on;
		return out;
	}
	if (!on) {
		return out;
	}
	if (p == ip::kDelayRate || p == ip::kDelayFeedback) {
		push_delay(out, song, fx, p == ip::kDelayRate, inputs);
	}
	else if (p == ip::kLpfResonance || p == ip::kHpfResonance) {
		push_filter_mode(out, fx, p == ip::kLpfResonance, inputs);
	}
	else if (p == ip::kReverbAmount) {
		push_reverb(out, song);
		say(out, inputs, std::string{"Sidechain: "} + sidechain_name(song, fx) + "\n" + pushed_reverb_name(song),
		    pushed_reverb_name(song));
	}
	else if (sidechain_cable(p)) {
		effects::EffectStates after = fx;
		const int32_t level = next_sidechain_level(song, fx);
		after.sidechain.sync_level = plugin::sync_level_from_index(level);
		out.effects.push_back({"sidechain/syncLevel", serialization::Value::makeNumber(level)});
		say(out, inputs, sidechain_reverb_popup(song, after), sidechain_name(song, after));
		out.edited = true;
	}
	else if ((p == ip::kLpfFrequency || p == ip::kHpfFrequency || p == ip::kTreble) && !inputs.fm) {
		// The filter knob cycles low-pass, high-pass, EQ, and its resonance partner follows.
		const FilterType next = p == ip::kLpfFrequency   ? FilterType::HPF
		                        : p == ip::kHpfFrequency ? FilterType::EQ
		                                                 : FilterType::LPF;
		out.controls = p == ip::kLpfFrequency
		                   ? cycled(*controls, mode, knob, ip::kHpfFrequency, ip::kLpfResonance, ip::kHpfResonance)
		               : p == ip::kHpfFrequency
		                   ? cycled(*controls, mode, knob, ip::kTreble, ip::kHpfResonance, ip::kBass)
		                   : cycled(*controls, mode, knob, ip::kLpfFrequency, ip::kBass, ip::kLpfResonance);
		say(out, inputs, filter_popup(next, fx), filter_type_name(next));
	}
	return out;
}

KnobPush push_chain(const project::Song& song, const bind::KnobFocus& focus, int32_t knob, bool on,
                    const PushInputs& inputs) {
	KnobPush out;
	const bool is_song = std::holds_alternative<bind::SongFocus>(focus.owner);
	const uint8_t mode = bind::knob_mode(song, focus);
	const effects::EffectStates& fx = inputs.effects;
	const project::FxChain* chain = chain_of(song, focus);
	const auto filter = chain != nullptr ? static_cast<FilterType>(chain->filter_type) : FilterType::LPF;
	const auto mod_fx = chain != nullptr ? static_cast<ModFXParam>(chain->mod_fx_param) : ModFXParam::FEEDBACK;

	// A clip's chain trades the song's compressor knobs for the sidechain and the reverb preset.
	if (!is_song && on && mode == 4) {
		if (knob == 1) {
			effects::EffectStates after = fx;
			const int32_t level = next_sidechain_level(song, fx);
			after.sidechain.sync_level = plugin::sync_level_from_index(level);
			out.effects.push_back({"sidechain/syncLevel", serialization::Value::makeNumber(level)});
			say(out, inputs, sidechain_reverb_popup(song, after), sidechain_name(song, after));
		}
		else {
			push_reverb(out, song);
			say(out, inputs, std::string{"Sidechain: "} + sidechain_name(song, fx) + "\n" + pushed_reverb_name(song),
			    pushed_reverb_name(song));
		}
		out.edited = true;
		return out;
	}
	if (mode == 6 && knob == 1) {
		out.stutter = on;
		return out;
	}
	if (!on) {
		return out;
	}
	switch (mode) {
	case 5: {
		effects::ModFXType type = fx.mod_fx.type;
		ModFXParam param = mod_fx;
		if (knob == 1) {
			type = static_cast<effects::ModFXType>((std::to_underlying(type) + 1) % kNumModFXTypes);
			out.effects.push_back(
			    {"modFXType", serialization::Value::makeScalar(std::string{effects::to_string(type)})});
		}
		else {
			param = static_cast<ModFXParam>((std::to_underlying(param) + 1) % kNumModFXParams);
		}
		// A view the type has no param for moves on to one it has.
		const auto valid = [type](ModFXParam candidate) {
			switch (candidate) {
			case ModFXParam::DEPTH:
				return type != effects::ModFXType::FLANGER;
			case ModFXParam::OFFSET:
				return type == effects::ModFXType::CHORUS || type == effects::ModFXType::CHORUS_STEREO
				       || type == effects::ModFXType::GRAIN;
			default:
				return type != effects::ModFXType::CHORUS && type != effects::ModFXType::CHORUS_STEREO;
			}
		};
		for (int32_t tried = 0; tried < kNumModFXParams && !valid(param); tried++) {
			param = static_cast<ModFXParam>((std::to_underlying(param) + 1) % kNumModFXParams);
		}
		if (param != mod_fx) {
			out.chain_view.emplace("modFXCurrentParam", effects::kModFxParamWords[std::to_underlying(param)]);
		}
		say(out, inputs, mod_fx_popup(type, param),
		    knob == 1 ? modfx::modFXToString(type) : modfx::getParamName(type, param));
		out.edited = true;
		return out;
	}
	case 1:
		if (knob == 1) {
			const auto next = static_cast<FilterType>((std::to_underlying(filter) + 1) % kNumFilterTypes);
			out.chain_view.emplace("currentFilterType", effects::kFilterTypeWords[std::to_underlying(next)]);
			say(out, inputs, filter_popup(next, fx), filter_type_name(next));
			out.edited = true;
		}
		else if (filter == FilterType::LPF || filter == FilterType::HPF) {
			push_filter_mode(out, fx, filter == FilterType::LPF, inputs);
		}
		return out;
	case 3:
		push_delay(out, song, fx, knob == 1, inputs);
		return out;
	case 4:
		if (knob == 0 && !focus.comp.editing) {
			push_reverb(out, song);
			say(out, inputs, std::string{"Comp Mode: ONE\n"} + pushed_reverb_name(song), pushed_reverb_name(song));
		}
		else if (knob == 0) {
			bind::CompView comp = focus.comp;
			comp.param = static_cast<uint8_t>((comp.param + 1) % kCompParamNames.size());
			out.comp = comp;
			say(out, inputs, comp_reverb_popup(song, comp), kCompParamNames[comp.param]);
		}
		else {
			bind::CompView comp = focus.comp;
			comp.editing = !comp.editing;
			out.comp = comp;
			say(out, inputs, comp_reverb_popup(song, comp), comp.editing ? "FULL" : "ONE");
		}
		return out;
	default:
		return out;
	}
}

/// A MIDI track's knob CC, as its file names it.
std::string cc_name(const project::DocOutput& output, const project::ModKnobCc& cc) {
	using Kind = project::ModKnobCc::Kind;
	switch (cc.kind) {
	case Kind::NONE:
		return text(String::STRING_FOR_NO_PARAM);
	case Kind::BEND:
		return text(String::STRING_FOR_PITCH_BEND);
	case Kind::AFTERTOUCH:
		return text(String::STRING_FOR_CHANNEL_PRESSURE);
	case Kind::NUMBER:
		break;
	}
	if (cc.number == project::ModKnobCc::kYAxisNumber || cc.number == 1) {
		return text(String::STRING_FOR_MOD_WHEEL);
	}
	if (const auto label = output.cc_labels.find(cc.number);
	    label != output.cc_labels.end() && !label->second.empty()) {
		return label->second;
	}
	return "CC " + std::to_string(cc.number);
}

/// The name a mod button shows for knob @p knob: its param's, as the filter modes name morph.
std::string knob_name(const project::Song& song, const bind::KnobFocus& focus, int32_t knob,
                      const effects::EffectStates& fx) {
	const bind::KnobAssignment assignment = bind::knob_assignment(song, focus, knob);
	const auto* address = std::get_if<project::ParamAddress>(&assignment.target);
	if (address == nullptr) {
		return {};
	}
	const plugin::ParamId id = address->id;
	if (instrument::is_cable_depth(id)) {
		const int level = instrument::detail::depth_level(id.value);
		const auto first = static_cast<PatchSource>(instrument::detail::source_field(id.value, 0) - 1);
		const auto second = level > 1
		                        ? static_cast<PatchSource>(instrument::detail::source_field(id.value, level - 1) - 1)
		                        : PatchSource::NONE;
		const std::optional<instrument::RuntimeSlot> destination =
		    instrument::runtime_slot(plugin::ParamId{id.value & 0xFFu});
		std::string out = second == PatchSource::NONE ? std::string{text(source_display_string(first))}
		                                              : std::string{source_short_name(first)};
		out += " -> ";
		if (second != PatchSource::NONE) {
			out += source_short_name(second);
			out += " -> ";
		}
		out += params::getPatchedParamShortName(static_cast<params::ParamType>(destination ? destination->index : 0),
		                                        fx.filter.lpf_mode, fx.filter.hpf_mode);
		return out;
	}
	if (assignment.kind == plugin::ParamKind::UNPATCHED_GLOBAL) {
		const std::optional<uint8_t> index = global_effectable::runtime_index(id);
		return index ? params::getParamDisplayName(params::Kind::UNPATCHED_GLOBAL, *index, fx.filter.lpf_mode,
		                                           fx.filter.hpf_mode)
		             : std::string{};
	}
	const std::optional<instrument::RuntimeSlot> slot = instrument::runtime_slot(id);
	if (!slot) {
		return {};
	}
	const params::Kind kind =
	    slot->set == instrument::RuntimeSet::PATCHED ? params::Kind::PATCHED : params::Kind::UNPATCHED_SOUND;
	return params::getParamDisplayName(kind, slot->index, fx.filter.lpf_mode, fx.filter.hpf_mode);
}

/// Both knobs' names, the upper first; the stutter button's lower knob has none on the song.
std::string names_popup(const project::Song& song, const bind::KnobFocus& focus, int32_t button,
                        const PushInputs& inputs) {
	std::string out = knob_name(song, focus, 1, inputs.effects);
	if (!(button == 6 && !inputs.clip_context)) {
		out += "\n";
		out += knob_name(song, focus, 0, inputs.effects);
	}
	return out;
}

} // namespace

std::optional<EffectOwner> effect_owner(const bind::KnobFocus& focus) {
	if (std::holds_alternative<bind::SongFocus>(focus.owner)) {
		return project::ChainAddress{.kind = project::ChainAddress::Kind::SONG};
	}
	if (const auto* sound = std::get_if<bind::SoundFocus>(&focus.owner)) {
		return sound->slot;
	}
	if (const auto* chain = std::get_if<bind::ChainFocus>(&focus.owner)) {
		return chain->chain;
	}
	return std::nullopt;
}

KnobPush knob_push(const project::Song& song, const bind::KnobFocus& focus, int32_t knob, bool on,
                   const PushInputs& inputs) {
	if (const auto* sound = std::get_if<bind::SoundFocus>(&focus.owner)) {
		return push_sound(song, focus, *sound, knob, on, inputs);
	}
	if (std::holds_alternative<bind::SongFocus>(focus.owner) || std::holds_alternative<bind::ChainFocus>(focus.owner)) {
		return push_chain(song, focus, knob, on, inputs);
	}
	if (std::holds_alternative<bind::MidiFocus>(focus.owner)) {
		KnobPush out;
		out.midi_cc_select = true;
		out.edited = on;
		return out;
	}
	return {};
}

std::string mod_button_popup(const project::Song& song, const bind::KnobFocus& focus, int32_t button,
                             const PushInputs& inputs) {
	const effects::EffectStates& fx = inputs.effects;
	if (const auto* sound = std::get_if<bind::SoundFocus>(&focus.owner)) {
		const std::vector<ControllerMapping>* controls = controls_of(song, *sound);
		const uint8_t mode = bind::knob_mode(song, focus);
		const std::optional<plugin::ParamId> top =
		    controls != nullptr ? control_target(*controls, mode, 1) : std::nullopt;
		const std::optional<plugin::ParamId> bottom =
		    controls != nullptr ? control_target(*controls, mode, 0) : std::nullopt;
		const auto pair = [&](plugin::ParamId upper, plugin::ParamId lower) { return top == upper && bottom == lower; };
		if (pair(ip::kLpfFrequency, ip::kLpfResonance)) {
			return filter_popup(FilterType::LPF, fx);
		}
		if (pair(ip::kHpfFrequency, ip::kHpfResonance)) {
			return filter_popup(FilterType::HPF, fx);
		}
		if (pair(ip::kTreble, ip::kBass)) {
			return filter_popup(FilterType::EQ, fx);
		}
		if (pair(ip::kDelayRate, ip::kDelayFeedback)) {
			return delay_popup(song, fx, inputs.alt_delay);
		}
		if (top && sidechain_cable(*top) && bottom == ip::kReverbAmount) {
			return sidechain_reverb_popup(song, fx);
		}
		return names_popup(song, focus, button, inputs);
	}
	if (const auto* midi = std::get_if<bind::MidiFocus>(&focus.owner)) {
		const project::DocOutput* output = song.output_structure.find(midi->track.output_id);
		const size_t at = static_cast<size_t>(bind::knob_mode(song, focus)) * 2;
		if (output == nullptr || at + 1 >= output->mod_knob_ccs.size()) {
			return {};
		}
		return cc_name(*output, output->mod_knob_ccs[at + 1]) + "\n" + cc_name(*output, output->mod_knob_ccs[at]);
	}
	const bool is_song = std::holds_alternative<bind::SongFocus>(focus.owner);
	if (!is_song && !std::holds_alternative<bind::ChainFocus>(focus.owner)) {
		return {};
	}
	const project::FxChain* chain = chain_of(song, focus);
	switch (button) {
	case 1:
		return filter_popup(chain != nullptr ? static_cast<FilterType>(chain->filter_type) : FilterType::LPF, fx);
	case 2:
		return {};
	case 3:
		return delay_popup(song, fx, inputs.alt_delay);
	case 4:
		return is_song ? comp_reverb_popup(song, focus.comp) : sidechain_reverb_popup(song, fx);
	case 5:
		return mod_fx_popup(fx.mod_fx.type,
		                    chain != nullptr ? static_cast<ModFXParam>(chain->mod_fx_param) : ModFXParam::FEEDBACK);
	default:
		return names_popup(song, focus, button, inputs);
	}
}

namespace {

/// The compressor setting's stored value, by its path.
int32_t comp_value(std::string_view path, const effects::EffectStates& fx) {
	const effects::CompressorState& c = fx.compressor;
	return path == "audioCompressor/thresh"    ? c.threshold
	       : path == "audioCompressor/ratio"   ? c.ratio
	       : path == "audioCompressor/attack"  ? c.attack
	       : path == "audioCompressor/release" ? c.release
	       : path == "audioCompressor/compHPF" ? c.sidechain_hpf
	                                           : c.blend;
}

} // namespace

int32_t comp_knob_pos(const bind::CompArm& arm, const effects::EffectStates& effects) {
	return (comp_value(arm.path, effects) >> 24) - 64;
}

CompTurn comp_turn(const bind::CompArm& arm, const effects::EffectStates& effects, int32_t offset) {
	const int32_t knob = std::clamp(comp_knob_pos(arm, effects) + offset, -64, 64);
	// The top of the knob's travel saturates to the whole Q31 range.
	const int32_t stored = knob == 64 ? std::numeric_limits<int32_t>::max() : (knob + 64) << 24;
	CompTurn out{.path = arm.path, .value = serialization::Value::makeNumber(stored)};
	int32_t shown = 0;
	const char* unit = "";
	if (arm.path == "audioCompressor/thresh") {
		out.title = text(String::STRING_FOR_THRESHOLD);
		shown = (64 + knob) * kMaxMenuValue / 128;
	}
	else if (arm.path == "audioCompressor/ratio") {
		out.title = text(String::STRING_FOR_RATIO);
		shown = static_cast<int32_t>(dsp::fixed::compressor_ratio(stored));
		unit = " : 1";
	}
	else if (arm.path == "audioCompressor/attack") {
		out.title = text(String::STRING_FOR_ATTACK);
		shown = static_cast<int32_t>(dsp::fixed::compressor_attack_ms(stored));
		unit = " MS";
	}
	else if (arm.path == "audioCompressor/release") {
		out.title = text(String::STRING_FOR_RELEASE);
		shown = static_cast<int32_t>(dsp::fixed::compressor_release_ms(stored));
		unit = " MS";
	}
	else if (arm.path == "audioCompressor/compHPF") {
		out.title = text(String::STRING_FOR_HPF);
		shown = static_cast<int32_t>(dsp::fixed::compressor_sidechain_hz(stored));
		unit = " HZ";
	}
	else {
		out.title = text(String::STRING_FOR_BLEND);
		shown = dsp::fixed::compressor_blend_percent(stored);
		unit = " %";
	}
	out.value_text = std::to_string(shown) + unit;
	return out;
}

bool apply_push(bind::WriteSink& sink, const bind::KnobFocus& focus, const KnobPush& push) {
	bool taken = true;
	const std::optional<EffectOwner> owner = effect_owner(focus);
	for (const EffectWrite& write : push.effects) {
		taken &= owner && sink.write_effect(*owner, write.path, write.value);
	}
	for (const auto& [path, menu_value] : push.song) {
		const bind::Target target{
		    .kind = bind::Target::Kind::MODEL, .path = path, .group = project::SettingGroupId::SONG};
		taken &= sink.write(bind::WriteRequest{.target = target, .menu_value = menu_value});
	}
	if (push.chain_view) {
		const auto* chain = owner ? std::get_if<project::ChainAddress>(&*owner) : nullptr;
		taken &= chain != nullptr && sink.chain_view(*chain, push.chain_view->first, push.chain_view->second);
	}
	if (push.controls) {
		const auto* sound = std::get_if<bind::SoundFocus>(&focus.owner);
		taken &=
		    sound != nullptr
		    && sink.write_controls(project::ControlsAddress{.kind = sound->slot.kind == project::SlotAddress::Kind::DRUM
		                                                                ? project::ControlsAddress::Kind::DRUM
		                                                                : project::ControlsAddress::Kind::OUTPUT,
		                                                    .id = sound->slot.id},
		                           *push.controls);
	}
	if (push.stutter) {
		taken &= sink.stutter(focus, *push.stutter);
	}
	return taken;
}

std::string note_length_name(uint32_t note_length, int32_t tick_magnitude) {
	// The note's magnitude: positive is bars, negative a division of a bar.
	int32_t magnitude = -5 - tick_magnitude;
	for (uint32_t level = 3; level < note_length; level <<= 1) {
		magnitude++;
	}
	if (magnitude < 0) {
		const uint32_t division = uint32_t{1} << -magnitude;
		return std::to_string(division) + ((division % 10) == 2 ? "nd" : "th") + "-notes";
	}
	return std::to_string(uint32_t{1} << magnitude) + "-bar";
}

} // namespace deluge::scenes::services
