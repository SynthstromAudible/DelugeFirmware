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

#include "definitions_cxx.hpp"
#include "storage/audio/audio_load_context.h"
#include "storage/audio/sample_load_walk.h"

#include <cstdint>
#include <string_view>
#include <variant>
#include <vector>

class AudioClip;
class AudioFileHolder;
class Drum;
class Output;
class Song;

namespace deluge::storage {

/// @brief A sample load a consumer ticks until it ends: every demand of a song, an output, a drum, an audio clip or
///        one holder, found through the async loader, with no sample read from the card on the calling tier.
///
/// Each tick() walks the root again, and each demand settles one way:
/// - a holder that already has a file, or names none, is left as it is;
/// - a sample is asked of the loader (AsyncLoadMode::AsyncOnly): a file that has arrived is bound and its after-bind
///   step run; a file still loading is asked for again on the next tick; any other answer leaves the holder empty,
///   remembered so it is not asked for again;
/// - a wavetable is looked up synchronously, on the calling tier, once the previous tick's walk left no sample in
///   flight. Its lookup may probe the load's folder on a miss, which must not happen before the samples have asked.
///
/// The load ends Loaded when a walk leaves nothing in flight, and Failed when the user aborts it or the card cannot be
/// read; holders bound before then stay bound. The end sticks: later ticks return it without walking.
///
/// The consumer owns the object, the root and the context, keeps all three alive while ticking, and does not change
/// the root's tree between ticks. Dropping the object cancels the load; a request still in flight stays with the
/// loader, where a later lookup collects it or an eject discards it.
class AsyncSampleLoad final : private SampleLoadVisitor {
public:
	/// @brief Where a load is.
	enum class State : uint8_t {
		Loading, ///< Something is still in flight: tick again.
		Loaded,  ///< Every demand has settled.
		Failed,  ///< The load ended early: see Reason.
	};

	/// @brief Why a load ended early.
	enum class Reason : uint8_t {
		None,         ///< It did not.
		Aborted,      ///< The user aborted it: shouldAbortLoading() answered true at a checkpoint.
		CardUnusable, ///< The card cannot be read (ejected, disabled or not mounted), or no async loader backs loads.
	};

	/// @brief What a tick reports.
	struct Progress {
		State state = State::Loading;
		Reason reason = Reason::None;
		int32_t bound = 0;          ///< Holders the last walk found with a file.
		int32_t emptyHolders = 0;   ///< Holders the last walk found settled with no file.
		int32_t inFlight = 0;       ///< Demands the last walk left still loading.
		int32_t wavetableReads = 0; ///< Synchronous wavetable lookups, over the whole load.
		int32_t ticks = 0;          ///< Ticks that walked.
	};

	/// @brief Every sample a song needs: each output's, then each audio clip's.
	[[nodiscard]] static AsyncSampleLoad forSong(Song& song, AudioLoadContext* context);
	/// @brief Only what a song's active clips need to sound (Song::walkCrucialSampleDemands()).
	[[nodiscard]] static AsyncSampleLoad forCrucialSong(Song& song, AudioLoadContext* context);
	/// @brief Every sample an output's sounds need. A kit or synth reached under no context also looks in its own
	///        folder, as a load that may read the card does.
	[[nodiscard]] static AsyncSampleLoad forOutput(Output& output, AudioLoadContext* context);
	/// @brief Every sample a drum's sound needs.
	[[nodiscard]] static AsyncSampleLoad forDrum(Drum& drum, AudioLoadContext* context);
	/// @brief An audio clip's sample, with the clip named after it.
	[[nodiscard]] static AsyncSampleLoad forAudioClip(AudioClip& clip, AudioLoadContext* context);
	/// @brief One demand, with no walk around it.
	[[nodiscard]] static AsyncSampleLoad forHolder(const SampleDemand& demand, AudioLoadContext* context);

	/// @brief Walk the root once more and settle what can be settled. Runs on the calling tier.
	/// @return The load's progress after this walk; once the load has ended, that end.
	Progress tick();

	/// @brief What the last tick() returned.
	[[nodiscard]] const Progress& progress() const { return progress_; }

private:
	struct SongRoot {
		Song* song;
	};
	struct CrucialSongRoot {
		Song* song;
	};
	using Root = std::variant<SongRoot, CrucialSongRoot, Output*, Drum*, AudioClip*, SampleDemand>;

	AsyncSampleLoad(Root root, AudioLoadContext* context);

	Walk rangeCheckpoint(int32_t rangeIndex) override;
	Walk drumCheckpoint() override;
	void clipCheckpoint() override;
	void beginInstrumentScope(std::string_view dirPath, std::string_view name, ThingType thing,
	                          ScopeRule rule) override;
	void endInstrumentScope() override;
	void demand(const SampleDemand& demand) override;

	/// @brief Walk::Stop, remembering the abort, once the user has asked to abort the load.
	[[nodiscard]] Walk stopIfAborted();

	Root root_;
	/// The context the consumer passed.
	AudioLoadContext* callerContext_;
	/// The context each lookup runs under: callerContext_, or ownFolder_ while an instrument's scope holds it.
	AudioLoadContext* context_;
	/// The instrument folder a walk opened. Kept for the load's life, not one walk's, so what a lookup learnt about the
	/// folder survives to the next tick.
	AudioLoadContext ownFolder_{};
	/// Whether any walk has opened ownFolder_.
	bool ownFolderMade_{false};
	/// context_ as it was when ownFolder_ was opened in this walk.
	AudioLoadContext* contextOutsideOwnFolder_{nullptr};
	/// How many instrument scopes this walk has open.
	int32_t scopeDepth_{0};
	/// The scope depth ownFolder_ is open at in this walk, or -1 when it is not open.
	int32_t ownFolderDepth_{-1};
	/// Holders settled with no file. Never asked for again.
	std::vector<const AudioFileHolder*> settledEmpty_{};
	/// Whether the previous walk left no sample in flight. False before the first walk.
	bool samplesSettled_{false};
	/// Samples this walk left in flight.
	int32_t samplesInFlight_{0};
	/// Whether this walk met an abort.
	bool aborted_{false};
	Progress progress_{};
};

} // namespace deluge::storage
