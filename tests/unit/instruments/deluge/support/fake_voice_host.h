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

#pragma once

#include "instruments/deluge/voice_pool.h"
#include "plugin-api/types/voice_priority.h"
#include "synth/voice/voice_clock.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

/// @file
/// @brief A kernel and a host for the voice pool's specs: ratings set by hand, every host call logged.

namespace deluge::instrument::test {

/// @brief A kernel whose rating and cull fade are set, and that stops after a set number of releases.
struct FakeKernel {
	uint32_t rating{};
	bool fading{};
	int32_t releases_to_stop{1}; ///< Each release counts one down; at 0 the voice has stopped
	std::string last_release;    ///< The kind of the last release asked of it

	[[nodiscard]] uint32_t priority_rating(plugin::VoicePriority /*priority*/, size_t /*count*/) const {
		return rating;
	}
	[[nodiscard]] bool is_cull_fading() const { return fading; }
	bool do_immediate_release(synth::VoiceClock /*clock*/) { return released("immediate"); }
	bool do_fast_release(uint32_t increment, synth::VoiceClock /*clock*/) {
		return released("fast " + std::to_string(increment));
	}
	bool speed_up_release(synth::VoiceClock /*clock*/) { return released("speed up"); }

private:
	bool released(std::string kind) {
		last_release = std::move(kind);
		return --releases_to_stop > 0;
	}
};

/// @brief A host that allocates slots from the heap and logs each call, with the voices it saw listed.
struct FakeHost {
	using Slot = VoiceSlot<int, FakeKernel>; ///< The part is the voice's id
	using List = std::vector<Slot*>;

	/// Counts exclusions taken and how deep they nest.
	struct Exclusion {
		Exclusion() {
			taken++;
			depth++;
		}
		~Exclusion() { depth--; }
		Exclusion(const Exclusion&) = delete;
		Exclusion& operator=(const Exclusion&) = delete;
		static inline int32_t taken = 0;
		static inline int32_t depth = 0;
	};

	std::vector<std::string> log;
	std::function<size_t()> listed; ///< How many voices the pool lists, logged with each call
	uint32_t next_tick{};
	int32_t next_id{};

	Slot* acquire() {
		auto* slot = new Slot{.part = next_id++};
		log.push_back("acquire " + std::to_string(slot->part));
		return slot;
	}
	void unassign(Slot& slot) { log.push_back(seen("unassign", slot)); }
	void release(Slot& slot) noexcept {
		log.push_back(seen("release", slot));
		delete &slot;
	}
	void admit(uint32_t cost) { log.push_back("admit " + std::to_string(cost)); }
	void voice_starting() { log.push_back("starting"); }
	void voices_changed() { log.push_back("changed"); }
	void note_edit() { log.push_back("edit depth " + std::to_string(Exclusion::depth)); }
	synth::VoiceClock clock() { return synth::VoiceClock{&next_tick}; }

private:
	std::string seen(std::string_view what, const Slot& slot) const {
		return std::string(what) + " " + std::to_string(slot.part) + " listed " + std::to_string(listed ? listed() : 0);
	}
};

/// @brief A host and a pool over it, with @p ratings published as voices 0, 1, …
struct Rig {
	FakeHost host;
	VoicePool<FakeHost> pool{host};

	explicit Rig(std::vector<uint32_t> ratings = {}, std::vector<bool> fading = {}) {
		host.listed = [this] { return pool.size(); };
		for (size_t i = 0; i < ratings.size(); i++) {
			FakeHost::Slot* slot = host.acquire();
			slot->kernel.rating = ratings[i];
			slot->kernel.fading = i < fading.size() && fading[i];
			pool.publish(slot);
		}
		host.log.clear();
		FakeHost::Exclusion::taken = 0;
	}
	~Rig() { pool.kill_all(); }
	Rig(const Rig&) = delete;
	Rig& operator=(const Rig&) = delete;

	/// The ids of the voices listed, in order.
	[[nodiscard]] std::vector<int32_t> ids() const {
		return pool.read([](const FakeHost::List& voices) {
			std::vector<int32_t> out;
			for (const FakeHost::Slot* slot : voices) {
				out.push_back(slot->part);
			}
			return out;
		});
	}
};

} // namespace deluge::instrument::test
