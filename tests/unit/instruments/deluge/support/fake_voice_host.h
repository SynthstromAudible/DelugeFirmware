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
#include "instruments/deluge/words.h"
#include "plugin/types/voice_priority.h"
#include "synth/voice/voice_clock.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

/// @file
/// @brief A kernel and a host for the voice pool's specs: ratings set by hand, every host call logged.

namespace deluge::instrument::test {

/// @brief The sound's config as the pool's note path reads it.
struct FakeConfig {
	struct {
		PolyphonyMode polyphony{PolyphonyMode::POLY};
		uint8_t max_voices{8};
		plugin::VoicePriority priority{plugin::VoicePriority::MEDIUM};
		struct {
			int32_t count{1};
		} unison;
	} voicing;
	struct Source {
		OscType osc_type{OscType::SAMPLE};
	};
	std::array<Source, 2> sources{};
};

/// @brief A note-on as the pool hands it to the kernel.
struct FakeEvent {
	int32_t note_before_arp{};
	int32_t note_after_arp{};
	int32_t midi_channel{};
	bool reset_envelopes{};
	std::span<const int16_t, 3> mpe;
};

/// @brief The setup a fake host prepares: the note it was prepared for.
struct FakeSetup {
	int32_t note{};
};

/// @brief The noise the kernel draws from; only passed through.
struct FakeNoise {};

/// @brief The block values the pool's render reads.
struct FakeValues {
	bool note_tails_allowed{};
};

/// @brief The render arguments the pool hands each voice.
struct FakeArgs {
	uint32_t sources_changed{};
};

/// @brief The matrix block the pool passes through.
struct FakeControl {};

/// @brief What a fake host builds for one voice's render.
struct FakeVoiceBlock {};

/// @brief The release a fake host prepares: whether note tails were allowed.
struct FakeRelease {
	bool tails{};
};

/// @brief A kernel whose rating, cull fade and release stage are set, that stops after a set number of releases, and
/// that logs its note path calls into its host's log.
struct FakeKernel {
	uint32_t rating{};
	bool fading{};
	int32_t releases_to_stop{1}; ///< Each release counts one down; at 0 the voice has stopped
	std::string last_release;    ///< The kind of the last release asked of it
	bool in_release{};           ///< releasing()
	bool in_fast_release{};      ///< fast_releasing()
	bool starts{true};           ///< What note_on() returns
	std::array<int32_t, 2> levels{};
	int32_t note{};                  ///< note_code()
	bool ignored_note_off{};         ///< previously_ignored_note_off()
	bool keeps{true};                ///< What render() returns
	std::vector<std::string>* log{}; ///< The host's log, set by acquire()

	[[nodiscard]] int32_t note_code() const { return note; }
	[[nodiscard]] bool previously_ignored_note_off() const { return ignored_note_off; }
	bool render(const FakeControl& /*control*/, const FakeConfig& /*config*/, const FakeValues& /*values*/,
	            const FakeVoiceBlock& /*block*/, const FakeArgs& args) {
		logged("render sources " + std::to_string(args.sources_changed));
		return keeps;
	}
	[[nodiscard]] bool releasing() const { return in_release; }
	void note_off(const FakeRelease& release, bool allow_release_stage, synth::VoiceClock /*clock*/) {
		logged("note_off tails " + std::to_string(int{release.tails}) + " stage "
		       + std::to_string(int{allow_release_stage}));
	}
	[[nodiscard]] bool fast_releasing() const { return in_fast_release; }
	[[nodiscard]] std::array<int32_t, 2> envelope_levels() const { return levels; }
	void resume_attack(const std::array<int32_t, 2>& from) {
		logged("resume " + std::to_string(from[0]) + " " + std::to_string(from[1]));
	}
	bool note_on(const FakeSetup& setup, const FakeConfig& /*config*/, const FakeEvent& event,
	             synth::VoiceClock /*clock*/, size_t count, FakeNoise& /*noise*/) {
		logged("note_on " + std::to_string(setup.note) + " reset " + std::to_string(int{event.reset_envelopes})
		       + " count " + std::to_string(count));
		return starts;
	}
	void change_note(const FakeSetup& setup, const FakeConfig& /*config*/, int32_t before, int32_t after,
	                 int32_t channel, std::span<const int16_t, 3> /*mpe*/) {
		logged("change " + std::to_string(setup.note) + " " + std::to_string(before) + " " + std::to_string(after)
		       + " ch " + std::to_string(channel));
	}
	void randomize_osc_phases(const FakeConfig& /*config*/, FakeNoise& /*noise*/) { logged("randomize"); }
	void unassign_sources(int32_t num_unison, bool deleting_song) {
		logged("unassign_sources " + std::to_string(num_unison) + " " + std::to_string(int{deleting_song}));
	}

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
	void logged(std::string line) {
		if (log != nullptr) {
			log->push_back(std::move(line));
		}
	}
};

/// @brief A host that allocates slots from the heap and logs each call, with the voices it saw listed.
struct FakeHost {
	using Slot = VoiceSlot<int, FakeKernel>; ///< The part is the voice's id
	using List = std::vector<Slot*>;
	using Context = int; ///< Logged with each unassign
	using Config = FakeConfig;
	using Event = FakeEvent;
	using Noise = FakeNoise;

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
	bool acquire_fails{};     ///< acquire() answers null: no memory
	bool starts{true};        ///< Each new voice's note_on() answer
	int32_t prepared_depth{}; ///< The exclusion depth the last prepare() ran at
	int32_t released_depth{}; ///< The exclusion depth the last prepare_release() ran at
	uint32_t next_sources{};  ///< sources_changed()'s counter
	uint32_t next_order{};    ///< next_voice_order()'s counter
	uint32_t cycle_count{};   ///< cycles() steps by 5 each call
	FakeNoise noise_stream{}; ///< noise()

	Slot* acquire() {
		if (acquire_fails) {
			log.push_back("acquire none");
			return nullptr;
		}
		auto* slot = new Slot{.part = next_id++};
		slot->kernel.log = &log;
		slot->kernel.starts = starts;
		log.push_back("acquire " + std::to_string(slot->part));
		return slot;
	}
	void unassign(Slot& slot, Context context) {
		log.push_back(seen("unassign", slot) + (context != Context{} ? " ctx " + std::to_string(context) : ""));
	}
	void release(Slot& slot) noexcept {
		log.push_back(seen("release", slot));
		delete &slot;
	}
	void admit(uint32_t cost) { log.push_back("admit " + std::to_string(cost)); }
	void voice_starting() { log.push_back("starting"); }
	void voices_changed(Context context) { log.push_back("changed ctx " + std::to_string(context)); }
	void note_edit() { log.push_back("edit depth " + std::to_string(Exclusion::depth)); }
	synth::VoiceClock clock() { return synth::VoiceClock{&next_tick}; }

	FakeSetup prepare(Slot& slot, Context /*context*/, const FakeEvent& event, uint32_t /*length*/, int32_t /*late*/) {
		prepared_depth = Exclusion::depth;
		log.push_back("prepare " + std::to_string(slot.part) + " note " + std::to_string(event.note_after_arp));
		return {.note = event.note_after_arp};
	}
	FakeSetup prepare_change(Slot& slot, Context /*context*/, int32_t note) {
		log.push_back("prepare_change " + std::to_string(slot.part) + " note " + std::to_string(note));
		return {.note = note};
	}
	void renew(Slot& slot) noexcept {
		log.push_back(seen("renew", slot));
		slot.kernel = FakeKernel{.starts = starts, .log = &log};
	}
	uint32_t next_voice_order() {
		log.push_back("order " + std::to_string(next_order));
		return next_order++;
	}
	uint32_t cycles() {
		log.push_back("cycles");
		return cycle_count += 5;
	}
	void note_started(Slot& slot) { log.push_back("started " + std::to_string(slot.part)); }
	FakeNoise& noise() { return noise_stream; }
	static uint32_t steal_increment() { return 128; }
	FakeRelease prepare_release(Slot& slot, Context /*context*/, bool tails) {
		released_depth = Exclusion::depth;
		log.push_back("prepare_release " + std::to_string(slot.part));
		return {.tails = tails};
	}
	void note_released(Slot& slot) { log.push_back("released " + std::to_string(slot.part)); }
	void last_note(int32_t note) { log.push_back("last_note " + std::to_string(note)); }
	FakeVoiceBlock voice_block(Slot& slot) {
		log.push_back("voice_block " + std::to_string(slot.part));
		return {};
	}
	uint32_t sources_changed() {
		log.push_back("sources " + std::to_string(next_sources));
		return next_sources++;
	}
	void voice_rendered(Slot& slot, const FakeControl& /*control*/, const FakeConfig& /*config*/,
	                    const FakeValues& /*values*/) {
		log.push_back("rendered " + std::to_string(slot.part));
	}
	template <class Fn>
	bool timed_voice(Fn&& fn, bool renders) {
		log.push_back("timed_voice " + std::to_string(int{renders}));
		return std::forward<Fn>(fn)();
	}
	template <class Fn>
	bool timed(Fn&& fn) {
		log.push_back("timed");
		return std::forward<Fn>(fn)();
	}

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
