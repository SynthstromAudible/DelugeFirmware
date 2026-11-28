/*
 * Copyright © 2024 Synthstrom Audible Limited
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

#include "modulation/params/param.h"
#include "arp/params.h"
#include "definitions_cxx.hpp"
#include "gui/l10n/l10n.h"
#include "gui/l10n/param_names.h"
#include "gui/l10n/strings.h"
#include "host/chains/params.h"
#include "instruments/classic/params.h"
#include "model/song/song.h"
#include "processing/sound/sound.h"
#include "util/container/enum_to_string_map.hpp"
#include <cstring>
#include <span>

namespace deluge::modulation::params {

const plugin::ParamInfo* paramInfoFor(Kind kind, int32_t paramId) {
	if (paramId < 0) {
		return nullptr;
	}
	if (kind == Kind::ARP) {
		return static_cast<std::size_t>(paramId) < arp::kParamCount ? &arp::kParams[paramId].info : nullptr;
	}
	return instruments::classic::param_row_at(kind, static_cast<uint32_t>(paramId));
}

bool hasParamFlag(Kind kind, int32_t paramId, plugin::ParamFlag flag) {
	const plugin::ParamInfo* info = paramInfoFor(kind, paramId);
	return info != nullptr && info->flags.has(flag);
}

bool isParamBipolar(Kind kind, int32_t paramID) {
	return instruments::classic::is_bipolar_at(kind, static_cast<uint32_t>(paramID));
}

bool isParamArpRhythm(Kind kind, int32_t paramID) {
	return kind == Kind::ARP && paramID >= 0 && arp::is_rhythm(arp::id_at(static_cast<std::size_t>(paramID)));
}

bool isParamStutter(Kind kind, int32_t paramID) {
	return instruments::classic::is_stutter_rate_at(kind, static_cast<uint32_t>(paramID));
}

bool isParamQuantizedStutter(Kind kind, int32_t paramID, const deluge::model::EffectsOwner* owner) {
	return isParamStutter(kind, paramID) && owner != nullptr
	       && (owner->effects().stutter.use_song_stutter
	               ? currentSong->live.master_channel.binding().effects().stutter.quantized
	               : owner->effects().stutter.quantized);
}

char const* getPatchedParamShortName(ParamType type) {
	return l10n::patched_param_short_name_at(type).data();
}

namespace {
/// The name an envelope's mode gives param @p p of @p owner's sound, or nullopt for its usual one.
std::optional<std::string_view> envelopeParamName(Kind kind, int32_t p, const deluge::model::EffectsOwner& owner,
                                                  bool brief) {
	if (owner.sound == nullptr) {
		return std::nullopt;
	}
	l10n::EnvelopeModes modes{};
	for (size_t e = 0; e < modes.size(); ++e) {
		modes[e] = owner.sound->config().envelopes[e].mode;
	}
	return l10n::envelope_param_name_at(kind, static_cast<uint32_t>(p), modes, brief);
}
} // namespace

char const* getPatchedParamShortName(ParamType type, const deluge::model::EffectsOwner* owner) {
	if (owner == nullptr) {
		return getPatchedParamShortName(type);
	}
	if (const auto name = envelopeParamName(Kind::PATCHED, type, *owner, /*brief=*/true)) {
		return name->data();
	}
	const auto& filter = owner->binding->renderEffects().filter;
	return getPatchedParamShortName(type, filter.lpf_mode, filter.hpf_mode);
}

char const* getPatchedParamShortName(ParamType type, deluge::effects::FilterMode lpf, deluge::effects::FilterMode hpf) {
	return l10n::patched_param_short_name_at(type, lpf, hpf).data();
}

char const* getParamDisplayName(Kind kind, int32_t p) {
	return l10n::param_name_at(kind, static_cast<uint32_t>(p)).data();
}

char const* getParamDisplayName(Kind kind, int32_t p, const deluge::model::EffectsOwner* owner) {
	if (owner == nullptr) {
		return getParamDisplayName(kind, p);
	}
	if (const auto name = envelopeParamName(kind, p, *owner, /*brief=*/false)) {
		return name->data();
	}
	const auto& filter = owner->binding->renderEffects().filter;
	return getParamDisplayName(kind, p, filter.lpf_mode, filter.hpf_mode);
}

char const* getParamDisplayName(Kind kind, int32_t p, deluge::effects::FilterMode lpf,
                                deluge::effects::FilterMode hpf) {
	return l10n::param_name_at(kind, static_cast<uint32_t>(p), lpf, hpf).data();
}

bool paramNeedsLPF(ParamType p, bool fromAutomation) {
	// Volumes and the like smooth a user's change so it doesn't step, but take a step of automation at once so the
	// step lands at its value
	return hasParamFlag(Kind::PATCHED, p, plugin::ParamFlag::SMOOTHED)
	       || (!fromAutomation && hasParamFlag(Kind::PATCHED, p, plugin::ParamFlag::SMOOTHED_USER_ONLY));
}

/// @brief The name the instrument's or the global effectable's table gives id @p param, or "none".
constexpr char const* tableName(std::span<const plugin::ParamInfo> table, ParamType param) {
	const plugin::ParamInfo* row = plugin::find_param(table, plugin::ParamId{param});
	return row != nullptr ? row->file_name.data() : "none";
}

constexpr char const* paramNameForFileConst(Kind const kind, ParamType const param, bool forMidiFollowFile = false) {
	using enum Kind;
	// Patched params have one name whichever collection asks, the runtime index's param's
	if (param < UNPATCHED_START) {
		return param < kNumParams ? tableName(instruments::classic::kParams,
		                                      static_cast<ParamType>(instruments::classic::flat_param_id(param).value))
		                          : "none";
	}
	if (kind == UNPATCHED_GLOBAL) {
		const auto unpatched = static_cast<UnpatchedGlobal>(param - UNPATCHED_START);
		if (forMidiFollowFile && unpatched == UNPATCHED_VOLUME) {
			return "volumePostFX"; // MIDIFollow.XML's vocabulary
		}
		if (forMidiFollowFile && unpatched == UNPATCHED_PITCH_ADJUST) {
			return "pitch"; // MIDIFollow.XML's vocabulary
		}
		if (unpatched == UNPATCHED_TEMPO) {
			// A learned knob or a MIDI follow mapping can't name the tempo; only the param block writes it, from the
			// codec's own table
			return "none";
		}
		return tableName(host::chains::kParams, param);
	}
	// A sound's own unpatched params only for a sound's collection; the shared ones for any
	if (kind == UNPATCHED_SOUND || param < UNPATCHED_START + UNPATCHED_NUM_SHARED) {
		return tableName(instruments::classic::kParams, param);
	}
	return "none";
}
char const* paramNameForFile(Kind const kind, ParamType const param, bool forMidiFollowFile) {
	return paramNameForFileConst(kind, param, forMidiFollowFile);
}
constexpr ParamType fileStringToParamConst(Kind kind, char const* name, bool allowPatched) {
	int32_t start = allowPatched ? 0 : UNPATCHED_START;
	for (int32_t p = start; p < kUnpatchedAndPatchedMaximum; ++p) {
		if (std::string_view(name) == paramNameForFileConst(kind, p)) {
			return p;
		}
	}

	if (std::string_view(name) == "range") {
		return util::to_underlying(PLACEHOLDER_RANGE); // For compatibility reading files from before V3.2.0
	}

	return util::to_underlying(GLOBAL_NONE);
}
ParamType fileStringToParam(Kind kind, char const* name, bool allowPatched) {
	return fileStringToParamConst(kind, name, allowPatched);
}
constexpr bool validateParams() {
	bool m = true;
	Kind kind = Kind::UNPATCHED_SOUND;
	for (int32_t p = 0; p < kUnpatchedAndPatchedMaximum; ++p) {
		auto name = paramNameForFileConst(kind, p);
		auto paramFromName = fileStringToParamConst(kind, name, true);
		if (p != paramFromName && std::string_view(name) != "none") {
			m = false;
		}
	}
	kind = Kind::UNPATCHED_GLOBAL;
	for (int32_t p = UNPATCHED_START; p < kUnpatchedAndPatchedMaximum; ++p) {
		auto name = paramNameForFileConst(kind, p);
		auto paramFromName = fileStringToParamConst(kind, name, false);
		if (p != paramFromName && std::string_view(name) != "none") {
			m = false;
		}
	}
	return m;
}
static_assert(validateParams());
} // namespace deluge::modulation::params
