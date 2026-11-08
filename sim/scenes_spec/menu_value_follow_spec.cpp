/// @file
/// @brief The ported menu's open value follows a change made behind it: `View::displayAutomation`, the callback of
///        `TimerName::DISPLAY_AUTOMATION` outside the automation editor, re-reads the menu as it re-reads legacy's
///        item after a gold-knob turn, a learned or followed MIDI CC or automation playback.
///
/// Each example writes the param with the edit a gold knob makes (`edits::SetParamValue`, which
/// `ShellEditSink::write_param` applies), then runs the timer's callback itself: the harness fires no UI timer.

#include "gui/ui/shell_host_ui.h"
#include "gui/ui/ui.h"
#include "gui/ui_timer_manager.h"
#include "gui/views/instrument_clip_view.h"
#include "gui/views/view.h"
#include "instruments/deluge/params.h"
#include "model/clip/instrument_clip.h"
#include "model/edits/edit.h"
#include "model/edits/set_param_value.h"
#include "model/settings/runtime_feature_settings.h"
#include "model/song/song.h"
#include "notify/dirty_set.h"
#include "processing/sound/sound_instrument.h"

#include "scenes/menu/bindings.h"
#include "scenes/menu/context.h"
#include "scenes/menu/menu_scene.h"
#include "scenes/menu/node_id.h"
#include "scenes/menu/nodes/nodes.h"
#include "scenes/registry.h"
#include "shell/scene.h"

#include "support/feature_setting.h"
#include "support/global_guest_menu.h"
#include "support/song_fixture.h"

#include "cppspec.hpp"

#include "spec/support/spec_main.h"

namespace {

namespace menu = deluge::scenes::menu;
namespace nodes = deluge::scenes::menu::nodes;
namespace spec = deluge::scenes::spec;
using deluge::project::OwnerId;
using deluge::project::ParamAddress;
using deluge::project::ParamSpace;
using deluge::scenes::menu_scene;
using deluge::scenes::spec::FeatureSetting;

/// @brief The synth's LPF frequency on the fixture's clip, by address (`knob_writes_spec.cpp`'s shape).
ParamAddress lpf_on(spec::SongFixture& fx) {
	auto* synth = static_cast<SoundInstrument*>(fx.instrument);
	return ParamAddress{.owner = OwnerId::instrument(*synth->id()),
	                    .space = ParamSpace::ENGINE,
	                    .id = deluge::instrument::param::kLpfFrequency,
	                    .clip_id = fx.clip->id()};
}

/// @brief A gold knob's write of @p value to the LPF frequency.
bool knob_write(spec::SongFixture& fx, int32_t value) {
	deluge::edits::Context ctx{*fx.song, fx.song->project};
	deluge::edits::SetParamValue write{lpf_on(fx), value, 0, 0, deluge::edits::SetParamValue::Resolve::WRITE};
	return deluge::edits::apply(ctx, write) == Error::NONE;
}

/// @brief `TimerName::DISPLAY_AUTOMATION`'s callback outside the automation editor (`UITimerManager::routine`).
void display_automation_fires() {
	view.pendingParamAutomationUpdatesModLevels = false;
	view.displayAutomation();
}

/// @brief The guest over the clip view on the sound menu, at @p node (a leaf's full screen, or a page).
bool open_sound_menu_at(spec::SongFixture& fx, const menu::Node& node) {
	if (!spec::open_note_mode_over_clip_view(fx, menu::MenuContext::Root::Sound)) {
		return false;
	}
	menu_scene().enter_submenu(node);
	(void)shellHostUI.timerCallback();
	return menu_scene().focused() == &node;
}

/// @brief Did something ask the guest's shell for an OLED frame since the last drain?
bool shell_asked_for_a_frame() {
	(void)deluge::notify::dirty().drain(deluge::notify::DirtyConsumer::Legacy);
	return deluge::notify::dirty().drain(deluge::notify::DirtyConsumer::Shell).has(deluge::notify::Dirty::Oled);
}

} // namespace

// clang-format off
describe menu_value_follow("View::displayAutomation under the ported menu", $ {
	it("a full-screen param takes the value a gold knob wrote behind it", _ {
		spec::UiStackGuard ui_guard;
		spec::GlobalGuestGuard guest_guard;
		spec::SongFixture fx = spec::make_synth_song();
		expect(open_sound_menu_at(fx, nodes::lpf_freq)).to_be_true();
		const int32_t before = menu_scene().state().edit_int;
		expect(knob_write(fx, 0)).to_be_true();
		const int32_t now = menu::bindings::read(nodes::lpf_freq, menu_scene().menu_context());
		expect(now != before).to_be_true(); // the write moved what the screen shows
		display_automation_fires();
		expect(menu_scene().state().edit_int).to_equal(now);
	});

	it("a horizontal page asks for the frame that shows its columns' new values", _ {
		spec::UiStackGuard ui_guard;
		spec::GlobalGuestGuard guest_guard;
		FeatureSetting horizontal(RuntimeFeatureSettingType::HorizontalMenus, RuntimeFeatureStateToggle::On);
		spec::SongFixture fx = spec::make_synth_song();
		expect(open_sound_menu_at(fx, nodes::lpf)).to_be_true();
		expect(knob_write(fx, 0)).to_be_true();
		(void)deluge::notify::dirty().drain();
		display_automation_fires();
		expect(shell_asked_for_a_frame()).to_be_true();
	});

	it("re-reads on the settings root too, as legacy re-reads whatever item is current", _ {
		spec::UiStackGuard ui_guard;
		spec::GlobalGuestGuard guest_guard;
		spec::SongFixture fx = spec::make_synth_song();
		expect(spec::open_note_mode_over_clip_view(fx, menu::MenuContext::Root::Settings)).to_be_true();
		(void)shellHostUI.timerCallback();
		(void)deluge::notify::dirty().drain();
		display_automation_fires();
		expect(shell_asked_for_a_frame()).to_be_true();
	});

	it("the knob's write arms the timer under the guest, as notifyParamAutomationOccurred arms it for legacy", _ {
		// The trigger half: the menu's clip is the knobs' owner, so `knobParamManager_` matches.
		spec::UiStackGuard ui_guard;
		spec::GlobalGuestGuard guest_guard;
		spec::SongFixture fx = spec::make_synth_song();
		expect(open_sound_menu_at(fx, nodes::lpf_freq)).to_be_true();
		view.setActiveModControllableTimelineCounter(fx.clip, false);
		uiTimerManager.unsetTimer(TimerName::DISPLAY_AUTOMATION);
		expect(knob_write(fx, 0)).to_be_true();
		expect(uiTimerManager.isTimerSet(TimerName::DISPLAY_AUTOMATION)).to_be_true();
		uiTimerManager.unsetTimer(TimerName::DISPLAY_AUTOMATION);
	});
});
// clang-format on

DELUGE_SPEC(menu_value_follow_spec, menu_value_follow)
