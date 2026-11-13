#include "scenes/bind/section_pages.h"

#include "controls/mod_buttons.h"
#include "effects/global_effectable/pages.h"
#include "instruments/deluge/pages.h"

#include <algorithm>
#include <utility>

namespace deluge::scenes::bind {

namespace {

using controls::Page;
using controls::SectionPages;
using project::ParamSpace;

namespace mod_button = controls::mod_button;

/// The filter cycle's last page, EQ: a filter view past it is EQ.
constexpr uint32_t kEqPage = 2;

/// Converts @p pages to the controls' pages.
std::vector<Page> pages_of(const auto& pages) {
	std::vector<Page> out;
	out.reserve(std::size(pages));
	for (const plugin::RemotePage& p : pages) {
		out.push_back(controls::page_of(p));
	}
	return out;
}

/// A sound's filter cycle on mod button @p mode: low-pass, high-pass, EQ with the cutoff on the upper knob (group 0,
/// page ids 0 to 2), then the same with it on the lower knob (group 1, page ids 3 to 5). A sound may hold its filter
/// knob on either knob of any button, and its press cycles it wherever it is.
std::vector<Page> sound_filter_cycle(uint8_t mode) {
	std::vector<Page> out;
	for (const uint8_t group : {uint8_t{0}, uint8_t{1}}) {
		for (const plugin::RemotePage& p : instrument::kFilterCycle) {
			Page page = controls::page_of(p);
			page.section = mode;
			page.group = group;
			if (group == 1) {
				page.page_id += instrument::kFilterCycle.size();
				std::swap(page.slots[0], page.slots[1]);
			}
			out.push_back(page);
		}
	}
	return out;
}

/// A sound's saved knobs for @p mode: the first mapping of each knob.
Page sound_page(const project::Song& song, const controls::SoundFocus& focus, uint8_t mode, const PageInputs& inputs) {
	const std::vector<project::ControllerMapping>* controls = nullptr;
	if (focus.slot.kind == project::SlotAddress::Kind::DRUM) {
		const project::DocDrum* drum = song.drum_structure.find(focus.slot.id);
		controls = drum != nullptr ? &drum->controls : nullptr;
	}
	else {
		const project::DocOutput* output = song.output_structure.find(focus.slot.id);
		controls = output != nullptr ? &output->controls : nullptr;
	}
	plugin::RemotePage saved{.section = mode, .name = {}, .for_preset = true};
	if (controls != nullptr) {
		for (const project::ControllerMapping& m : *controls) {
			// The first mapping of a knob is the one it moves.
			if (m.control.button == mode && m.control.knob < saved.slots.size() && !saved.slots[m.control.knob].param) {
				saved.slots[m.control.knob] =
				    instrument::slot_for(m.target, {.fm = inputs.fm, .alt_delay = inputs.alt_delay});
			}
		}
	}
	return controls::page_of(saved);
}

/// What changes a chain's pages, for the global effectable.
global_effectable::PageInputs chain_inputs(bool is_song, const PageInputs& inputs) {
	return {.song = is_song, .mod_fx_type = inputs.effects.mod_fx.type, .alt_delay = inputs.alt_delay};
}

/// The song's compressor page the lower knob edits in @p focus's compressor view.
uint32_t comp_page(const KnobFocus& focus) {
	return 1 + (focus.comp.param % global_effectable::kCompPages);
}

/// Which values a chain's knobs edit, as its record holds them.
struct ChainView {
	uint8_t mod_fx_param{project::FxChain{}.mod_fx_param};
	uint8_t filter_type{project::FxChain{}.filter_type};
};

/// A chain's page for @p mode: the view @p chain holds and, on the song, the compressor view of @p focus.
Page chain_page(const KnobFocus& focus, ChainView chain, bool is_song, uint8_t mode, const PageInputs& inputs) {
	if (mode == mod_button::kModFx) {
		return controls::page_of(global_effectable::mod_fx_page(chain.mod_fx_param));
	}
	uint32_t current = 0;
	if (mode == mod_button::kFilter) {
		current = std::min<uint32_t>(chain.filter_type, kEqPage);
	}
	else if (mode == mod_button::kCompressor && is_song && focus.comp.editing) {
		current = comp_page(focus);
	}
	if (const std::optional<plugin::RemotePage> page =
	        global_effectable::section_page(mode, current, chain_inputs(is_song, inputs))) {
		return controls::page_of(*page);
	}
	return Page{.section = mode};
}

/// A MIDI track's page: the CCs its record names for @p mode's knobs, in its clip; a press picks a knob's CC, with
/// or without a clip (the host turns the selection down where it cannot take it).
Page midi_page(const project::Song& song, const controls::MidiFocus& focus, uint8_t mode) {
	Page out{.section = mode};
	constexpr size_t kKnobs = 2;
	for (size_t knob = 0; knob < kKnobs; knob++) {
		out.slots[knob].press = plugin::PressAction::MIDI_CC_SELECT;
	}
	const project::DocOutput* output = song.output_structure.find(focus.track.output_id);
	if (output == nullptr || !focus.clip) {
		return out;
	}
	for (size_t knob = 0; knob < kKnobs; knob++) {
		controls::Slot& slot = out.slots[knob];
		const size_t at = (static_cast<size_t>(mode) * kKnobs) + knob;
		if (at >= output->mod_knob_ccs.size()) {
			continue;
		}
		const project::ModKnobCc& cc = output->mod_knob_ccs[at];
		const auto in = [&slot](ParamSpace space, uint32_t id, plugin::ParamKind kind) {
			slot.target = project::ParamTarget{.space = space, .id = {id}};
			slot.kind = kind;
		};
		switch (cc.kind) {
		case project::ModKnobCc::Kind::BEND:
			in(ParamSpace::EXPRESSION, 0, plugin::ParamKind::EXPRESSION);
			break;
		case project::ModKnobCc::Kind::AFTERTOUCH:
			in(ParamSpace::EXPRESSION, 2, plugin::ParamKind::EXPRESSION);
			break;
		case project::ModKnobCc::Kind::NUMBER:
			if (cc.number == project::ModKnobCc::kYAxisNumber) {
				in(ParamSpace::EXPRESSION, 1, plugin::ParamKind::EXPRESSION);
			}
			else if (cc.number < project::ModKnobCc::kYAxisNumber) {
				in(ParamSpace::MIDI_CC, cc.number, plugin::ParamKind::MIDI);
			}
			break;
		case project::ModKnobCc::Kind::NONE:
			break;
		}
	}
	return out;
}

/// The view of the chain @p focus names, or none for another owner; a chain that does not resolve has a new chain's.
std::optional<ChainView> focused_chain(const project::Song& song, const KnobFocus& focus) {
	const project::FxChain* record = nullptr;
	if (std::holds_alternative<SongFocus>(focus.owner)) {
		record = &song.master_effects;
	}
	else if (const auto* chain = std::get_if<ChainFocus>(&focus.owner)) {
		record = song.resolveChain(chain->chain);
		if (record == nullptr) {
			return ChainView{};
		}
	}
	else {
		return std::nullopt;
	}
	return ChainView{.mod_fx_param = record->mod_fx_param, .filter_type = record->filter_type};
}

} // namespace

controls::Page current_page(const project::Song& song, const KnobFocus& focus, const PageInputs& inputs) {
	const uint8_t mode = knob_mode(song, focus);
	if (const auto* sound = std::get_if<SoundFocus>(&focus.owner)) {
		return sound_page(song, *sound, mode, inputs);
	}
	if (const auto* midi = std::get_if<MidiFocus>(&focus.owner)) {
		return midi_page(song, *midi, mode);
	}
	if (const std::optional<ChainView> chain = focused_chain(song, focus)) {
		return chain_page(focus, *chain, std::holds_alternative<SongFocus>(focus.owner), mode, inputs);
	}
	return {};
}

controls::SectionPages section_pages(const project::Song& song, const KnobFocus& focus, const PageInputs& inputs) {
	SectionPages out{.current = current_page(song, focus, inputs)};
	const uint8_t mode = knob_mode(song, focus);
	if (std::holds_alternative<SoundFocus>(focus.owner)) {
		out.cycle = sound_filter_cycle(mode);
	}
	else if (std::holds_alternative<SongFocus>(focus.owner) || std::holds_alternative<ChainFocus>(focus.owner)) {
		const bool is_song = std::holds_alternative<SongFocus>(focus.owner);
		out.cycle = pages_of(global_effectable::section_pages(mode, chain_inputs(is_song, inputs)));
		out.group_entry = {0, comp_page(focus)};
	}
	return out;
}

} // namespace deluge::scenes::bind
