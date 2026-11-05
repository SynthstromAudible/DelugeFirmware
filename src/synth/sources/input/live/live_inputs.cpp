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
#include "synth/sources/input/live/live_inputs.h"
#include "memory/memory_allocator_interface.h" // allocMaxSpeed / delugeDealloc (the thin allocator seam)
#include "synth/sources/input/live/live_input.h"

#include <memory>
#include <new>
#include <optional>
#include <utility>

namespace deluge::synth {

namespace {
/// @brief A new history of @p channel, or nullptr if there is no memory for it.
template <typename Frame>
[[nodiscard]] LiveInput<Frame>* make(InputChannel channel) {
	void* memory = allocMaxSpeed(sizeof(LiveInput<Frame>));
	if (memory == nullptr) {
		return nullptr;
	}
	return new (memory) LiveInput<Frame>(channel); // NOLINT(cppcoreguidelines-owning-memory): the store frees it
}

template <typename Frame>
void release(LiveInput<Frame>* input) {
	if (input != nullptr) {
		std::destroy_at(input);
		delugeDealloc(input);
	}
}

/// @brief The channel of the mono history @p type reads, if it reads one.
[[nodiscard]] std::optional<InputChannel> mono_channel(OscType type) {
	switch (type) {
	case OscType::INPUT_L:
	case OscType::INPUT_STEREO:
		return InputChannel::LEFT;
	case OscType::INPUT_R:
		return InputChannel::RIGHT;
	default:
		return std::nullopt;
	}
}
} // namespace

LiveInputStore::~LiveInputStore() {
	release(inputs_.mono);
	release(inputs_.stereo);
}

LiveInputStore LiveInputStore::for_type(OscType type) {
	LiveInputStore store;
	if (const auto channel = mono_channel(type)) {
		store.inputs_.mono = make<dsp::Q<31>>(*channel);
	}
	if (type == OscType::INPUT_STEREO) {
		store.inputs_.stereo = make<dsp::StereoSample>(InputChannel::STEREO);
	}
	return store;
}

bool LiveInputStore::holds_for(OscType type) const {
	const auto channel = mono_channel(type);
	const bool mono_held =
	    channel ? inputs_.mono != nullptr && inputs_.mono->channel() == *channel : inputs_.mono == nullptr;
	const bool stereo_held = (type == OscType::INPUT_STEREO) == (inputs_.stereo != nullptr);
	return mono_held && stereo_held;
}

void LiveInputStore::swap(LiveInputStore& other) noexcept {
	std::swap(inputs_.mono, other.inputs_.mono);
	std::swap(inputs_.stereo, other.inputs_.stereo);
}

} // namespace deluge::synth
