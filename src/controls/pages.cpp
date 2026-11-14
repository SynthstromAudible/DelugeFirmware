/*
 * Copyright © 2026 Synthstrom Audible Limited
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

#include "controls/pages.h"

#include "controls/mod_buttons.h"
#include "deluge-project/song.h"
#include "foundation/panic.h"
#include "plugin-api/types/plugin_id.h"

#include <algorithm>
#include <cstddef>
#include <optional>
#include <variant>
#include <vector>

namespace deluge::controls {

namespace {

using project::ParamSpace;

/// The filter view's last page, EQ: a view past it is EQ.
constexpr uint32_t kEqPage = 2;
/// The song compressor's settings the lower knob edits, one page each after the reverb page
/// (global_effectable::kCompPages).
constexpr uint32_t kCompPages = 5;
/// The most pages a publisher's find serves on one section: a bound on the walk of a sound's cycle.
constexpr uint32_t kMaxSectionPages = 16;

/// The owner a focus names, as its pages read it: its publisher, and what the publisher reads.
struct Publisher {
	const plugin::RemoteControls* record{nullptr};
	plugin::PageContext context;
};

/// A sound's saved knobs, as its record holds them; nullptr for none.
const std::vector<project::ControllerMapping>* sound_controls(const project::Song& song, project::SlotAddress slot) {
	if (slot.kind == project::SlotAddress::Kind::DRUM) {
		const project::DocDrum* drum = song.drum_structure.find(slot.id);
		return drum != nullptr ? &drum->controls : nullptr;
	}
	const project::DocOutput* output = song.output_structure.find(slot.id);
	return output != nullptr ? &output->controls : nullptr;
}

/// The Deluge instrument's publisher for the sound @p focus names, reading the state its slot holds.
Publisher sound_publisher(const project::Song& song, const SoundFocus& focus, const PageSources& sources) {
	const project::InstrumentSlot* slot = song.resolveSlot(focus.slot);
	return {.record = plugin::remote_controls_for(sources.publishers, plugin::kDelugeInstrument),
	        .context = {.state = slot != nullptr ? &slot->state : nullptr, .alt_delay = sources.alt_delay}};
}

/// The global effectable's publisher for @p chain (none: a chain that does not resolve), reading its effects' state.
Publisher chain_publisher(const project::FxChain* chain, bool is_song, const PageSources& sources) {
	return {.record = plugin::remote_controls_for(sources.publishers, plugin::kGlobalEffectable),
	        .context = {.state = chain != nullptr ? &chain->effects : nullptr,
	                    .song = is_song,
	                    .alt_delay = sources.alt_delay}};
}

/// A sound's saved knobs for @p mode: the first mapping of each knob, decorated by @p publisher.
Page sound_page(const project::Song& song, const SoundFocus& focus, uint8_t mode, const Publisher& publisher) {
	plugin::RemotePage saved{.section = mode, .name = {}, .for_preset = true};
	const std::vector<project::ControllerMapping>* controls = sound_controls(song, focus.slot);
	if (controls != nullptr && publisher.record != nullptr && publisher.record->decorate != nullptr) {
		for (const project::ControllerMapping& m : *controls) {
			// The first mapping of a knob is the one it moves.
			if (m.control.button == mode && m.control.knob < saved.slots.size() && !saved.slots[m.control.knob].param) {
				saved.slots[m.control.knob] = publisher.record->decorate(publisher.context, m.target);
			}
		}
	}
	return page_of(saved);
}

/// A sound's cycle on mod button @p mode: the pages its publisher finds there, from page id 0 to the first it does
/// not serve. The Deluge instrument's are the filter knob's: low-pass, high-pass, EQ with the cutoff on the upper knob
/// (group 0), then the same with it on the lower knob (group 1), as a sound may hold its filter knob on either knob of
/// any button.
std::vector<Page> sound_cycle(uint8_t mode, const Publisher& publisher) {
	std::vector<Page> out;
	if (publisher.record == nullptr || publisher.record->find == nullptr) {
		return out;
	}
	plugin::RemotePage found;
	for (uint32_t page_id = 0;
	     page_id < kMaxSectionPages && publisher.record->find(publisher.context, mode, page_id, found); page_id++) {
		out.push_back(page_of(found));
	}
	return out;
}

/// Which values a chain's knobs edit, as its record holds them; a new chain's by default.
struct ChainView {
	uint8_t mod_fx_param{project::FxChain{}.mod_fx_param};
	uint8_t filter_type{project::FxChain{}.filter_type};
};

/// The song's compressor page the lower knob edits in @p focus's compressor view.
uint32_t comp_page(const KnobFocus& focus) {
	return 1 + (focus.comp.param % kCompPages);
}

/// A chain's page for @p mode: the page of the view @p chain holds (a new chain's for none) and, on the song, of
/// @p focus's compressor view. The publisher's find serves a mod FX view the type has no param for too.
Page chain_page(const KnobFocus& focus, const project::FxChain* chain, uint8_t mode, const Publisher& publisher) {
	const ChainView view = chain != nullptr
	                           ? ChainView{.mod_fx_param = chain->mod_fx_param, .filter_type = chain->filter_type}
	                           : ChainView{};
	uint32_t current = 0;
	if (mode == mod_button::kModFx) {
		current = view.mod_fx_param;
	}
	else if (mode == mod_button::kFilter) {
		current = std::min<uint32_t>(view.filter_type, kEqPage);
	}
	else if (mode == mod_button::kCompressor && publisher.context.song && focus.comp.editing) {
		current = comp_page(focus);
	}
	plugin::RemotePage found;
	if (publisher.record != nullptr && publisher.record->find != nullptr
	    && publisher.record->find(publisher.context, mode, current, found)) {
		return page_of(found);
	}
	return Page{.section = mode};
}

/// A chain's cycle on mod button @p mode: its publisher's device pages of that section, in their order.
std::vector<Page> chain_cycle(uint8_t mode, const Publisher& publisher) {
	std::vector<Page> out;
	if (publisher.record == nullptr || publisher.record->count == nullptr || publisher.record->get == nullptr) {
		return out;
	}
	const uint32_t count = publisher.record->count(publisher.context);
	plugin::RemotePage found;
	for (uint32_t index = 0; index < count && publisher.record->get(publisher.context, index, found); index++) {
		if (found.section == mode) {
			out.push_back(page_of(found));
		}
	}
	return out;
}

/// A MIDI track's page: the CCs its record names for @p mode's knobs, in its clip; a press picks a knob's CC, with
/// or without a clip (the host turns the selection down where it cannot take it).
Page midi_page(const project::Song& song, const MidiFocus& focus, uint8_t mode, const Board& board) {
	Page out{.section = mode};
	// A board with no slots is a caller that never set `PageSources::board`: no page would name a CC.
	if (board.slots == 0) {
		freezeWithError("E-board");
	}
	const size_t knobs = std::min<size_t>(board.slots, out.slots.size());
	for (size_t knob = 0; knob < knobs; knob++) {
		out.slots[knob].press = plugin::PressAction::MIDI_CC_SELECT;
	}
	const project::DocOutput* output = song.output_structure.find(focus.track.output_id);
	if (output == nullptr || !focus.clip) {
		return out;
	}
	for (size_t knob = 0; knob < knobs; knob++) {
		Slot& slot = out.slots[knob];
		const size_t at = (static_cast<size_t>(mode) * knobs) + knob;
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

/// The chain @p focus names: the song's, or a kit's or audio track's (nullptr when it does not resolve).
const project::FxChain* focused_chain(const project::Song& song, const KnobFocus& focus) {
	if (std::holds_alternative<SongFocus>(focus.owner)) {
		return &song.master_effects;
	}
	return song.resolveChain(std::get<ChainFocus>(focus.owner).chain);
}

/// Whether @p focus is on a chain: the song's or a host chain.
bool on_chain(const KnobFocus& focus) {
	return std::holds_alternative<SongFocus>(focus.owner) || std::holds_alternative<ChainFocus>(focus.owner);
}

} // namespace

Page current_page(const project::Song& song, const KnobFocus& focus, const PageSources& sources) {
	const uint8_t mode = knob_mode(song, focus);
	if (const auto* sound = std::get_if<SoundFocus>(&focus.owner)) {
		return sound_page(song, *sound, mode, sound_publisher(song, *sound, sources));
	}
	if (const auto* midi = std::get_if<MidiFocus>(&focus.owner)) {
		return midi_page(song, *midi, mode, sources.board);
	}
	if (on_chain(focus)) {
		const project::FxChain* chain = focused_chain(song, focus);
		return chain_page(focus, chain, mode,
		                  chain_publisher(chain, std::holds_alternative<SongFocus>(focus.owner), sources));
	}
	return {};
}

SectionPages section_pages(const project::Song& song, const KnobFocus& focus, const PageSources& sources) {
	SectionPages out{.current = current_page(song, focus, sources), .cycle = {}, .group_entry = {}};
	const uint8_t mode = knob_mode(song, focus);
	if (const auto* sound = std::get_if<SoundFocus>(&focus.owner)) {
		out.cycle = sound_cycle(mode, sound_publisher(song, *sound, sources));
	}
	else if (on_chain(focus)) {
		out.cycle = chain_cycle(
		    mode, chain_publisher(focused_chain(song, focus), std::holds_alternative<SongFocus>(focus.owner), sources));
		out.group_entry = {0, comp_page(focus)};
	}
	return out;
}

} // namespace deluge::controls
