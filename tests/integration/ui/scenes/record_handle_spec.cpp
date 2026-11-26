/// @file
/// @brief A recorded write finds its lane's working copy by the handle it was made with, never a later copy's.

#include "model/clip/instrument_clip.h"
#include "model/project_bridge/lane_target.h"
#include "model/project_bridge/param_ids.h"
#include "model/project_bridge/param_stack.h"
#include "model/song/song.h"
#include "modulation/automation/record_events.h"
#include "modulation/automation/working_copies.h"
#include "modulation/params/param.h"
#include "playback/playback_handler.h"
#include "processing/sound/sound_instrument.h"

#include <optional>

#include "support/song_fixture.h"

#include "cppspec.hpp"
#include "support/spec_main.h"

namespace spec = deluge::ui::scenes::spec;
namespace params = deluge::modulation::params;
namespace bridge = deluge::bridge;
namespace wc = deluge::modulation;

namespace {

/// @brief The synth fixture's filter-frequency lane in its clip.
std::optional<bridge::LaneTarget> filter_lane(spec::SongFixture& f) {
	const deluge::project::ParamAddress address =
	    bridge::addressOf(deluge::project::OwnerId::instrument(*f.instrument->id()), params::Kind::PATCHED,
	                      params::LOCAL_LPF_FREQ, f.clip->id());
	return bridge::lane_exact(*f.song, address);
}

} // namespace

// clang-format off
describe record_handle("a working copy's handle", ${
	it("names its copy, and the same copy when made again", _ {
		spec::SongFixture f = spec::make_synth_song();
		spec::make_current(f.song);
		const std::optional<bridge::LaneTarget> lane = filter_lane(f);
		expect(lane.has_value()).to_be_true();
		if (lane) {
			const wc::WorkingCopy made = wc::makeWorkingCopy(*lane, wc::kNodesPerRecordedWrite);
			expect(made.copy).not_().to_be_null();
			expect(made.handle.generation).not_().to_equal(0u, "a made copy has a generation");
			expect(wc::copy_at(made.handle)).to_equal(made.copy);
			const wc::WorkingCopy again = wc::makeWorkingCopy(*lane, wc::kNodesPerRecordedWrite);
			expect(again.copy).to_equal(made.copy, "a second make finds it");
			expect(again.handle.generation).to_equal(made.handle.generation, "under the same handle");
			wc::releaseWorkingCopies();
			(void)wc::takeEndedSinceLastAsked(); // no later commit takes the release for a take's end
		}
		spec::make_current(nullptr);
	});

	it("names nothing once its copy is released, and never the slot's next copy", _ {
		spec::SongFixture f = spec::make_synth_song();
		spec::make_current(f.song);
		const std::optional<bridge::LaneTarget> lane = filter_lane(f);
		expect(lane.has_value()).to_be_true();
		if (lane) {
			const wc::WorkingCopy first = wc::makeWorkingCopy(*lane, wc::kNodesPerRecordedWrite);
			expect(first.copy).not_().to_be_null();
			wc::releaseWorkingCopies();
			(void)wc::takeEndedSinceLastAsked(); // no later commit takes the release for a take's end
			expect(wc::copy_at(first.handle)).to_be_null();
			const wc::WorkingCopy second = wc::makeWorkingCopy(*lane, wc::kNodesPerRecordedWrite);
			expect(int(second.handle.index)).to_equal(int(first.handle.index), "the slot is reused");
			expect(wc::copy_at(first.handle)).to_be_null();
			expect(wc::copy_at(second.handle)).to_equal(second.copy);
			wc::releaseWorkingCopies();
			(void)wc::takeEndedSinceLastAsked(); // no later commit takes the release for a take's end
		}
		spec::make_current(nullptr);
	});

	it("carries a value recorded through the setter into its lane's copy", _ {
		spec::SongFixture f = spec::make_synth_song();
		spec::make_current(f.song);
		const std::optional<bridge::LaneTarget> lane = filter_lane(f);
		expect(lane.has_value()).to_be_true();
		if (lane) {
			const uint8_t savedState = playbackHandler.playbackState;
			const RecordingMode savedRecording = playbackHandler.recording;
			const bool savedActive = f.clip->activeIfNoSolo;
			const bool savedArmed = f.clip->armedForRecording;
			playbackHandler.playbackState = PLAYBACK_CLOCK_INTERNAL_ACTIVE;
			playbackHandler.recording = RecordingMode::NORMAL;
			f.clip->activeIfNoSolo = true;
			f.clip->armedForRecording = true;
			constexpr int32_t kValue = 500000000;
			const int32_t livePos = f.clip->loopLength() / 4;
			lane->auto_param->setCurrentValueInResponseToUserInput(kValue, *lane, true, livePos, true, false);
			wc::applyRecordEvents(*f.song);
			const wc::WorkingCopy copy = wc::find_working_copy(*lane->collection, lane->param_id);
			expect(copy.copy).not_().to_be_null();
			bool found = false;
			if (copy.copy != nullptr) {
				for (const auto& node : copy.copy->nodes) {
					found = found || (node.pos == livePos && node.value == kValue);
				}
			}
			expect(found).to_be_true();
			playbackHandler.recording = RecordingMode::OFF;
			lane->auto_param->cancelOverriding(); // the recorded write latched the lane
			wc::releaseWorkingCopies();
			(void)wc::takeEndedSinceLastAsked(); // no later commit takes the release for a take's end
			f.clip->activeIfNoSolo = savedActive;
			f.clip->armedForRecording = savedArmed;
			playbackHandler.recording = savedRecording;
			playbackHandler.playbackState = savedState;
		}
		spec::make_current(nullptr);
	});

	it("drops a write queued for a released copy, leaving the slot's next copy as it was made", _ {
		spec::SongFixture f = spec::make_synth_song();
		spec::make_current(f.song);
		const std::optional<bridge::LaneTarget> lane = filter_lane(f);
		expect(lane.has_value()).to_be_true();
		if (lane) {
			const wc::WorkingCopy first = wc::makeWorkingCopy(*lane, wc::kNodesPerRecordedWrite);
			wc::releaseWorkingCopies();
			(void)wc::takeEndedSinceLastAsked();
			const wc::WorkingCopy second = wc::makeWorkingCopy(*lane, wc::kNodesPerRecordedWrite);
			expect(second.copy).not_().to_be_null();
			const size_t nodesBefore = second.copy != nullptr ? second.copy->nodes.size() : 0;
			const int32_t length = f.clip->loopLength();
			const wc::RecordEvent stale{.song = f.song,
			                            .copy = first.handle,
			                            .livePos = length / 4,
			                            .value = 500000000,
			                            .ticksToClear = 1,
			                            .effectiveLength = length,
			                            .posAtWhichPlaybackWillCut = 2147483647,
			                            .timePerInternalTick = 1};
			expect(wc::postRecordEvent(stale)).to_be_true();
			wc::applyRecordEvents(*f.song);
			if (second.copy != nullptr) {
				expect(second.copy->nodes.size()).to_equal(nodesBefore, "the later copy took no write");
			}
			wc::releaseWorkingCopies();
			(void)wc::takeEndedSinceLastAsked();
		}
		spec::make_current(nullptr);
	});

	it("names nothing when it names no copy", _ {
		expect(wc::copy_at(wc::CopyHandle{})).to_be_null();
		expect(wc::copy_at(wc::CopyHandle{.index = 200, .generation = 1})).to_be_null();
	});
});
// clang-format on

CPPSPEC_SPEC(record_handle)
