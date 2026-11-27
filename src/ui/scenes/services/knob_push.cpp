#include "ui/scenes/services/knob_push.h"

#include "ui/scenes/bind/knob_assignment.h"
#include "ui/scenes/bind/page_sources.h"
#include "ui/scenes/bind/ports/ports.h"
#include "ui/scenes/services/source_names.h"

#include "effects/chain.h"
#include "effects/compressor.h"
#include "effects/reverb_presets.h"
#include "effects/setting_params.h"
#include "effects/words.h"
#include "gui/l10n/effect_names.h"
#include "gui/l10n/l10n.h"
#include "gui/l10n/param_names.h"
#include "gui/l10n/strings.h"
#include "host/chains/pages.h"
#include "host/chains/params.h"
#include "instruments/classic/cables.h"
#include "instruments/classic/pages.h"
#include "instruments/classic/params.h"
#include "instruments/classic/state_access.h"
#include "music/note_length.h"
#include "plugin/types/display_mapping.h"
#include "ui/controls/pages.h"
#include "ui/controls/resolve.h"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <limits>
#include <utility>

namespace deluge::ui::scenes::services {

namespace {

using deluge::l10n::String;
using deluge::project::ControllerMapping;
namespace ip = deluge::instruments::classic::param;

/// The song compressor's lower-knob settings, by name, in the compressor view's order.
constexpr std::array<const char*, 5> kCompParamNames{"ratio", "attack", "release", "hpf", "blend"};

/// What a push reads beyond the song's records: the owner's effects (through the gesture's track and sound ports), the
/// device facts (through its controls port) and the gesture's context.
struct Reads {
	effects::EffectStates effects; ///< the owner's effect settings (a new chain's for none)
	/// the sound's envelopes' modes; all ADSR for no sound
	l10n::EnvelopeModes envelopes{};
	ui::controls::PageSources pages; ///< the publishers and the device facts (the alternative delay knobs)
	bool button_held{false};         ///< the mode's mod button is held
	bool clip_context{false};        ///< the knobs are on a clip, not the song
};

/// What a push on @p focus reads, in @p gesture's context.
Reads reads_of(const bind::KnobFocus& focus, const PushInputs& gesture) {
	const ui::controls::Facts* const facts = bind::port_of(gesture.ports, &bind::Ports::controls);
	Reads out{.pages = facts != nullptr ? bind::app_page_sources(*facts, gesture.board)
	                                    : bind::app_turn_sources(gesture.board),
	          .button_held = gesture.button_held,
	          .clip_context = gesture.clip_context};
	const bind::TrackFacts* const tracks = bind::port_of(gesture.ports, &bind::Ports::tracks);
	const bind::SoundFacts* const sound = bind::port_of(gesture.ports, &bind::Ports::sound);
	if (tracks != nullptr && sound != nullptr) {
		if (const effects::EffectStates* fx = owner_effects(focus, *tracks, *sound)) {
			out.effects = *fx;
		}
	}
	if (sound != nullptr) {
		out.envelopes = owner_envelope_modes(focus, *sound).value_or(l10n::EnvelopeModes{});
	}
	return out;
}

const char* text(String s) {
	return deluge::l10n::get(s);
}

/// The song's input tick magnitude, as its transport records it.
int32_t tick_magnitude(const project::Song& song) {
	return song.transport.input_tick_magnitude();
}

const char* filter_type_name(effects::FilterType type) {
	switch (type) {
	case effects::FilterType::LPF:
		return text(String::STRING_FOR_LPF);
	case effects::FilterType::HPF:
		return text(String::STRING_FOR_HPF);
	case effects::FilterType::EQ:
		return text(String::STRING_FOR_EQ);
	}
	return text(String::STRING_FOR_NONE);
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

const char* filter_mode_name(effects::FilterType type, const effects::EffectStates& fx) {
	switch (type) {
	case effects::FilterType::LPF:
		return lpf_mode_name(fx.filter.lpf_mode);
	case effects::FilterType::HPF:
		return hpf_mode_name(fx.filter.hpf_mode);
	case effects::FilterType::EQ:
		break;
	}
	return text(String::STRING_FOR_NONE);
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

const char* sidechain_name(const project::Song& song, const effects::EffectStates& fx) {
	return text(plugin::index_of(fx.sidechain.sync_level) == 7 - tick_magnitude(song) ? String::STRING_FOR_SLOW
	                                                                                  : String::STRING_FOR_FAST);
}

std::string filter_popup(effects::FilterType type, const effects::EffectStates& fx) {
	std::string out = filter_type_name(type);
	if (type != effects::FilterType::EQ) {
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

/// The preset nearest @p song's reverb.
size_t current_reverb_preset(const project::Song& song) {
	return effects::nearest_reverb_preset_of(song.reverb.room_size, song.reverb.damping);
}

/// The preset after the one nearest @p song's reverb.
size_t next_reverb_preset(const project::Song& song) {
	return (current_reverb_preset(song) + 1) % effects::kReverbPresets.size();
}

/// Reverb preset @p preset's popup name.
std::string reverb_name(size_t preset) {
	return std::string{l10n::reverb_preset_name(preset)};
}

std::string sidechain_reverb_popup(const project::Song& song, const effects::EffectStates& fx) {
	return std::string{"Sidechain: "} + sidechain_name(song, fx) + "\n" + reverb_name(current_reverb_preset(song));
}

std::string comp_reverb_popup(const project::Song& song, const bind::CompView& comp) {
	std::string out = std::string{"Comp Mode: "} + (comp.editing ? "FULL" : "ONE") + "\n";
	if (comp.editing) {
		return out + "Comp Param: " + kCompParamNames[comp.param % kCompParamNames.size()];
	}
	return out + reverb_name(current_reverb_preset(song));
}

std::string mod_fx_popup(effects::ModFXType type, effects::ModFxParam param) {
	return std::string{"Type: "} + std::string{l10n::mod_fx_name(type)}
	       + "\nParam: " + std::string{l10n::mod_fx_param_name(type, param, false)};
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

/// Show @p popup when the mode's button is held, else @p notice.
void say(KnobPush& out, const Reads& inputs, std::string popup, std::string notice) {
	if (inputs.button_held) {
		out.popup = std::move(popup);
	}
	else {
		out.notice = std::move(notice);
	}
}

/// The next reverb preset, as two song settings.
void push_reverb(KnobPush& out, const project::Song& song) {
	const size_t next = next_reverb_preset(song);
	out.song.emplace_back("reverb/roomSize", effects::kReverbPresets[next].room_size);
	out.song.emplace_back("reverb/dampening", effects::kReverbPresets[next].damping);
}

/// The reverb name after @p out's preset change.
std::string pushed_reverb_name(const project::Song& song) {
	return reverb_name(next_reverb_preset(song));
}

/// The text a step of setting @p id shows, from the effects @p after it.
void say_step(KnobPush& out, const project::Song& song, plugin::ParamId id, const effects::EffectStates& after,
              const Reads& inputs) {
	namespace setting = effects::setting;
	if (id == setting::kDelaySyncType || id == setting::kDelayPingPong || id == setting::kDelaySyncLevel
	    || id == setting::kDelayType) {
		const std::string notice = id == setting::kDelaySyncType    ? std::string{sync_type_name(after.delay.sync_type)}
		                           : id == setting::kDelayPingPong  ? std::string{ping_pong_name(after.delay.ping_pong)}
		                           : id == setting::kDelaySyncLevel ? delay_sync_level_name(song, after)
		                                                            : std::string{delay_type_name(after.delay.type)};
		say(out, inputs, delay_popup(song, after, inputs.pages.alt_delay), notice);
	}
	else if (id == setting::kLpfMode || id == setting::kHpfMode) {
		const effects::FilterType type = id == setting::kLpfMode ? effects::FilterType::LPF : effects::FilterType::HPF;
		say(out, inputs, filter_popup(type, after), filter_mode_name(type, after));
	}
	else if (id == setting::kSidechainSyncLevel) {
		say(out, inputs, sidechain_reverb_popup(song, after), sidechain_name(song, after));
	}
}

/// The chain's mod FX view, as its record holds it; feedback for no chain.
uint32_t mod_fx_view(const project::Song& song, const bind::KnobFocus& focus) {
	const project::FxChain* chain = chain_of(song, focus);
	return chain != nullptr ? chain->mod_fx_param : std::to_underlying(effects::ModFxParam::FEEDBACK);
}

/// After the mod FX type stepped to @p after's: a view the new type has no param for moves on to the next one it
/// has, and the text names the type.
void fix_mod_fx_view(KnobPush& out, const project::Song& song, const bind::KnobFocus& focus,
                     const effects::EffectStates& after, const Reads& inputs) {
	const effects::ModFXType type = after.mod_fx.type;
	const uint32_t view = mod_fx_view(song, focus);
	uint32_t param = view;
	for (int32_t tried = 0; tried < effects::kNumModFxParams && !host::chains::mod_fx_page_valid(type, param);
	     tried++) {
		param = (param + 1) % effects::kNumModFxParams;
	}
	if (param != view) {
		out.chain_view.emplace("modFXCurrentParam", effects::kModFxParamWords[param]);
	}
	say(out, inputs, mod_fx_popup(type, static_cast<effects::ModFxParam>(param)), std::string{l10n::mod_fx_name(type)});
}

/// A press of knob @p pressed that moved @p focus's section from @p pages's current page to @p page: a sound's filter
/// knob to its next filter (its knobs, rewritten), a chain's filter or mod FX view, or the song's compressor view.
void select_page_push(KnobPush& out, const project::Song& song, const bind::KnobFocus& focus,
                      const ui::controls::SectionPages& pages, const ui::controls::SelectPage& page, int32_t pressed,
                      const Reads& inputs) {
	const effects::EffectStates& fx = inputs.effects;
	if (const auto* sound = std::get_if<bind::SoundFocus>(&focus.owner)) {
		const std::vector<ControllerMapping>* mappings = controls_of(song, *sound);
		if (mappings == nullptr) {
			return;
		}
		// The pressed knob takes its new param, every mapping of it (the engine holds one param per knob); each other
		// knob the page moves takes its new param where it held the old one, and a knob mapped elsewhere stays.
		std::vector<ControllerMapping> after = *mappings;
		for (ControllerMapping& mapping : after) {
			const size_t knob = mapping.control.knob;
			if (mapping.control.button != page.section || knob >= page.slots.size()) {
				continue;
			}
			const std::optional<project::ParamTarget>& from = pages.current.slots[knob].target;
			const std::optional<project::ParamTarget>& to = page.slots[knob].target;
			if (to && (std::cmp_equal(knob, pressed) || (from && mapping.target == *from))) {
				mapping.target = *to;
			}
		}
		out.controls = std::move(after);
		// The filter cycle's pages are low-pass, high-pass and EQ, on either knob.
		const auto next = static_cast<effects::FilterType>(page.page_id % effects::kNumFilterTypes);
		say(out, inputs, filter_popup(next, fx), filter_type_name(next));
		return;
	}
	switch (page.section) {
	case host::chains::kFilterSection: {
		const auto next = static_cast<effects::FilterType>(page.page_id);
		out.chain_view.emplace("currentFilterType", effects::kFilterTypeWords[page.page_id]);
		say(out, inputs, filter_popup(next, fx), filter_type_name(next));
		out.edited = true;
		return;
	}
	case host::chains::kModFxSection: {
		const auto param = static_cast<effects::ModFxParam>(page.page_id);
		if (page.page_id != mod_fx_view(song, focus)) {
			out.chain_view.emplace("modFXCurrentParam", effects::kModFxParamWords[page.page_id]);
		}
		say(out, inputs, mod_fx_popup(fx.mod_fx.type, param),
		    std::string{l10n::mod_fx_param_name(fx.mod_fx.type, param, false)});
		out.edited = true;
		return;
	}
	case host::chains::kCompressorSection: {
		// The song's compressor: its group 1 pages are its settings, one each after the reverb page.
		const auto found =
		    std::ranges::find_if(pages.cycle, [&](const ui::controls::Page& p) { return p.page_id == page.page_id; });
		bind::CompView comp = focus.comp;
		comp.editing = found != pages.cycle.end() && found->group == 1;
		if (comp.editing) {
			comp.param = static_cast<uint8_t>(page.page_id - 1);
		}
		out.comp = comp;
		say(out, inputs, comp_reverb_popup(song, comp),
		    comp.editing != focus.comp.editing ? (comp.editing ? "FULL" : "ONE") : kCompParamNames[comp.param]);
		return;
	}
	default:
		return;
	}
}

/// A press the host carries out: a stutter, a MIDI knob's CC select, or the next reverb preset.
void host_command_push(KnobPush& out, const project::Song& song, const bind::KnobFocus& focus,
                       ui::controls::HostCommand command, const Reads& inputs) {
	using ui::controls::HostCommand;
	switch (command) {
	case HostCommand::STUTTER_BEGIN:
	case HostCommand::STUTTER_END:
		out.stutter = command == HostCommand::STUTTER_BEGIN;
		return;
	case HostCommand::MIDI_CC_SELECT_ON:
	case HostCommand::MIDI_CC_SELECT_OFF:
		out.midi_cc_select = true;
		out.edited = command == HostCommand::MIDI_CC_SELECT_ON;
		return;
	case HostCommand::REVERB_PRESET:
		push_reverb(out, song);
		if (std::holds_alternative<bind::SongFocus>(focus.owner)) {
			say(out, inputs, std::string{"Comp Mode: ONE\n"} + pushed_reverb_name(song), pushed_reverb_name(song));
			return;
		}
		say(out, inputs,
		    std::string{"Sidechain: "} + sidechain_name(song, inputs.effects) + "\n" + pushed_reverb_name(song),
		    pushed_reverb_name(song));
		// A clip's chain marks its instrument edited; a sound's reverb does not.
		out.edited = std::holds_alternative<bind::ChainFocus>(focus.owner);
		return;
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
	// CC 1 is named the mod wheel, as `MIDIInstrument::appendCCName` names `CC_EXTERNAL_MOD_WHEEL`, while its lane is
	// CC 1's own (`HostEditSink::midi_cc_select`'s lane, as legacy's `getParamToControlFromInputMIDIChannel`). No
	// record holds a 1: the codec reads a file's 1 as the Y axis, and the CC choice steps over it.
	if (cc.number == project::ModKnobCc::kYAxisNumber || cc.number == 1) {
		return text(String::STRING_FOR_MOD_WHEEL);
	}
	if (const auto label = output.cc_labels.find(cc.number);
	    label != output.cc_labels.end() && !label->second.empty()) {
		return label->second;
	}
	return "CC " + std::to_string(cc.number);
}

/// The name a mod button shows for knob @p knob: its param's, as the filter modes name morph and the envelopes' modes
/// their params.
std::string knob_name(const project::Song& song, const bind::KnobFocus& focus, int32_t knob, const Reads& inputs) {
	const effects::EffectStates& fx = inputs.effects;
	const ui::controls::Board& board = inputs.pages.board;
	const bind::KnobAssignment assignment = bind::knob_assignment(song, focus, knob, board);
	const auto* address = std::get_if<project::ParamAddress>(&assignment.target);
	if (address == nullptr) {
		return {};
	}
	const plugin::ParamId id = address->id;
	if (address->space == project::ParamSpace::ARP) {
		return std::string{l10n::arp_param_name(id)};
	}
	if (instruments::classic::is_cable_depth(id)) {
		const int level = instruments::classic::detail::depth_level(id.value);
		const auto first = static_cast<PatchSource>(instruments::classic::detail::source_field(id.value, 0) - 1);
		const auto second =
		    level > 1 ? static_cast<PatchSource>(instruments::classic::detail::source_field(id.value, level - 1) - 1)
		              : PatchSource::NONE;
		const std::optional<instruments::classic::RuntimeSlot> destination =
		    instruments::classic::runtime_slot(plugin::ParamId{id.value & 0xFFu});
		std::string out = second == PatchSource::NONE ? std::string{text(source_display_string(first))}
		                                              : std::string{source_short_name(first)};
		out += " -> ";
		if (second != PatchSource::NONE) {
			out += source_short_name(second);
			out += " -> ";
		}
		const uint32_t index = destination ? destination->index : 0;
		out += l10n::envelope_param_name(plugin::ParamId{index}, inputs.envelopes, /*brief=*/true)
		           .value_or(
		               l10n::patched_param_short_name(plugin::ParamId{index}, fx.filter.lpf_mode, fx.filter.hpf_mode));
		return out;
	}
	if (assignment.plugin == project::OwnerPlugin::GLOBAL_EFFECTABLE) {
		return host::chains::kParamBlock.contains(id)
		           ? std::string{l10n::param_name(assignment.plugin, id, fx.filter.lpf_mode, fx.filter.hpf_mode)}
		           : std::string{};
	}
	if (!instruments::classic::runtime_slot(id)) {
		return {};
	}
	return std::string{l10n::envelope_param_name(id, inputs.envelopes)
	                       .value_or(l10n::param_name(assignment.plugin, id, fx.filter.lpf_mode, fx.filter.hpf_mode))};
}

/// Both knobs' names, the upper first; the stutter button's lower knob has none on the song.
std::string names_popup(const project::Song& song, const bind::KnobFocus& focus, int32_t button, const Reads& inputs) {
	std::string out = knob_name(song, focus, 1, inputs);
	if (!(std::cmp_equal(button, host::chains::kStutterSection) && !inputs.clip_context)) {
		out += "\n";
		out += knob_name(song, focus, 0, inputs);
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

const effects::EffectStates* owner_effects(const bind::KnobFocus& focus, const bind::TrackFacts& tracks,
                                           const bind::SoundFacts& sound) {
	const std::optional<EffectOwner> owner = effect_owner(focus);
	if (!owner) {
		return nullptr;
	}
	if (const auto* slot = std::get_if<project::SlotAddress>(&*owner)) {
		const instruments::classic::SoundState* state = sound.sound_state(*slot);
		return state != nullptr ? &instruments::classic::sound_effects(*state) : nullptr;
	}
	return tracks.chain_effects(std::get<project::ChainAddress>(*owner));
}

std::optional<l10n::EnvelopeModes> owner_envelope_modes(const bind::KnobFocus& focus, const bind::SoundFacts& sound) {
	const auto* focused = std::get_if<bind::SoundFocus>(&focus.owner);
	const instruments::classic::SoundState* state = focused != nullptr ? sound.sound_state(focused->slot) : nullptr;
	if (state == nullptr) {
		return std::nullopt;
	}
	l10n::EnvelopeModes modes{};
	for (size_t e = 0; e < modes.size(); ++e) {
		modes[e] = instruments::classic::envelope(*state, e).mode;
	}
	return modes;
}

KnobPush knob_push(const project::Song& song, const bind::KnobFocus& focus, int32_t knob, bool on,
                   const PushInputs& gesture) {
	const Reads inputs = reads_of(focus, gesture);
	const ui::controls::SectionPages pages = ui::controls::section_pages(song, focus, inputs.pages);
	const ui::controls::Target target = ui::controls::resolve_press(focus, pages, knob, on);
	KnobPush out;
	if (const auto* step = std::get_if<ui::controls::StepSetting>(&target)) {
		const plugin::ParamId id = step->address.id;
		const std::optional<effects::Stepped> stepped =
		    effects::step_setting(id, inputs.effects, {.tick_magnitude = tick_magnitude(song)});
		const plugin::ParamInfo* setting = effects::find_setting(id);
		if (!stepped || setting == nullptr) {
			return out;
		}
		out.effects.push_back({setting->file_name, stepped->value});
		if (id == effects::setting::kModFXType) {
			fix_mod_fx_view(out, song, focus, stepped->after, inputs);
		}
		else {
			say_step(out, song, id, stepped->after, inputs);
		}
		out.edited = true;
	}
	else if (const auto* page = std::get_if<ui::controls::SelectPage>(&target)) {
		select_page_push(out, song, focus, pages, *page, knob, inputs);
	}
	else if (const auto* command = std::get_if<ui::controls::HostCommand>(&target)) {
		host_command_push(out, song, focus, *command, inputs);
	}
	return out;
}

std::string mod_button_popup(const project::Song& song, const bind::KnobFocus& focus, int32_t button,
                             const PushInputs& gesture) {
	const Reads inputs = reads_of(focus, gesture);
	const effects::EffectStates& fx = inputs.effects;
	if (const auto* sound = std::get_if<bind::SoundFocus>(&focus.owner)) {
		const ui::controls::Page current = ui::controls::current_page(song, focus, inputs.pages);
		// The engine param a knob is on: the pairs below are the instrument's
		const auto param_at = [&current](size_t knob) -> std::optional<plugin::ParamId> {
			const std::optional<project::ParamTarget>& target = current.slots[knob].target;
			return target && target->space == project::ParamSpace::ENGINE ? std::optional{target->id} : std::nullopt;
		};
		const std::optional<plugin::ParamId> top = param_at(1);
		const std::optional<plugin::ParamId> bottom = param_at(0);
		const auto pair = [&](plugin::ParamId upper, plugin::ParamId lower) { return top == upper && bottom == lower; };
		if (pair(ip::kLpfFrequency, ip::kLpfResonance)) {
			return filter_popup(effects::FilterType::LPF, fx);
		}
		if (pair(ip::kHpfFrequency, ip::kHpfResonance)) {
			return filter_popup(effects::FilterType::HPF, fx);
		}
		if (pair(ip::kTreble, ip::kBass)) {
			return filter_popup(effects::FilterType::EQ, fx);
		}
		if (pair(ip::kDelayRate, ip::kDelayFeedback)) {
			return delay_popup(song, fx, inputs.pages.alt_delay);
		}
		if (top && instruments::classic::is_sidechain_cable(*top) && bottom == ip::kReverbAmount) {
			return sidechain_reverb_popup(song, fx);
		}
		return names_popup(song, focus, button, inputs);
	}
	if (const auto* midi = std::get_if<bind::MidiFocus>(&focus.owner)) {
		const project::DocOutput* output = song.output_structure.find(midi->track.output_id);
		const size_t at =
		    static_cast<size_t>(bind::knob_mode(song, focus, inputs.pages.board)) * inputs.pages.board.slots;
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
	case host::chains::kFilterSection:
		return filter_popup(
		    chain != nullptr ? static_cast<effects::FilterType>(chain->filter_type) : effects::FilterType::LPF, fx);
	case 2: // the envelopes, which a chain has none of
		return {};
	case 3: // the delay
		return delay_popup(song, fx, inputs.pages.alt_delay);
	case host::chains::kCompressorSection:
		return is_song ? comp_reverb_popup(song, focus.comp) : sidechain_reverb_popup(song, fx);
	case host::chains::kModFxSection:
		return mod_fx_popup(fx.mod_fx.type, chain != nullptr ? static_cast<effects::ModFxParam>(chain->mod_fx_param)
		                                                     : effects::ModFxParam::FEEDBACK);
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
	CompTurn out{.path = arm.path, .value = serialization::Value::makeNumber(stored), .knob_pos = knob};
	int32_t shown = 0;
	const char* unit = "";
	if (arm.path == "audioCompressor/thresh") {
		out.title = text(String::STRING_FOR_THRESHOLD);
		shown = (64 + knob) * plugin::kMaxMenuSteps / 128;
	}
	else if (arm.path == "audioCompressor/ratio") {
		out.title = text(String::STRING_FOR_RATIO);
		shown = static_cast<int32_t>(dsp::fixed::compressor_ratio(dsp::Q<31>::from_raw(stored)));
		unit = " : 1";
	}
	else if (arm.path == "audioCompressor/attack") {
		out.title = text(String::STRING_FOR_ATTACK);
		shown = static_cast<int32_t>(dsp::fixed::compressor_attack_ms(dsp::Q<31>::from_raw(stored)));
		unit = " MS";
	}
	else if (arm.path == "audioCompressor/release") {
		out.title = text(String::STRING_FOR_RELEASE);
		shown = static_cast<int32_t>(dsp::fixed::compressor_release_ms(dsp::Q<31>::from_raw(stored)));
		unit = " MS";
	}
	else if (arm.path == "audioCompressor/compHPF") {
		out.title = text(String::STRING_FOR_HPF);
		shown = static_cast<int32_t>(dsp::fixed::compressor_sidechain_hz(dsp::Q<31>::from_raw(stored)));
		unit = " HZ";
	}
	else {
		out.title = text(String::STRING_FOR_BLEND);
		shown = dsp::fixed::compressor_blend_percent(dsp::Q<31>::from_raw(stored));
		unit = " %";
	}
	out.value_text = std::to_string(shown) + unit;
	return out;
}

bool apply_push(bind::WriteSink& sink, ui::controls::Sink& controls, const bind::KnobFocus& focus,
                const KnobPush& push) {
	bool taken = true;
	const std::optional<EffectOwner> owner = effect_owner(focus);
	for (const EffectWrite& write : push.effects) {
		taken &= owner && controls.write_effect(*owner, write.path, write.value);
	}
	for (const auto& [path, menu_value] : push.song) {
		const bind::Target target{
		    .kind = bind::Target::Kind::MODEL, .path = path, .group = project::SettingGroupId::SONG};
		taken &= sink.write(bind::WriteRequest{.target = target, .menu_value = menu_value});
	}
	if (push.chain_view) {
		const auto* chain = owner ? std::get_if<project::ChainAddress>(&*owner) : nullptr;
		taken &= chain != nullptr && controls.chain_view(*chain, push.chain_view->first, push.chain_view->second);
	}
	if (push.controls) {
		const auto* sound = std::get_if<bind::SoundFocus>(&focus.owner);
		taken &= sound != nullptr
		         && controls.write_controls(
		             project::ControlsAddress{.kind = sound->slot.kind == project::SlotAddress::Kind::DRUM
		                                                  ? project::ControlsAddress::Kind::DRUM
		                                                  : project::ControlsAddress::Kind::OUTPUT,
		                                      .id = sound->slot.id},
		             *push.controls);
	}
	if (push.stutter) {
		taken &= controls.stutter(focus, *push.stutter);
	}
	return taken;
}

std::string note_length_name(uint32_t note_length, int32_t tick_magnitude) {
	return std::string{
	    music::note_length_name(music::note_magnitude(note_length, tick_magnitude), "-notes", false).view()};
}

} // namespace deluge::ui::scenes::services
