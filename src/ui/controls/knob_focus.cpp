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

#include "ui/controls/knob_focus.h"

#include "base/panic.h"
#include "project/song.h"

#include <algorithm>

namespace deluge::ui::controls {

namespace {

/// @p mode, or @p board's last section past it.
uint8_t capped(uint8_t mode, const Board& board) {
	// A board with no sections is a caller that never named its board: no mode would be a section of it.
	if (board.sections == 0) {
		freezeWithError("E-board");
	}
	return std::min<uint8_t>(mode, board.sections - 1);
}

/// The output whose mode the focus's knobs follow: a synth's, a kit row's kit's, a kit's or audio track's, a MIDI
/// track's. None for the song's, no focus, an owner with no controls (a CV track has no mode), or a drum that names
/// no kit.
std::optional<uint32_t> mode_output(const KnobFocus& focus) {
	if (const auto* sound = std::get_if<SoundFocus>(&focus.owner)) {
		if (sound->kit) {
			return sound->kit;
		}
		return sound->slot.kind == project::SlotAddress::Kind::OUTPUT ? std::optional{sound->slot.id} : std::nullopt;
	}
	if (const auto* chain = std::get_if<ChainFocus>(&focus.owner)) {
		return chain->chain.kind == project::ChainAddress::Kind::OUTPUT ? std::optional{chain->chain.id} : std::nullopt;
	}
	if (const auto* midi = std::get_if<MidiFocus>(&focus.owner)) {
		return midi->track.output_id;
	}
	return std::nullopt;
}

} // namespace

std::optional<project::ParamAddress> knob_owner(const KnobFocus& focus) {
	const auto clip_id = [](const std::optional<project::ClipAddress>& clip) {
		return clip ? std::optional{clip->clip_id} : std::nullopt;
	};
	if (std::holds_alternative<SongFocus>(focus.owner)) {
		return project::ParamAddress{.owner = project::OwnerId::song()};
	}
	if (const auto* sound = std::get_if<SoundFocus>(&focus.owner)) {
		return project::ParamAddress{.owner = sound->slot.kind == project::SlotAddress::Kind::DRUM
		                                          ? project::OwnerId::drum(sound->slot.id)
		                                          : project::OwnerId::instrument(sound->slot.id),
		                             .clip_id = clip_id(sound->clip)};
	}
	if (const auto* chain = std::get_if<ChainFocus>(&focus.owner)) {
		if (chain->owner == ChainOwner::AUDIO_TRACK) {
			return project::ParamAddress{.owner = chain->clip ? project::OwnerId::clip(chain->clip->clip_id)
			                                                  : project::OwnerId::instrument(chain->chain.id)};
		}
		return project::ParamAddress{.owner = project::OwnerId::instrument(chain->chain.id),
		                             .clip_id = clip_id(chain->clip)};
	}
	if (const auto* midi = std::get_if<MidiFocus>(&focus.owner); midi != nullptr && midi->clip) {
		return project::ParamAddress{.owner = project::OwnerId::clip(midi->clip->clip_id)};
	}
	// No focus and an owner with no controls have no param to write.
	return std::nullopt;
}

std::optional<project::KnobModeOwner> knob_mode_owner(const KnobFocus& focus) {
	if (std::holds_alternative<SongFocus>(focus.owner)) {
		return project::SongAddress{};
	}
	if (const std::optional<uint32_t> id = mode_output(focus)) {
		return project::TrackAddress{*id};
	}
	return std::nullopt;
}

uint8_t knob_mode(const project::Song& song, const KnobFocus& focus, const Board& board) {
	if (std::holds_alternative<SongFocus>(focus.owner)) {
		return capped(song.mod_knob_mode, board);
	}
	if (const std::optional<uint32_t> id = mode_output(focus)) {
		const project::DocOutput* output = song.output_structure.find(*id);
		return output != nullptr ? capped(output->mod_knob_mode, board) : 0;
	}
	return 0;
}

} // namespace deluge::ui::controls
