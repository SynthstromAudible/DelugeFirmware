#pragma once

/// @file
/// @brief The sample browser's preview: the file the cursor is on, loaded into the preview drum and sounded at the
/// target sound's volume. Shared by the legacy sample browser and the ported one, which draw it their own ways.

#include "processing/engines/audio_engine.h"
#include "storage/audio/latest_sample_load.h"

#include <cstdint>
#include <optional>
#include <string>

class InstrumentClip;
class ParamManagerForTimeline;
class Sound;

namespace deluge::gui::ui {

/// @brief The sound a previewed file is headed for, whose volume the preview matches: the sound editor's sound and
/// params, and for a kit the kit's clip, whose output volume folds in.
struct PreviewSound {
	Sound* sound = nullptr;
	ParamManagerForTimeline* params = nullptr;
	bool kit = false;
	InstrumentClip* clip = nullptr; ///< read only for a kit
};

/// @brief The volume a file will play at once it's loaded into @p target, so the preview can match it.
AudioEngine::PreviewVolume targetPreviewVolume(const PreviewSound& target);

/// @brief The preview's load and sound: the file asked for, its load (latest wins), and its sounding once loaded.
///
/// Runs on the interaction tier. A request is only recorded; the next tick starts its load, so a cursor that moves on
/// first replaces it at no cost, and a load already started for an earlier file is replaced when the new one starts.
/// Drawing is the caller's: tick() reports a load that landed.
class SamplePreviewer {
public:
	/// @brief The file to preview, captured when it was asked for.
	struct Target {
		std::string path;
		int32_t movementDirection = 0;
	};
	/// @brief A load that ended with the preview drum bound (or settled empty): what it was for, whether the drum
	/// holds a sample to draw, and the generation it landed in.
	struct Landed {
		Target target;
		bool hasSample = false;
		uint32_t generation = 0;
	};

	/// @param lastStarted Where each load's path is recorded as it starts (the browser's last file loaded), or null.
	explicit SamplePreviewer(std::string* lastStarted = nullptr) : lastStarted_(lastStarted) {}

	/// @brief Preview the file at @p path: record it, for the next tick to start its load.
	/// @param movementDirection Scroll direction for the caller's animation; 0 for none.
	void request(std::string path, int32_t movementDirection);

	/// @brief Abandon any preview in progress: nothing wanted or loading is wanted any more.
	///
	/// Clears the wanted file, drops the load, and moves the generation, so that a redraw still pending for the preview
	/// being left does nothing. Cancelling strands nothing: a dropped load's request stays with the loader, where a
	/// later lookup of the same path collects it, and a result nobody collects is evicted.
	void cancel();

	/// @brief Whether a preview is wanted and not yet started, or its load has not ended.
	[[nodiscard]] bool pending() const { return wanted_.has_value() || load_.pending(); }

	/// @brief Moves each time a load starts or the preview is cancelled; a draw from an earlier one is stale.
	[[nodiscard]] uint32_t generation() const { return generation_; }

	/// @brief How many previews gave up on a load that stopped moving.
	[[nodiscard]] int32_t giveUps() const { return giveUps_; }

	/// @brief One tick: start the wanted file's load, tick the load in hand, and act on its end.
	///
	/// A load that found its file, or settled without one, shows any lookup error and sounds the preview at
	/// @p sound's volume if the browser should. A load that stalled, or found the card unusable, gives up with the
	/// card error and reports nothing; nothing is asked for again until the next request.
	/// @return The load that landed this tick, if one did.
	std::optional<Landed> tick(const PreviewSound& sound);

private:
	/// @brief Start @p target's load, replacing any load in hand.
	///
	/// Records the path as the last one started, stops the preview sounding (which also unbinds its holder), and asks
	/// for the file in the preview drum's first range. Moves the generation.
	void start(Target target);
	/// @brief Act on the end of a load: report, sound and land it, or give up.
	std::optional<Landed> finish(deluge::storage::LatestSampleLoad<Target>::Outcome outcome, const PreviewSound& sound);

	std::string* lastStarted_;
	/// The file a request asked for, until the next tick starts its load.
	std::optional<Target> wanted_{};
	/// The load in hand. Holds the target it was started for.
	deluge::storage::LatestSampleLoad<Target> load_{};
	uint32_t generation_{0};
	int32_t giveUps_{0};
};

} // namespace deluge::gui::ui
