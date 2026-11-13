#include "scenes/bind/section_pages.h"

#include "effects/global_effectable/pages.h"
#include "instruments/deluge/pages.h"

#include <algorithm>

namespace deluge::scenes::bind {

namespace {

using controls::Page;
using controls::SectionPages;
using project::ParamSpace;

/// The song's compressor settings the lower knob edits, one page each after the reverb page.
constexpr uint32_t kCompPages = 5;
/// The filter cycle's last page, EQ: a filter view past it is EQ.
constexpr uint32_t kEqPage = 2;
constexpr uint8_t kFilterButton = 1;
constexpr uint8_t kCompButton = 4;
constexpr uint8_t kModFxButton = 5;

/// Converts @p pages to the controls' pages.
std::vector<Page> pages_of(const auto& pages) {
	std::vector<Page> out;
	out.reserve(std::size(pages));
	for (const plugin::RemotePage& p : pages) {
		out.push_back(controls::page_of(p));
	}
	return out;
}

/// A sound's pages: its saved knobs for @p mode, and the filter cycle on the filter button.
SectionPages sound_pages(const project::Song& song, const controls::SoundFocus& focus, uint8_t mode,
                         const PageInputs& inputs) {
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
	return {.current = controls::page_of(saved),
	        .cycle = mode == kFilterButton ? pages_of(instrument::kFilterCycle) : std::vector<Page>{}};
}

/// A chain's pages: the global effectable's for @p mode, the current one the view @p chain holds and, on the song,
/// the compressor view of @p focus.
SectionPages chain_pages(const KnobFocus& focus, const project::FxChain& chain, bool is_song, uint8_t mode,
                         const PageInputs& inputs) {
	SectionPages out{
	    .cycle = pages_of(global_effectable::section_pages(
	        mode, {.song = is_song, .mod_fx_type = inputs.effects.mod_fx.type, .alt_delay = inputs.alt_delay})),
	    .group_entry = {0, 1 + (focus.comp.param % kCompPages)}};
	out.current.section = mode;
	if (mode == kModFxButton) {
		out.current = controls::page_of(global_effectable::mod_fx_page(chain.mod_fx_param));
		return out;
	}
	uint32_t current = 0;
	if (mode == kFilterButton) {
		current = std::min<uint32_t>(chain.filter_type, kEqPage);
	}
	else if (mode == kCompButton && is_song && focus.comp.editing) {
		current = out.group_entry[1];
	}
	const auto found = std::ranges::find_if(out.cycle, [&](const Page& p) { return p.page_id == current; });
	if (found != out.cycle.end()) {
		out.current = *found;
	}
	return out;
}

/// A MIDI track's page: the CCs its record names for @p mode's knobs, in its clip; a press picks a knob's CC.
SectionPages midi_pages(const project::Song& song, const controls::MidiFocus& focus, uint8_t mode) {
	SectionPages out;
	out.current.section = mode;
	const project::DocOutput* output = song.output_structure.find(focus.track.output_id);
	if (output == nullptr || !focus.clip) {
		return out;
	}
	constexpr size_t kKnobs = 2;
	for (size_t knob = 0; knob < kKnobs; knob++) {
		controls::Slot& slot = out.current.slots[knob];
		slot.press = plugin::PressAction::MIDI_CC_SELECT;
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

} // namespace

controls::SectionPages section_pages(const project::Song& song, const KnobFocus& focus, const PageInputs& inputs) {
	const uint8_t mode = knob_mode(song, focus);
	if (const auto* sound = std::get_if<SoundFocus>(&focus.owner)) {
		return sound_pages(song, *sound, mode, inputs);
	}
	if (std::holds_alternative<SongFocus>(focus.owner)) {
		return chain_pages(focus, song.master_effects, true, mode, inputs);
	}
	if (const auto* chain = std::get_if<ChainFocus>(&focus.owner)) {
		const project::FxChain* record = song.resolveChain(chain->chain);
		return chain_pages(focus, record != nullptr ? *record : project::FxChain{}, false, mode, inputs);
	}
	if (const auto* midi = std::get_if<MidiFocus>(&focus.owner)) {
		return midi_pages(song, *midi, mode);
	}
	return {};
}

} // namespace deluge::scenes::bind
