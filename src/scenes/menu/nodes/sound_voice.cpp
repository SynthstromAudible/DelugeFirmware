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

/// @file
/// @brief The voice page's `get`/`set` hooks and its exported root span. The `Node` literals are in
///        `sound_voice.h`.

#include "scenes/menu/nodes/sound_voice.h"

#include "scenes/l10n_bridge.h"
#include "scenes/menu/bindings.h"
#include "scenes/menu/context.h"

#include "definitions_cxx.hpp"
#include "processing/sound/sound.h"

namespace deluge::scenes::menu::nodes {

using deluge::l10n::String;

int32_t get_voice_priority(const MenuContext& ctx) {
	// `voice::Priority::readCurrentValue` (`voice/priority.h:32`) reads `*currentPriority`, which is
	// the sound's own on a synth and the audio clip's on an audio clip (`context.cpp:308`, `:338`).
	return ctx.priority != nullptr ? static_cast<int32_t>(*ctx.priority) : 0;
}

void set_voice_priority(const MenuContext& ctx, int32_t value) {
	// `Priority::writeCurrentValue` (`priority.h:25-37`). Legacy writes `*currentPriority` twice --
	// once before the affect-entire branch (`:27`) and once inside the `else` (`:36`) -- the same
	// double write `bindings.cpp` records as a bug in `patched_param::Integer` and does not
	// reproduce. The single-target arm below is the `else`.
	const auto priority = static_cast<VoicePriority>(value);
	if (ctx.priority != nullptr) {
		*ctx.priority = priority;
	}
	if (!bindings::writes_whole_kit(ctx)) {
		return;
	}
	bindings::apply_to_targets(ctx, value, 0,
	                           [](const bindings::SoundTarget& t, int32_t v, int32_t, const MenuContext&) {
		                           if (t.sound != nullptr) {
			                           t.sound->voicePriority = static_cast<VoicePriority>(v);
		                           }
	                           });
}

size_t polyphony_names(const MenuContext& ctx, std::span<std::string_view> out) {
	// `PolyphonyType::getOptions` (`voice/polyphony.h:114-125`): four modes, plus CHOKE on a kit.
	static constexpr String kBase[] = {
	    String::STRING_FOR_AUTO,
	    String::STRING_FOR_POLYPHONIC,
	    String::STRING_FOR_MONOPHONIC,
	    String::STRING_FOR_LEGATO,
	};
	size_t n = 0;
	for (String s : kBase) {
		if (n < out.size()) {
			out[n++] = deluge::scenes::text_lookup(sid(s));
		}
	}
	if (ctx.is_kit && n < out.size()) {
		out[n++] = deluge::scenes::text_lookup(sid(String::STRING_FOR_CHOKE));
	}
	return n;
}

int32_t get_polyphony(const MenuContext& ctx) {
	return ctx.sound != nullptr ? static_cast<int32_t>(ctx.sound->polyphonic) : 0; // `polyphony.h:93`
}

void set_polyphony(const MenuContext& ctx, int32_t value) {
	bindings::apply_to_targets(ctx, value, 0,
	                           [](const bindings::SoundTarget& t, int32_t v, int32_t, const MenuContext&) {
		                           if (t.sound != nullptr) {
			                           t.sound->polyphonic = static_cast<PolyphonyMode>(v); // `:105`, `:110`
		                           }
	                           });
}

int32_t get_voice_count(const MenuContext& ctx) {
	// `VoiceCount::readCurrentValue` (`voice/polyphony.h:25-31`): anything above the menu maximum --
	// legacy stores 127 for "unlimited" -- shows as 0, which the item draws as an infinity icon.
	if (ctx.sound == nullptr) {
		return 0;
	}
	const int32_t count = ctx.sound->maxVoiceCount;
	return count > 16 ? 0 : count;
}

void set_voice_count(const MenuContext& ctx, int32_t value) {
	// `VoiceCount::writeCurrentValue` (`polyphony.h:33-54`): 0 means unlimited, stored as 127. The
	// affect-entire sweep filters to polyphonic drums *inside* the loop, where legacy filters it.
	const int32_t stored = value == 0 ? 127 : value;
	bindings::apply_to_targets(ctx, stored, 0,
	                           [](const bindings::SoundTarget& t, int32_t v, int32_t, const MenuContext&) {
		                           if (t.sound == nullptr) {
			                           return;
		                           }
		                           if (t.whole_kit && t.sound->polyphonic != PolyphonyMode::POLY) {
			                           return; // `polyphony.h:44-46`
		                           }
		                           t.sound->maxVoiceCount = static_cast<uint8_t>(v);
	                           });
}

namespace {
constexpr const Node* kRoots[] = {&voice};
} // namespace

const std::span<const Node* const> kSoundVoiceChildren{kRoots};

} // namespace deluge::scenes::menu::nodes
