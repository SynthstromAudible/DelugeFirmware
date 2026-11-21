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

#include "instruments/deluge/words.h"
#include "plugin-api/types/voice_priority.h"
#include "synth/voice/voice_host.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <ranges>
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
	bool unassigned{false};   ///< Its resources are freed; the render's sweep takes it out of the list
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
			free(slot, Context{});
		}
	}

	/// @brief A note-on's voice half: a MONO or LEGATO sound's reuse or glide of a voice it plays, else a new voice,
	///        the voice limit's steal and the budget's admit; with no memory, one of the sound's own voices restarted.
	/// @param config The sound's config, handed to the kernel.
	/// @param facts What the note reads of the sound; read only when the sound is not POLY and has voices.
	/// @param event The note, handed to the kernel; its envelope reset is set here.
	/// @param sample_sync_length A time-stretched sample's sync length.
	/// @param ticks_late How late the note starts, in ticks.
	/// @param context What the host reassesses and frees with.
	/// @return False when the note cannot sound at all (no memory, and no voice of the sound's to take).
	template <class Config>
	requires synth::note_host<H>
	bool note_on(const Config& config, const synth::NoteFacts& facts, H::Event event, uint32_t sample_sync_length,
	             int32_t ticks_late, Context context) {
		const bool poly = config.voicing.polyphony == PolyphonyMode::POLY;
		std::optional<Exclusion> non_poly; // reuse and glide act on voices the render plays
		if (!poly) {
			non_poly.emplace();
		}
		const Found found = poly || voices_.empty() ? Found{} : find_reusable(config, facts, context);
		if (config.voicing.polyphony == PolyphonyMode::LEGATO && found.legato != nullptr) {
			found.legato->kernel.change_note(host_->prepare_change(*found.legato, context, event.note_after_arp),
			                                 config, event.note_before_arp, event.note_after_arp, event.midi_channel,
			                                 event.mpe);
			return true;
		}
		if (found.reuse != nullptr) {
			event.reset_envelopes = false;
			retrigger(*found.reuse, config, event, sample_sync_length, ticks_late, context);
			return true;
		}
		event.reset_envelopes = true;
		Slot* voice = host_->acquire();
		if (voice == nullptr) {
			return restart_highest(config, event, sample_sync_length, ticks_late, context);
		}
		start_new(*voice, config, event, sample_sync_length, ticks_late, context);
		return true;
	}

	/// @brief A note held under a released one, which a LEGATO or MONO sound falls back to.
	struct HeldNote {
		int32_t note;                ///< Its note code
		int32_t midi_channel;        ///< The channel it came on
		decltype(H::Event::mpe) mpe; ///< Its MPE values, as the kernel's change_note takes them
	};

	/// @brief A note-off's voice half: each voice playing @p note_code and not yet releasing is released, or, for a
	///        LEGATO sound falling back to @p held, glides to it.
	/// @param config The sound's config, handed to the kernel.
	/// @param facts What the note-off reads of the sound.
	/// @param note_code The note released.
	/// @param all_notes Every voice matches, whatever its note.
	/// @param held The note to fall back to, when the sound falls back and one is held.
	/// @param context What the host prepares the release and the glide with.
	/// @return The held note a MONO sound must start as a new note-on, or none.
	template <class Config>
	requires synth::note_host<H>
	std::optional<HeldNote> note_off(const Config& config, const synth::NoteFacts& facts, int32_t note_code,
	                                 bool all_notes, std::optional<HeldNote> held, Context context) {
		const bool falls_back = !facts.arpeggiating && facts.falls_back && held.has_value();
		for (Slot* voice : voices_) {
			if ((voice->kernel.note_code() != note_code && !all_notes) || voice->kernel.releasing()) {
				continue;
			}
			if (falls_back) {
				if (config.voicing.polyphony != PolyphonyMode::LEGATO) {
					return held; // MONO: the held note starts as a new note-on
				}
				voice->kernel.change_note(host_->prepare_change(*voice, context, held->note), config, held->note,
				                          held->note, held->midi_channel, held->mpe);
				host_->last_note(held->note);
				continue;
			}
			const auto release = host_->prepare_release(*voice, context, facts.note_tails_allowed);
			voice->kernel.note_off(release, true, host_->clock());
			host_->note_released(*voice);
		}
		return std::nullopt;
	}

private:
	/// @brief What a non-POLY note-on's walk found: a voice to re-trigger, or a voice to glide.
	struct Found {
		Slot* reuse{};  ///< A voice whose sources were unassigned, to re-trigger
		Slot* legato{}; ///< A voice not releasing, for a LEGATO sound to glide
	};

	/// @brief A non-POLY note-on's walk: the first voice to glide, else the first to unassign and reuse, the rest of
	///        those freed.
	template <class Config>
	Found find_reusable(const Config& config, const synth::NoteFacts& facts, Context context) {
		Found found;
		for (auto it = voices_.begin(); it != voices_.end();) {
			Slot& voice = **it;
			if (config.voicing.polyphony != PolyphonyMode::MONO && !voice.kernel.releasing()
			    && facts.note_tails_allowed) {
				found.legato = &voice;
				break;
			}
			if (!needs_unassign(voice, config, facts)) {
				++it;
				continue;
			}
			if (found.reuse != nullptr) {
				it = erase(it, context); // already found a voice to reuse: discard the rest
				continue;
			}
			voice.kernel.unassign_sources(config.voicing.unison.count, false);
			found.reuse = &voice;
			++it;
		}
		return found;
	}

	/// @brief Whether a non-POLY note-on takes @p voice over: FM, or an active source that is not a sample, or still
	///        sounding after a fast release (asked only when neither, and only of a voice not already in one).
	template <class Config>
	bool needs_unassign(Slot& voice, const Config& config, const synth::NoteFacts& facts) {
		const auto plays_non_sample = [&](size_t s) {
			return facts.source_active_now[s] && config.sources[s].osc_type != OscType::SAMPLE;
		};
		return facts.fm
		       || std::ranges::any_of(std::views::iota(size_t{0}, facts.source_active_now.size()), plays_non_sample)
		       || (!voice.kernel.fast_releasing()
		           && !voice.kernel.do_fast_release(host_->steal_increment(), host_->clock()));
	}

	/// @brief Re-triggers @p voice, whose envelopes resume from where they are; a failed note frees it.
	template <class Config>
	void retrigger(Slot& voice, const Config& config, const H::Event& event, uint32_t sample_sync_length,
	               int32_t ticks_late, Context context) {
		const auto levels = voice.kernel.envelope_levels();
		host_->voice_starting();
		if (start(voice, config, event, sample_sync_length, ticks_late, context)) {
			voice.kernel.resume_attack(levels);
		}
		else {
			free(voice, context);
		}
	}

	/// @brief With no memory for a new voice, starts the note on the sound's highest voice, rebuilt in place.
	/// @return False when the sound has no voice to take.
	template <class Config>
	bool restart_highest(const Config& config, const H::Event& event, uint32_t sample_sync_length, int32_t ticks_late,
	                     Context context) {
		const Exclusion excluded; // the steal plays a voice the render plays
		if (voices_.empty()) {
			return false;
		}
		Slot& stolen = *highest(config.voicing.priority, false);
		host_->unassign(stolen, Context{});
		host_->renew(stolen);
		host_->voices_changed(context);
		stolen.kernel.randomize_osc_phases(config, host_->noise());
		host_->voice_starting();
		if (!start(stolen, config, event, sample_sync_length, ticks_late, context)) {
			free(stolen, context);
		}
		return true;
	}

	/// @brief Starts the note on @p voice, new and not listed, then lists it: past the voice limit with the highest
	///        voice fast-released, else admitted at its cost; a failed note releases it unlisted.
	template <class Config>
	void start_new(Slot& voice, const Config& config, const H::Event& event, uint32_t sample_sync_length,
	               int32_t ticks_late, Context context) {
		voice.kernel.randomize_osc_phases(config, host_->noise());
		host_->voice_starting();
		const uint32_t began = host_->cycles();
		const bool started = start(voice, config, event, sample_sync_length, ticks_late, context);
		const uint32_t spent = host_->cycles() - began;
		if (!started) {
			host_->release(voice); // never listed: the render never saw it
			return;
		}
		const Exclusion excluded; // the limit, the push and the skipping status in one step the render cannot split
		const auto counts = [](const Slot* listed) { return !listed->kernel.is_cull_fading(); };
		if (std::ranges::count_if(voices_, counts) >= config.voicing.max_voices) {
			// A voice for a voice, so the block's cost stays
			cull(config.voicing.priority, synth::CullKind::FAST_RELEASE, host_->steal_increment());
		}
		else {
			host_->admit(spent);
		}
		publish(&voice);
		host_->voices_changed(context);
	}

	/// @brief The kernel's note-on on @p voice inside the host's bracket: its order, its setup, the note-on, the dump.
	/// @return Whether the note started.
	template <class Config>
	bool start(Slot& voice, const Config& config, const H::Event& event, uint32_t sample_sync_length,
	           int32_t ticks_late, Context context) {
		return host_->timed([&] {
			voice.order = host_->next_voice_order();
			const auto setup = host_->prepare(voice, context, event, sample_sync_length, ticks_late);
			const bool started =
			    voice.kernel.note_on(setup, config, event, host_->clock(), voices_.size(), host_->noise());
			host_->note_started(voice);
			return started;
		});
	}

	/// @brief Frees @p voice as the cull does: its resources, then its slot while it is still listed.
	void free(Slot& voice, Context context) {
		host_->unassign(voice, context);
		erase_if([&](const Slot* listed) { return listed == &voice; });
	}

	/// @brief The first voice of the highest rating, by a strict comparison, as the host's cull compares.
	/// @param priority The sound's voice priority.
	/// @param skip_cull_fading Leaves out voices already fading at the cull rate.
	/// @return The voice, or null when none qualifies.
	[[nodiscard]] Slot* highest(plugin::VoicePriority priority, bool skip_cull_fading) const {
		Slot* best = nullptr;
		uint32_t best_rating = 0;
		for (Slot* slot : voices_) {
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
