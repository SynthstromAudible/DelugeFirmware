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

#include "engines/effects/chain.h"

#include "engines/effects/codec.h"

#include <algorithm>
#include <array>

namespace deluge::effects {
namespace {

/// An effect and the owner element's entries it owns (bitcrush and eq own params only).
struct EffectKeys {
	std::string_view effect;
	std::array<std::string_view, 3> keys;
};

constexpr std::array<EffectKeys, 9> kEffectEntries{{
    {.effect = "saturation", .keys = {"clippingAmount"}},
    {.effect = "filter", .keys = {"lpfMode", "hpfMode", "filterRoute"}},
    {.effect = "bitcrush", .keys = {}},
    {.effect = "mod_fx", .keys = {"modFXType"}},
    {.effect = "eq", .keys = {}},
    {.effect = "delay", .keys = {"delay"}},
    {.effect = "stutter", .keys = {"stutter"}},
    {.effect = "compressor", .keys = {"audioCompressor"}},
    {.effect = "sidechain", .keys = {"sidechain"}},
}};

constexpr std::array<std::string_view, 9> kClipChain{"saturation", "filter",  "bitcrush",   "mod_fx",   "eq",
                                                     "delay",      "stutter", "compressor", "sidechain"};
/// The master processes mod FX, EQ and delay before the reverb returns join, the rest after; it saves a saturation and
/// a sidechain entry it does not process, which take the last two slots.
constexpr std::array<std::string_view, 9> kMasterChain{"mod_fx",  "eq",         "delay",      "filter",   "bitcrush",
                                                       "stutter", "compressor", "saturation", "sidechain"};
constexpr uint32_t kMasterReverbReturn = 3;

/// The view entries a host owner's element holds, in the order it writes them.
constexpr std::array<std::string_view, 2> kViewKeys{"modFXCurrentParam", "currentFilterType"};

const EffectKeys* keys_of(std::string_view effect) {
	const auto* found =
	    std::ranges::find_if(kEffectEntries, [effect](const EffectKeys& e) { return e.effect == effect; });
	return found == kEffectEntries.end() ? nullptr : found;
}

std::optional<std::string_view> effect_of_key(std::string_view key) {
	for (const EffectKeys& e : kEffectEntries) {
		if (std::ranges::find(e.keys, key) != e.keys.end() && !key.empty()) {
			return e.effect;
		}
	}
	return std::nullopt;
}

} // namespace

std::span<const std::string_view> chain_effects(Home home) {
	return home == Home::CLIP ? std::span<const std::string_view>{kClipChain}
	                          : std::span<const std::string_view>{kMasterChain};
}

std::optional<uint32_t> reverb_return(Home home) {
	return home == Home::MASTER ? std::optional<uint32_t>{kMasterReverbReturn} : std::nullopt;
}

std::optional<std::string_view> effect_of_setting(std::string_view path) {
	return effect_of_key(path.substr(0, path.find('/')));
}

std::vector<SlotEntries> encode_chain(const EffectStates& states, Home home, const plugin::CodecContext& context,
                                      std::optional<int32_t> compressor_threshold) {
	const EffectPieces pieces = encode_pieces(states, context, compressor_threshold);
	std::vector<SlotEntries> slots;
	for (std::string_view effect : chain_effects(home)) {
		SlotEntries slot{.effect = effect, .entries = serialization::Value::makeMap()};
		for (const serialization::Value* piece : {&pieces.attributes, &pieces.delay, &pieces.tags}) {
			for (const auto& [key, value] : piece->entries()) {
				if (effect_of_key(key) == effect) {
					slot.entries.set(key, value);
				}
			}
		}
		slots.push_back(std::move(slot));
	}
	return slots;
}

void decode_slot(std::string_view effect, const serialization::Value& entries, EffectStates& states,
                 const plugin::CodecContext& context) {
	const EffectKeys* owned = keys_of(effect);
	if (owned == nullptr) {
		return;
	}
	EffectStates read = states;
	decode(entries, read, context);
	for (std::string_view key : owned->keys) {
		if (!key.empty()) {
			keep_effect_entry(key, states, read);
		}
	}
}

serialization::Value view_entries(const serialization::Value& element) {
	serialization::Value view = serialization::Value::makeMap();
	for (std::string_view key : kViewKeys) {
		if (const serialization::Value* value = element.find(key)) {
			view.set(std::string{key}, *value);
		}
	}
	return view;
}

void write_chain(const std::vector<SlotEntries>& slots, const serialization::Value& view,
                 serialization::Value& element) {
	for (const auto& [key, value] : view.entries()) {
		element.set(key, value);
	}
	for (std::string_view key : kEffectKeys) {
		for (const SlotEntries& slot : slots) {
			if (const serialization::Value* value = slot.entries.find(key)) {
				element.set(std::string{key}, *value);
				break;
			}
		}
	}
}

} // namespace deluge::effects
