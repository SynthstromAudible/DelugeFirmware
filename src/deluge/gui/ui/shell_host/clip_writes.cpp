#include "gui/ui/shell_host/shell_host.h"

#include "definitions_cxx.hpp"
#include "model/action/action.h"
#include "model/action/action_logger.h"
#include "model/clip/audio_clip.h"
#include "model/edits/clear_clip.h"
#include "model/edits/edit.h"
#include "model/edits/resize_audio_clip.h"
#include "model/edits/set_clip_colour.h"
#include "model/edits/set_setting.h"
#include "model/edits/shift_clip.h"
#include "model/model_stack.h"
#include "model/project_bridge/setting_projection.h"
#include "model/song/song.h"
#include "project/setting_group.h"
#include "serialization/value.h"
#include "storage/flash_storage.h"

#include <string>

namespace deluge::gui::shell_host {
namespace {

/// The clip edits, applied to the current song as Edits.
class HostClipWrites final : public deluge::ui::scenes::bind::ClipWrites {
public:
	bool recolour_clip(project::ClipAddress clip, int32_t offset) override {
		if (currentSong == nullptr) {
			return false;
		}
		edits::SetClipColour edit{clip, offset};
		return applied(edit);
	}

	bool clear_clip(project::ClipAddress clip, bool clear_automation, bool clear_sequence_and_mpe) override {
		if (currentSong == nullptr) {
			return false;
		}
		edits::ClearClip edit{clip, clear_automation, clear_sequence_and_mpe};
		return applied(edit);
	}

	bool move_audio_clip_marker(project::ClipAddress clip, std::string_view path, int64_t value) override {
		if (currentSong == nullptr) {
			return false;
		}
		::Clip* found = currentSong->clipForId(clip.clip_id);
		if (found == nullptr || found->type != ClipType::AUDIO) {
			return false;
		}
		// The marker editor's move: the record's row written with the voice taken off the sample, the moved part's
		// clusters claimed and a playing clip resumed. A write of the row by any other gesture does not reseat.
		bool taken = false;
		bridge::withVoiceReseated(*static_cast<::AudioClip*>(found), *currentSong, [&] {
			edits::SetSetting edit{project::SettingAddress{.group = project::SettingGroupId::AUDIO_CLIP, .owner = clip},
			                       std::string{path}, serialization::Value::makeNumber(value)};
			taken = applied(edit);
		});
		return taken;
	}

	bool resize_audio_clip(project::ClipAddress clip, deluge::ui::scenes::bind::ResizeKind kind, int32_t new_length,
	                       int32_t old_length) override {
		if (currentSong == nullptr) {
			return false;
		}
		using Kind = edits::ResizeAudioClip::Kind;
		using Request = deluge::ui::scenes::bind::ResizeKind;
		Kind edit_kind = Kind::END;
		switch (kind) {
		case Request::END:
			edit_kind = Kind::END;
			break;
		case Request::START:
			edit_kind = Kind::START;
			break;
		case Request::STRETCH:
			edit_kind = Kind::STRETCH;
			break;
		case Request::NO_STRETCH:
			edit_kind = Kind::NO_STRETCH;
			break;
		case Request::TO_SAMPLE_LENGTH:
			edit_kind = Kind::TO_SAMPLE_LENGTH;
			break;
		}
		edits::ResizeAudioClip edit{clip, edit_kind, new_length, old_length};
		resize_action_ = nullptr;
		if (!applied(edit)) {
			return false;
		}
		resize_action_ = edit.lengthAction();
		resize_clip_ = clip;
		return true;
	}

	void set_undo_scroll_after(project::ClipAddress clip, int32_t x_scroll) override {
		::Action* const step = resize_action_;
		resize_action_ = nullptr; // one view per length change
		if (step == nullptr || !(clip == resize_clip_)) {
			return;
		}
		// The step is the log's still, unless something since has replaced the log.
		for (::Action* action = actionLogger.firstAction[BEFORE]; action != nullptr; action = action->nextAction) {
			if (action == step) {
				action->xScrollClip[AFTER] = x_scroll;
				return;
			}
		}
	}
	void set_clip_navigation(int32_t x_scroll, uint32_t x_zoom) override {
		if (currentSong == nullptr) {
			return;
		}
		currentSong->persistentView.xScroll[NAVIGATION_CLIP] = x_scroll;
		currentSong->persistentView.xZoom[NAVIGATION_CLIP] = x_zoom;
	}

	bool shift_clip_horizontally(project::ClipAddress clip, int32_t amount) override {
		if (currentSong == nullptr) {
			return false;
		}
		::Clip* found = currentSong->clipForId(clip.clip_id);
		if (found == nullptr) {
			return false;
		}
		// `ClipView::horizontalEncoderAction`'s shift arm, from a clip view that is not the automation view: the
		// sequence and MPE always move, and the automation unless the device keeps that to the automation view.
		alignas(ModelStackWithAutoParam) char memory[MODEL_STACK_MAX_SIZE];
		ModelStackWithTimelineCounter* model_stack = currentSong->setupModelStackWithCurrentClip(memory);
		edits::ShiftClip edit{clip, amount, /*shiftAutomation=*/!FlashStorage::automationShift,
		                      /*shiftSequenceAndMPE=*/true, model_stack};
		edits::Context ctx{*currentSong, currentSong->project};
		(void)edits::apply(ctx, edit);
		return edit.shifted();
	}

private:
	/// The undo step the last length change opened, and its clip: what a redo of it scrolls the view back to.
	::Action* resize_action_{nullptr};
	project::ClipAddress resize_clip_{};
};

} // namespace

deluge::ui::scenes::bind::ClipWrites& clip_writes() {
	static HostClipWrites writes;
	return writes;
}

} // namespace deluge::gui::shell_host
