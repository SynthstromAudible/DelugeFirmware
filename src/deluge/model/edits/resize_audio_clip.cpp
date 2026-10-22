#include "model/edits/resize_audio_clip.h"

#include "deluge-project/setting_group.h"
#include "model/action/action.h"
#include "model/action/action_logger.h"
#include "model/clip/audio_clip.h"
#include "model/consequence/consequence.h"
#include "model/consequence/consequence_clip_length.h"
#include "model/edits/set_clip_length.h"
#include "model/edits/set_setting.h"
#include "model/model_stack.h"
#include "model/sample/sample.h"
#include "model/sample/sample_recorder.h"
#include "model/song/song.h"
#include "playback/mode/playback_mode.h"
#include "playback/playback_handler.h"

#include <algorithm>
#include <cstdlib>
#include <string>

// Song's gate on the re-sync a length change does: lowered across the undo of a shorten that a lengthen replaces.
extern bool allowResyncingDuringClipLengthChange;

namespace deluge::edits {

namespace {

using Marker = ConsequenceClipLength::Marker;

/// The steps of one resize, over the clip it resolved.
class Resize {
public:
	Resize(Context& ctx, project::ClipAddress address, ::AudioClip& clip) : ctx_(ctx), address_(address), clip_(clip) {}

	/// The sample the clip plays: the take while it records linearly, else its file.
	[[nodiscard]] ::Sample* playedSample() const {
		if (clip_.getCurrentlyRecordingLinearly()) {
			return clip_.recorder != nullptr ? clip_.recorder->sample : nullptr;
		}
		return static_cast<::Sample*>(clip_.sampleHolder.audioFile);
	}

	/// The sample the playback guide holds, which a length change without timestretching measures against.
	[[nodiscard]] ::Sample* guideSample() const {
		auto* holder = static_cast<SampleHolder*>(clip_.guide.audioFileHolder);
		return holder != nullptr ? static_cast<::Sample*>(holder->audioFile) : nullptr;
	}

	/// Drag the sample's end (its start, reversed) to make the clip @p newLength long. A missing @p sample clamps
	/// nothing.
	void sampleLength(const ::Sample* sample, int32_t newLength, int32_t oldLength, uint64_t oldLengthSamples) {
		if (oldLength == 0) {
			return;
		}
		const uint64_t newLengthSamples =
		    static_cast<uint64_t>(oldLengthSamples * static_cast<uint64_t>(newLength) + (oldLength >> 1))
		    / static_cast<uint32_t>(oldLength);
		int64_t newPos = 0;
		Marker marker = Marker::END;
		uint64_t oldValue = 0;
		if (clip_.sampleControls.isCurrentlyReversed()) {
			newPos = std::max<int64_t>(
			    static_cast<int64_t>(clip_.sampleHolder.endPos) - static_cast<int64_t>(newLengthSamples), 0);
			marker = Marker::START;
			oldValue = clip_.sampleHolder.startPos;
		}
		else {
			newPos = static_cast<int64_t>(clip_.sampleHolder.startPos) + static_cast<int64_t>(newLengthSamples);
			if (sample != nullptr) {
				newPos = std::min<int64_t>(newPos, static_cast<int64_t>(sample->lengthInSamples));
				// Snap to the loop marker written in the file, within ten samples of it.
				if (sample->fileLoopStartSamples != 0
				    && std::abs(newPos - static_cast<int64_t>(sample->fileLoopStartSamples)) < 10) {
					newPos = static_cast<int64_t>(sample->fileLoopStartSamples);
				}
			}
			oldValue = clip_.sampleHolder.endPos;
		}
		const ActionType type =
		    newLength < oldLength ? ActionType::CLIP_LENGTH_DECREASE : ActionType::CLIP_LENGTH_INCREASE;
		markedLength(marker, newPos, oldValue, static_cast<uint32_t>(newLength), type);
	}

	/// Drag the sample's start (its end, reversed) to start the clip at @p newStartTicks.
	void sampleStart(const ::Sample& sample, int32_t newStartTicks, int32_t oldLength, uint64_t oldLengthSamples) {
		if (oldLength == 0) {
			return;
		}
		const int32_t newLengthTicks = std::max(oldLength - newStartTicks, 1);
		const uint64_t newLengthSamples =
		    static_cast<uint64_t>(oldLengthSamples * static_cast<uint64_t>(newLengthTicks) + (oldLength / 2))
		    / static_cast<uint32_t>(oldLength);
		Marker marker = Marker::START;
		int64_t newPos = 0;
		uint64_t oldValue = 0;
		if (clip_.sampleControls.isCurrentlyReversed()) {
			oldValue = clip_.sampleHolder.endPos;
			newPos = static_cast<int64_t>(
			    std::min<uint64_t>(clip_.sampleHolder.startPos + newLengthSamples, sample.lengthInSamples));
			marker = Marker::END;
		}
		else {
			oldValue = clip_.sampleHolder.startPos;
			newPos = std::max<int64_t>(static_cast<int64_t>(clip_.sampleHolder.endPos - newLengthSamples), 0);
		}
		const ActionType type =
		    newLengthTicks < oldLength ? ActionType::CLIP_LENGTH_DECREASE : ActionType::CLIP_LENGTH_INCREASE;
		markedLength(marker, newPos, oldValue, static_cast<uint32_t>(newLengthTicks), type);
	}

	/// Lengthen or shorten the clip to @p newLength, the sample timestretched to fill it.
	/// @return The undo step it was logged in, or null.
	::Action* length(int32_t newLength, int32_t oldLength) {
		if (newLength > oldLength) {
			if (newLength <= static_cast<int32_t>(kMaxSequenceLength)) {
				return lengthen(newLength);
			}
		}
		else if (newLength < oldLength && newLength > 0) {
			return shorten(newLength);
		}
		return nullptr;
	}

private:
	/// Move @p marker to @p newPos, then the length, as one undo step whose length consequence also restores the
	/// marker to @p oldValue. The marker moves first: the length change reads it.
	void markedLength(Marker marker, int64_t newPos, uint64_t oldValue, uint32_t newLength, ActionType type) {
		SetSetting move{{project::SettingGroupId::AUDIO_CLIP, address_},
		                marker == Marker::START ? "startSamplePos" : "endSamplePos",
		                serialization::Value::makeNumber(newPos)};
		(void)deluge::edits::apply(ctx_, move);
		::Action* action = actionLogger.getNewAction(type, ActionAddition::NOT_ALLOWED);
		setLength(newLength, action);
		if (action != nullptr) {
			if (action->firstConsequence != nullptr && action->firstConsequence->type == Consequence::CLIP_LENGTH) {
				auto* consequence = static_cast<ConsequenceClipLength*>(action->firstConsequence);
				consequence->audioClip = &clip_;
				consequence->marker = marker;
				consequence->markerValueToRevertTo = oldValue;
			}
			actionLogger.closeAction(type);
		}
	}

	void setLength(uint32_t newLength, ::Action* action) {
		SetClipLength edit{address_, newLength, action, &clip_};
		(void)deluge::edits::apply(ctx_, edit);
	}

	/// A lengthen right after a shorten of this clip undoes the shorten first; a paste being built absorbs it.
	::Action* lengthen(int32_t newLength) {
		::Action* action = nullptr;
		::Action* const first = actionLogger.firstAction[BEFORE];
		const bool undoing = first != nullptr && first->openForAdditions
		                     && first->type == ActionType::CLIP_LENGTH_DECREASE && first->currentClip == &clip_;
		if (undoing) {
			allowResyncingDuringClipLengthChange = false;
			(void)actionLogger.revert(BEFORE, false, false);
			allowResyncingDuringClipLengthChange = true;
		}
		if (clip_.loopLength() != newLength) {
			ActionType type =
			    newLength < clip_.loopLength() ? ActionType::CLIP_LENGTH_DECREASE : ActionType::CLIP_LENGTH_INCREASE;
			::Action* const nowFirst = actionLogger.firstAction[BEFORE];
			if (nowFirst != nullptr && nowFirst->type == ActionType::PATTERN_PASTE) {
				type = ActionType::PATTERN_PASTE;
			}
			action = actionLogger.getNewAction(type, ActionAddition::ALLOWED);
			if (action != nullptr && action->currentClip != &clip_) {
				action = actionLogger.getNewAction(type, ActionAddition::NOT_ALLOWED);
			}
			setLength(static_cast<uint32_t>(newLength), action);
		}
		else if (undoing && playbackHandler.isEitherClockActive()) {
			// The revert produced the length without the re-sync it would have done.
			char memory[MODEL_STACK_MAX_SIZE];
			currentPlaybackMode->reSyncClip(setupModelStackWithTimelineCounter(memory, &ctx_.song, &clip_));
		}
		return action;
	}

	/// Successive shortens of one clip coalesce into one undo step, which records the original length once.
	::Action* shorten(int32_t newLength) {
		::Action* action = nullptr;
		::Action* const first = actionLogger.firstAction[BEFORE];
		if (first != nullptr && first->type == ActionType::PATTERN_PASTE) {
			action = actionLogger.getNewAction(ActionType::PATTERN_PASTE, ActionAddition::ALLOWED);
		}
		else {
			action = actionLogger.getNewAction(ActionType::CLIP_LENGTH_DECREASE, ActionAddition::ALLOWED);
			if (action != nullptr && action->currentClip != &clip_) {
				action = actionLogger.getNewAction(ActionType::CLIP_LENGTH_DECREASE, ActionAddition::NOT_ALLOWED);
			}
		}
		setLength(static_cast<uint32_t>(newLength), action);
		return action;
	}

	Context& ctx_;
	project::ClipAddress address_;
	::AudioClip& clip_;
};

} // namespace

Error ResizeAudioClip::apply(Context& ctx) {
	::Clip* found = ctx.song.clipForId(clip_.clip_id);
	if (found == nullptr || found->type != ClipType::AUDIO) {
		return Error::UNSPECIFIED;
	}
	auto& clip = *static_cast<::AudioClip*>(found);
	Resize resize{ctx, clip_, clip};
	const int32_t oldLength = clip.loopLength();
	const auto oldLengthSamples = static_cast<uint64_t>(clip.sampleHolder.getDurationInSamples(true));

	switch (kind_) {
	case Kind::END:
		resize.sampleLength(resize.playedSample(), new_length_, oldLength, oldLengthSamples);
		return Error::NONE;
	case Kind::START: {
		const ::Sample* sample = resize.playedSample();
		if (sample == nullptr) {
			return Error::UNSPECIFIED;
		}
		resize.sampleStart(*sample, new_length_, oldLength, oldLengthSamples);
		return Error::NONE;
	}
	case Kind::STRETCH:
		length_action_ = resize.length(new_length_, old_length_);
		return Error::NONE;
	case Kind::NO_STRETCH:
		// The sample's end moves to whatever length the clip gets, so a length no clip may have is refused whole.
		if (new_length_ <= 0 || new_length_ > static_cast<int32_t>(kMaxSequenceLength)) {
			return Error::UNSPECIFIED;
		}
		length_action_ = resize.length(new_length_, old_length_);
		if (const ::Sample* sample = resize.guideSample(); sample != nullptr) {
			resize.sampleLength(sample, new_length_, oldLength, oldLengthSamples);
		}
		return Error::NONE;
	case Kind::TO_SAMPLE_LENGTH: {
		auto* holder = static_cast<SampleHolder*>(clip.guide.audioFileHolder);
		if (holder == nullptr) {
			return Error::UNSPECIFIED;
		}
		length_action_ = resize.length(holder->getLoopLengthAtSystemSampleRate(true), oldLength);
		return Error::NONE;
	}
	}
	return Error::UNSPECIFIED;
}

} // namespace deluge::edits
