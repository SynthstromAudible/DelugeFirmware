/// @file
/// @brief The instrument the ported menu edits counts as edited by the user, as `SoundEditor::selectEncoderAction`
///        marks it: after a select turn on an item that edits the instrument
///        (`MenuItem::selectEncoderActionEditsInstrument`), when there is a sound to edit, never in the settings menu.
///
/// The menu is opened on the global guest over the instrument clip view, as `openShellMenu` leaves it, and every turn
/// arrives through `ShellHostUI::selectEncoderAction`, the hook the firmware calls.

#include "gui/ui/menu_view_of.h"
#include "gui/ui/shell_host_ui.h"
#include "gui/ui/ui.h"
#include "gui/views/instrument_clip_view.h"
#include "model/instrument/kit.h"
#include "model/model_stack.h"
#include "model/settings/runtime_feature_settings.h"
#include "model/song/song.h"
#include "processing/sound/sound_drum.h"
#include "processing/sound/sound_instrument.h"
#include "storage/storage_manager.h"

#include "scenes/menu/context.h"
#include "scenes/menu/menu_core.h"
#include "scenes/menu/menu_scene.h"
#include "scenes/menu/menu_state.h"
#include "scenes/menu/node.h"
#include "scenes/menu/node_id.h"
#include "scenes/menu/nodes/nodes.h"
#include "scenes/registry.h"
#include "shell/scene.h"

#include "support/feature_setting.h"
#include "support/global_guest_menu.h"
#include "support/song_fixture.h"
#include "support/ui_mode_guard.h"

#include "cppspec.hpp"

#include "spec/support/spec_main.h"

namespace {

namespace menu = deluge::scenes::menu;
namespace nodes = deluge::scenes::menu::nodes;
namespace spec = deluge::scenes::spec;
using deluge::scenes::menu_scene;

/// @brief The clip plays @p f's instrument, as a song's load leaves it: `beenEdited` copies the clip's arp from it.
void activate(spec::SongFixture& f) {
	alignas(ModelStackWithAutoParam) char memory[MODEL_STACK_MAX_SIZE];
	f.instrument->setActiveClip(setupModelStackWithTimelineCounter(memory, f.song, f.clip));
}

/// @brief Open @p node with @p ctx on the global guest over the instrument clip view, and let it draw.
bool open_on_guest(const menu::Node& node, const menu::MenuContext& ctx) {
	setRootUILowLevel(&instrumentClipView);
	(void)menu::assign_node_ids(nodes::kAllRoots);
	menu_scene().open(node, ctx);
	shellHostUI.setInitialScene(
	    deluge::ui::shell::SceneId{deluge::ui::shell::SceneKind::FirstApp, menu::node_id(node)});
	if (!openUI(&shellHostUI)) {
		return false;
	}
	for (int32_t i = 0; i < 4; i++) {
		(void)shellHostUI.timerCallback();
	}
	return true;
}

/// @brief One select detent through the guest, and the tick that acts on it.
void select_turn() {
	shellHostUI.selectEncoderAction(1);
	(void)shellHostUI.timerCallback();
}

/// @brief A synth built the way a preset load builds one, its params backed up with the song, not yet in it.
SoundInstrument* loose_synth(::Song& song) {
	ParamManagerForTimeline params;
	auto* synth = static_cast<SoundInstrument*>(StorageManager::createNewInstrument(OutputType::SYNTH, &params));
	synth->setupAsDefaultSynth(&params);
	song.live.param_backup.backUp(synth, nullptr, &params, true);
	return synth;
}

int32_t read_zero(const menu::MenuContext&) {
	return 0;
}
void write_nothing(const menu::MenuContext&, int32_t) {
}

/// Two value leaves: target-less toggles, so listing them reads nothing.
constexpr menu::Node value_a = menu::toggle_item(0, read_zero, write_nothing);
constexpr menu::Node value_b = menu::toggle_item(0, read_zero, write_nothing);
constexpr const menu::Node* kValues[] = {&value_a, &value_b};
/// Legacy's `Submenu` and `HorizontalMenu`, each over the two values.
constexpr menu::Node plain_list = menu::submenu(0, kValues);
constexpr menu::Node horizontal_column = menu::submenu(0, kValues, menu::Layout::Horizontal);
/// A horizontal page: a value column, a plain submenu column and a horizontal submenu column.
constexpr const menu::Node* kPageColumns[] = {&value_a, &plain_list, &horizontal_column};
constexpr menu::Node page = menu::submenu(0, kPageColumns, menu::Layout::Horizontal);

/// @brief @p node on screen at the root, its list cursor and its column focus both at @p at.
menu::MenuState on(const menu::Node& node, int16_t at = 0) {
	menu::MenuState st{};
	st.path[0] = &node;
	st.depth = 0;
	st.list[0].cursor = at;
	st.columns[0].focused = static_cast<int8_t>(at);
	return st;
}

} // namespace

// clang-format off
describe menu_edited_guest("the ported menu marks the instrument it edits, as SoundEditor::selectEncoderAction does", $ {
	it("marks a synth edited after a turn on one of its settings", _ {
		spec::UiStackGuard ui_guard;
		spec::GlobalGuestGuard guest_guard;
		spec::UiModeGuard mode_guard;
		spec::SongFixture fx = spec::make_synth_song();
		expect(fx.instrument->editedByUser).to_be_false();
		expect(open_on_guest(nodes::synth_mode, spec::menu_context_at(fx, menu::MenuContext::Root::Sound))).to_be_true();
		expect(fx.instrument->editedByUser).to_be_false(); // opening edits nothing
		select_turn();
		expect(fx.instrument->editedByUser).to_be_true();
	});

	it("marks it after a turn on a param column of a horizontal page", _ {
		spec::UiStackGuard ui_guard;
		spec::GlobalGuestGuard guest_guard;
		spec::UiModeGuard mode_guard;
		spec::SongFixture fx = spec::make_synth_song();
		expect(open_on_guest(nodes::delay, spec::menu_context_at(fx, menu::MenuContext::Root::Sound))).to_be_true();
		select_turn(); // the first column, the delay amount
		expect(fx.instrument->editedByUser).to_be_true();
	});

	it("marks a kit row's kit", _ {
		spec::UiStackGuard ui_guard;
		spec::GlobalGuestGuard guest_guard;
		spec::UiModeGuard mode_guard;
		spec::SongFixture fx = spec::make_kit_song();
		(void)spec::add_drum<SoundDrum>(fx);
		activate(fx);
		const menu::MenuContext ctx = spec::menu_context_at(fx, menu::MenuContext::Root::Drum);
		expect(ctx.addresses.slot.has_value() && ctx.addresses.kit_output.has_value()).to_be_true();
		expect(open_on_guest(nodes::delay_type, ctx)).to_be_true();
		select_turn();
		expect(fx.instrument->editedByUser).to_be_true();
	});

	it("leaves the kit unmarked in its own effects, where legacy has no sound", _ {
		spec::UiStackGuard ui_guard;
		spec::GlobalGuestGuard guest_guard;
		spec::UiModeGuard mode_guard;
		spec::SongFixture fx = spec::make_kit_song();
		(void)spec::add_drum<SoundDrum>(fx);
		activate(fx);
		const menu::MenuContext ctx =
		    menu::context_for(*fx.song, deluge::gui::menuViewOf(fx.clip), 0, /*performance_view=*/false,
		                      /*session_or_arranger_root=*/false, /*session_view_root=*/false, /*kit_global_fx=*/true);
		expect(ctx.root == menu::MenuContext::Root::KitGlobalFx).to_be_true();
		expect(ctx.addresses.slot.has_value()).to_be_false();
		expect(open_on_guest(nodes::delay_type, ctx)).to_be_true();
		select_turn();
		expect(fx.instrument->editedByUser).to_be_false();
	});

	it("leaves the instrument unmarked in the settings menu, even opened over a synth", _ {
		spec::UiStackGuard ui_guard;
		spec::GlobalGuestGuard guest_guard;
		spec::UiModeGuard mode_guard;
		spec::FeatureSetting quantize(RuntimeFeatureSettingType::Quantize, 0);
		spec::SongFixture fx = spec::make_synth_song();
		const menu::MenuContext ctx = spec::menu_context_at(fx, menu::MenuContext::Root::Settings);
		expect(ctx.addresses.slot.has_value()).to_be_true(); // legacy's currentSound is set here too
		expect(open_on_guest(nodes::feature_quantize, ctx)).to_be_true();
		select_turn();
		expect(fx.instrument->editedByUser).to_be_false();
	});

	it("leaves it unmarked when a turn only moves a plain list", _ {
		spec::UiStackGuard ui_guard;
		spec::GlobalGuestGuard guest_guard;
		spec::UiModeGuard mode_guard;
		spec::SongFixture fx = spec::make_synth_song();
		expect(open_on_guest(nodes::sound_root, spec::menu_context_at(fx, menu::MenuContext::Root::Sound))).to_be_true();
		const int32_t before = menu_scene().state().list[0].cursor;
		select_turn();
		expect(static_cast<int32_t>(menu_scene().state().list[0].cursor)).to_equal(before + 1);
		expect(fx.instrument->editedByUser).to_be_false();
	});

	it("copies the clip's arp into the synth's default arp, as beenEdited does", _ {
		spec::UiStackGuard ui_guard;
		spec::GlobalGuestGuard guest_guard;
		spec::UiModeGuard mode_guard;
		spec::SongFixture fx = spec::make_synth_song();
		activate(fx);
		expect(open_on_guest(nodes::arp_mode, spec::menu_context_at(fx, menu::MenuContext::Root::Sound))).to_be_true();
		select_turn();
		auto* synth = static_cast<SoundInstrument*>(fx.instrument);
		expect(fx.clip->arpSettings.mode != ArpMode::OFF).to_be_true();
		expect(synth->defaultArpSettings.mode == fx.clip->arpSettings.mode).to_be_true();
	});

	it("keeps a synth edited only through the menu across a preset change: it hibernates, not deleted", _ {
		spec::UiStackGuard ui_guard;
		spec::GlobalGuestGuard guest_guard;
		spec::UiModeGuard mode_guard;
		spec::SongFixture fx = spec::make_synth_song();
		activate(fx);
		expect(open_on_guest(nodes::synth_mode, spec::menu_context_at(fx, menu::MenuContext::Root::Sound))).to_be_true();
		select_turn();
		spec::GlobalGuestGuard::empty_guest_stack();
		nullifyUIs();
		Instrument* edited = fx.instrument;
		fx.song->replaceInstrument(edited, loose_synth(*fx.song)); // `Song::replaceInstrument`, a preset scroll's
		expect(fx.song->live.media.first_hibernating_instrument == edited).to_be_true();
	});

	it("deletes an unedited synth on a preset change, the control", _ {
		spec::UiStackGuard ui_guard;
		spec::GlobalGuestGuard guest_guard;
		spec::UiModeGuard mode_guard;
		spec::SongFixture fx = spec::make_synth_song();
		activate(fx);
		expect(open_on_guest(nodes::synth_mode, spec::menu_context_at(fx, menu::MenuContext::Root::Sound))).to_be_true();
		spec::GlobalGuestGuard::empty_guest_stack();
		nullifyUIs();
		fx.song->replaceInstrument(fx.instrument, loose_synth(*fx.song));
		expect(fx.song->live.media.first_hibernating_instrument == nullptr).to_be_true();
	});
});
describe menu_edited_rule("menu::select_turn_edits_instrument, MenuItem::selectEncoderActionEditsInstrument", $ {
	it("answers true on a leaf whose legacy item is a Value", _ {
		const menu::MenuContext ctx{};
		expect(menu::select_turn_edits_instrument(ctx, on(nodes::synth_mode))).to_be_true();   // Enum
		expect(menu::select_turn_edits_instrument(ctx, on(nodes::master_volume))).to_be_true(); // a param, Int
		expect(menu::select_turn_edits_instrument(ctx, on(value_a))).to_be_true();              // Toggle
	});

	it("answers for a bespoke item as its legacy item does", _ {
		const menu::MenuContext ctx{};
		expect(menu::select_turn_edits_instrument(ctx, on(nodes::multi_range))).to_be_true();   // MultiRange: a Value
		expect(menu::select_turn_edits_instrument(ctx, on(nodes::patch_cables))).to_be_false(); // PatchCables: not
	});

	it("answers false on a plain submenu, whatever row its cursor is on", _ {
		const menu::MenuContext ctx{};
		expect(menu::selected_child(ctx, on(plain_list, 1), plain_list) == &value_b).to_be_true();
		expect(menu::select_turn_edits_instrument(ctx, on(plain_list, 1))).to_be_false();
	});

	it("asks a horizontal page's focused column", _ {
		const menu::MenuContext ctx{};
		expect(menu::selected_child(ctx, on(page, 0), page) == &value_a).to_be_true();
		expect(menu::select_turn_edits_instrument(ctx, on(page, 0))).to_be_true();
		expect(menu::selected_child(ctx, on(page, 1), page) == &plain_list).to_be_true();
		expect(menu::select_turn_edits_instrument(ctx, on(page, 1))).to_be_false(); // a plain submenu column
		expect(menu::selected_child(ctx, on(page, 2), page) == &horizontal_column).to_be_true();
		expect(menu::select_turn_edits_instrument(ctx, on(page, 2))).to_be_true(); // its own current item, a value
	});

	it("asks the row under the cursor on a horizontal page drawn as a list", _ {
		menu::MenuContext ctx{};
		ctx.horizontal_menus = false;
		// The cursor and the column focus apart, so only the cursor's answer can pass: row 0 a value, column 1 not.
		menu::MenuState st = on(page, 1);
		st.list[0].cursor = 0;
		expect(menu::selected_child(ctx, st, page) == &value_a).to_be_true();
		expect(menu::select_turn_edits_instrument(ctx, st)).to_be_true();
		st.list[0].cursor = 1;
		st.columns[0].focused = 0;
		expect(menu::select_turn_edits_instrument(ctx, st)).to_be_false(); // row 1, the plain submenu
	});
});
// clang-format on

DELUGE_SPEC(menu_edited_spec, menu_edited_guest, menu_edited_rule)
