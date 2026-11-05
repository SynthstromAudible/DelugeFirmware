/// @file
/// @brief The automation view under the ported menu: the facts about the root UI the menu and the knobs hold, read
///        again as the root, or the view's editor, changes while the menu is open.
///
/// Runs over the real `automationView`, `instrumentClipView` and guest, with the menu opened through the shortcut entry
/// (`support/menu_over_root.h`) on a fresh synth (`spec::make_synth_song`).

#include "gui/ui/shell_guest_services.h"
#include "gui/ui/shell_host_ui.h"
#include "gui/ui/ui.h" // currentUIMode, setRootUILowLevel
#include "gui/views/automation_view.h"
#include "gui/views/instrument_clip_view.h"
#include "modulation/params/param.h"
#include "scenes/menu/context.h"
#include "scenes/menu/menu_scene.h"
#include "scenes/registry.h"

#include "support/global_guest_menu.h"
#include "support/menu_over_root.h"
#include "support/song_fixture.h"

#include "cppspec.hpp"

#include "spec/support/spec_main.h"

namespace params = deluge::modulation::params;
namespace spec = deluge::scenes::spec;
using deluge::scenes::menu_scene;
using deluge::scenes::menu::MenuContext;
using spec::GlobalGuestGuard;
using spec::open_shortcut_over_automation;
using spec::ShellMenuOn;
using spec::UiStackGuard;

namespace {

/// The automation view on @p clip's overview, nothing selected.
void on_overview(InstrumentClip* clip) {
	automationView.onArrangerView = false;
	clip->lastSelectedParamKind = params::Kind::NONE;
	clip->lastSelectedParamID = params::kNoParamID;
	currentUIMode = UI_MODE_NONE;
}

/// The automation view's editor on @p clip's LPF frequency.
void on_lpf_freq(InstrumentClip* clip) {
	clip->lastSelectedParamKind = params::Kind::PATCHED;
	clip->lastSelectedParamID = params::LOCAL_LPF_FREQ;
}

} // namespace

// clang-format off
describe automation_root_facts("the menu's root facts under the automation view", $ {
	it("are the automation view's overview at open", _ {
		ShellMenuOn on;
		GlobalGuestGuard guest_guard;
		UiStackGuard ui_guard;
		spec::SongFixture fx = spec::make_synth_song();
		on_overview(fx.clip);
		expect(open_shortcut_over_automation(fx.clip, 8, 7)).to_be_true();
		const MenuContext& ctx = menu_scene().menu_context();
		expect(ctx.root_is_automation).to_be_true();
		expect(ctx.root_is_keyboard).to_be_false();
		expect(ctx.root_greyout == MenuContext::RootGreyout::SpareSidebar).to_be_true();
	});

	it("follow the view into its editor, for the menu's greyout and the knobs", _ {
		// `SoundEditor::getGreyoutColsAndRows` re-reads `automationView.inAutomationEditor()` every time
		// (`sound_editor.cpp:497-500`): the editor greys nothing.
		ShellMenuOn on;
		GlobalGuestGuard guest_guard;
		UiStackGuard ui_guard;
		spec::SongFixture fx = spec::make_synth_song();
		on_overview(fx.clip);
		expect(open_shortcut_over_automation(fx.clip, 8, 7)).to_be_true();
		on_lpf_freq(fx.clip);
		shellHostUI.refreshRootFacts();
		expect(menu_scene().menu_context().root_greyout == MenuContext::RootGreyout::Nothing).to_be_true();
		expect(deluge::gui::ui::guestKnobsSeeAutomationEditor()).to_be_true();
		uint32_t cols = 0;
		uint32_t rows = 0;
		expect(shellHostUI.getGreyoutColsAndRows(&cols, &rows)).to_be_false(); // greys nothing

	});

	it("follow a new root", _ {
		ShellMenuOn on;
		GlobalGuestGuard guest_guard;
		UiStackGuard ui_guard;
		spec::SongFixture fx = spec::make_synth_song();
		on_overview(fx.clip);
		expect(open_shortcut_over_automation(fx.clip, 8, 7)).to_be_true();
		setRootUILowLevel(&instrumentClipView);
		shellHostUI.refreshRootFacts();
		const MenuContext& ctx = menu_scene().menu_context();
		expect(ctx.root_is_automation).to_be_false();
		expect(ctx.root_greyout == MenuContext::RootGreyout::InstrumentClip).to_be_true();
		expect(deluge::gui::ui::guestKnobsSeeAutomationEditor()).to_be_false();
	});
});
// clang-format on

DELUGE_SPEC(automation_retarget_spec, automation_root_facts)
