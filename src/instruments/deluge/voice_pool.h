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

#include "plugin-api/types/voice_priority.h"
#include "synth/voice/voice_host.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <utility>

/// @file
/// @brief A Deluge sound's voices: the slots the host allocates, held in the list only the pool edits.

namespace deluge::instrument {

/// @brief One voice: the host's part, declared ahead of the kernel, whose sources point into the part's ports.
/// @tparam Part The host's per-voice resources.
/// @tparam Kernel The voice that renders.
template <class Part, synth::voice_kernel Kernel>
struct VoiceSlot {
	Part part;
	Kernel kernel;
	uint32_t order{};         ///< When its note started, counted across every sound's voices
	bool just_created{false}; ///< The first render call only marks it created
};

/// @brief A sound's voices: the only code that changes the list, always with the render excluded.
/// @tparam H The host the slots come from (synth::voice_host).
template <synth::voice_host H>
class VoicePool {
public:
	using Slot = H::Slot;           ///< One voice, in host memory
	using List = H::List;           ///< The host's list of slot pointers
	using Exclusion = H::Exclusion; ///< Keeps the render out while it lives; nests
	using Context = H::Context;     ///< What the host frees a voice's resources with

	/// @brief A pool with no voices, over @p host.
	/// @param host The host the pool's slots come from and go back to; it outlives the pool.
	explicit VoicePool(H& host) : host_{&host} {}

	/// @brief Room for @p count voices, so the list never moves while it grows to that many.
	/// @param count The most voices the list will hold.
	void reserve(size_t count) { voices_.reserve(count); }

	/// @brief The voices, for the owner's own walk: on the audio tier, or under its own exclusion.
	/// @return The list.
	[[nodiscard]] const List& list() const { return voices_; }

	/// @brief Adds @p slot, a voice whose note has started, to the voices the render plays.
	/// @param slot The voice, from the host's acquire(); the pool holds it from here on.
	void publish(Slot* slot) {
		const Exclusion excluded;
		host_->note_edit();
		voices_.push_back(slot);
	}

	/// @brief Frees @p slot's resources while it stays listed; erase_if() later removes it.
	/// @param context What the host frees them with.
	void unassign(Slot& slot, Context context = {}) {
		const Exclusion excluded;
		host_->unassign(slot, context);
	}

	/// @brief Frees the voice at @p it: its resources while it is still listed, then its slot.
	/// @param it The voice.
	/// @param context What the host frees its resources with.
	/// @return The voice after it.
	List::iterator erase(List::const_iterator it, Context context = {}) {
		const Exclusion excluded;
		Slot* slot = *it;
		host_->unassign(*slot, context);
		host_->note_edit();
		auto next = voices_.erase(it);
		host_->release(*slot);
		return next;
	}

	/// @brief Removes and frees the slots of the voices @p pred names, in list order, their resources already freed
	///        by unassign().
	/// @param pred Called with each slot as a `const Slot*`; true removes it.
	template <class Pred>
	void erase_if(Pred pred) {
		const Exclusion excluded;
		host_->note_edit();
		// remove_if asks once per voice, in order: each one named is released as it is asked about.
		auto gone = std::ranges::remove_if(voices_, [&](Slot* slot) {
			if (!pred(static_cast<const Slot*>(slot))) {
				return false;
			}
			host_->release(*slot);
			return true;
		});
		voices_.erase(gone.begin(), gone.end());
	}

	/// @brief Frees every voice: each one's resources while all are still listed, then the slots.
	/// @param context What the host frees their resources with.
	void kill_all(Context context = {}) {
		const Exclusion excluded;
		for (Slot* slot : voices_) {
			host_->unassign(*slot, context);
		}
		host_->note_edit();
		for (Slot* slot : voices_) {
			host_->release(*slot);
		}
		voices_.clear();
	}

	/// @brief Fast-releases every voice at @p increment, freeing those it stops.
	/// @param context What the host frees a stopped voice's resources with.
	void fast_release_all(uint32_t increment, Context context = {}) {
		const Exclusion excluded;
		for (auto it = voices_.begin(); it != voices_.end();) {
			it = (*it)->kernel.do_fast_release(increment, host_->clock()) ? it + 1 : erase(it, context);
		}
	}

	/// @brief The number of voices listed.
	/// @return The count.
	[[nodiscard]] size_t size() const { return voices_.size(); }
	/// @brief Whether no voice is listed.
	/// @return True with none.
	[[nodiscard]] bool empty() const { return voices_.empty(); }

	/// @brief Calls @p fn with a const view of the voices, the render excluded.
	/// @param fn Called once with the list; it reads the voices and may not keep the view.
	/// @return What @p fn returns.
	template <class Fn>
	decltype(auto) read(Fn&& fn) const {
		const Exclusion excluded;
		return std::forward<Fn>(fn)(std::as_const(voices_));
	}

	/// @brief Calls @p fn with each voice, to change it in place (not the list), the render excluded.
	/// @param fn Called with each slot in list order; it may change the voice but not add or remove one.
	template <class Fn>
	void for_each(Fn fn) {
		const Exclusion excluded;
		for (Slot* slot : voices_) {
			fn(*slot);
		}
	}

	/// @brief The rating of the voice the cull would take: the highest, the first of equals.
	/// @param priority The sound's voice priority, which the rating's top bits weigh.
	/// @param skip_cull_fading Leaves out voices already fading at the cull rate (terminate and force-release do; kill
	///        does not).
	/// @return The rating, or none when no voice qualifies.
	/// @note A host comparing sounds holds its own exclusion across this and the cull() that follows, so the pick
	///       cannot change between.
	[[nodiscard]] std::optional<uint32_t> cull_rating(plugin::VoicePriority priority, bool skip_cull_fading) const {
		const Exclusion excluded;
		const Slot* best = highest(priority, skip_cull_fading);
		return best != nullptr ? std::optional{best->kernel.priority_rating(priority, voices_.size())} : std::nullopt;
	}

	/// @brief Culls the voice cull_rating() names, freeing it when the cull stops it: its resources, then its slot
	/// while
	///        it is still listed (the host's release may ask whether the sound has voices left, and the voice culled
	///        still counts).
	/// @param priority The sound's voice priority.
	/// @param kind How it is culled; every kind but IMMEDIATE passes over voices already cull-fading.
	/// @param increment The release increment, for FAST_RELEASE.
	void cull(plugin::VoicePriority priority, synth::CullKind kind, uint32_t increment) {
		const Exclusion excluded;
		const bool skip = kind != synth::CullKind::IMMEDIATE;
		auto it = std::ranges::find(voices_, highest(priority, skip));
		if (it == voices_.end()) {
			return;
		}
		Slot& slot = **it;
		bool going = false;
		switch (kind) {
		case synth::CullKind::IMMEDIATE:
			going = slot.kernel.do_immediate_release(host_->clock());
			break;
		case synth::CullKind::FAST_RELEASE:
			going = slot.kernel.do_fast_release(increment, host_->clock());
			break;
		case synth::CullKind::SPEED_UP:
			going = slot.kernel.speed_up_release(host_->clock());
			break;
		}
		if (!going) {
			host_->unassign(slot, Context{});
			erase_if([&](const Slot* listed) { return listed == &slot; });
		}
	}

private:
	/// @brief The first voice of the highest rating, by a strict comparison, as the host's cull compares.
	/// @param priority The sound's voice priority.
	/// @param skip_cull_fading Leaves out voices already fading at the cull rate.
	/// @return The voice, or null when none qualifies.
	[[nodiscard]] const Slot* highest(plugin::VoicePriority priority, bool skip_cull_fading) const {
		const Slot* best = nullptr;
		uint32_t best_rating = 0;
		for (const Slot* slot : voices_) {
			if (skip_cull_fading && slot->kernel.is_cull_fading()) {
				continue;
			}
			const uint32_t rating = slot->kernel.priority_rating(priority, voices_.size());
			if (best == nullptr || best_rating < rating) {
				best = slot;
				best_rating = rating;
			}
		}
		return best;
	}

	H* host_;
	List voices_;
};

} // namespace deluge::instrument
