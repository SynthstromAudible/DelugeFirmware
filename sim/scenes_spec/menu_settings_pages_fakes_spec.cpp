/// @file
/// @brief The settings pages' DEVICE rows, driven through a real `MenuScene` and `Shell` over the ports' fakes, with no
///        song behind the menu: what a page shows is the fake's value, and what a turn writes reaches the fake's
///        store.

#include "scenes/bind/ports/device_settings.h"
#include "scenes/menu/bindings.h"
#include "scenes/menu/context.h"
#include "scenes/menu/menu_scene.h"
#include "scenes/menu/node.h"
#include "scenes/menu/node_id.h"
#include "scenes/menu/nodes/multi_range.h"
#include "scenes/menu/nodes/nodes.h"
#include "scenes/menu/nodes/settings.h"
#include "scenes/menu/present.h"
#include "scenes/menu/walk.h"
#include "scenes/profile.h"
#include "scenes/registry.h"

#include "deluge-project/device_settings.h"
#include "deluge-project/features.h"
#include "deluge-project/instrument_slot.h"
#include "gfx/oled/glyphs.h"
#include "gui/l10n/l10n.h"
#include "instruments/deluge/sound_state.h"
#include "music-theory/preset_scales.h"
#include "notify/dirty_set.h"
#include "notify/notice_queue.h"
#include "shell/profiles/deluge.h"
#include "shell/shell.h"

#include "support/fake_host_rig.h"
#include "support/mocks/fake_surfaces.h"

#include "cppspec.hpp"
#include "support/matchers/value_matchers.h"

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "support/spec_main.h"

using namespace deluge::scenes;
using deluge::scenes::menu::MenuContext;
using deluge::scenes::menu::Node;
using deluge::ui::shell::SceneId;
using deluge::ui::shell::SceneKind;
using deluge::ui::shell::Shell;
using deluge::ui::shell::ShellConfig;
using device_path = deluge::project::device_path;
using deluge::project::Feature;

namespace {

namespace nodes = deluge::scenes::menu::nodes;

constexpr uint8_t kHorizontalEncoder = 1;
constexpr uint8_t kSelectEncoder = 3;
constexpr uint8_t kSelectEncButtonX = 4, kSelectEncButtonY = 3;

/// @brief A `Shell` over fake surfaces with the app's scene registry, and the menu's context over the ports' fakes.
struct Rig {
	spec::FakeHostRig fakes;
	deluge::ui::shell::SceneRegistry reg = scene_registry();
	uitest::FakeSurfaceSet surfaces{deluge::ui::shell::kDelugeBoard, deluge_profile()};
	Shell shell{surfaces, deluge_profile(), reg, deluge::ui::shell::Services{}, ShellConfig{}};

	Rig() {
		deluge::l10n::chosenLanguage = &deluge::l10n::built_in::english;
		deluge::notify::Notice discard;
		while (deluge::notify::notices().pop(discard)) {}
	}
	Rig(const Rig&) = delete;
	Rig& operator=(const Rig&) = delete;
	/// @brief Every scene popped, so the next example's menu starts as a boot leaves it.
	~Rig() {
		while (shell.stack().depth() > 0) {
			shell.stack().pop(SceneKind::Null);
		}
		menu_scene().reset_process_state();
	}

	/// @brief Open @p page on the settings root's context, with no model behind it.
	MenuContext open(const Node& page) {
		const MenuContext ctx = fakes.settings_context(/*with_model=*/false);
		(void)menu::assign_node_ids(nodes::kAllRoots);
		menu_scene().open(page, ctx);
		shell.init(SceneId{SceneKind::FirstApp, menu::node_id(page)});
		shell.tick(0);
		return ctx;
	}
	/// @brief @p detents clicks of the select encoder: two edges a click.
	void turn(int32_t detents, uint32_t at_ms) {
		shell.on_board_event(
		    DelugeInputEvent{DELUGE_EVENT_ENCODER, kSelectEncoder, 0, static_cast<int16_t>(detents * 2)}, at_ms);
		shell.tick(at_ms + 5);
	}
	/// @brief @p detents clicks of the horizontal encoder, which picks a range's end.
	void horizontal(int32_t detents, uint32_t at_ms) {
		shell.on_board_event(
		    DelugeInputEvent{DELUGE_EVENT_ENCODER, kHorizontalEncoder, 0, static_cast<int16_t>(detents * 2)}, at_ms);
		shell.tick(at_ms + 5);
	}
	/// @brief Press and release the select encoder.
	void select(uint32_t at_ms) {
		shell.on_board_event(DelugeInputEvent{DELUGE_EVENT_BUTTON, kSelectEncButtonX, kSelectEncButtonY, 255}, at_ms);
		shell.on_board_event(DelugeInputEvent{DELUGE_EVENT_BUTTON, kSelectEncButtonX, kSelectEncButtonY, 0}, at_ms);
		shell.tick(at_ms + 5);
	}
};

/// @brief Where @p node is listed on @p page in @p ctx, or -1.
int32_t row_of(const Node& page, const Node& node, const MenuContext& ctx) {
	std::array<const Node*, menu::kMaxDynamicChildren> listed{};
	const size_t count = menu::bindings::listed_children(page, ctx, listed, menu::bindings::ListFor::Rows);
	for (size_t i = 0; i < count; ++i) {
		if (listed[i] == &node) {
			return static_cast<int32_t>(i);
		}
	}
	return -1;
}

/// @brief The names @p node lists in @p ctx, in menu order.
std::vector<std::string> names_of(const Node& node, const MenuContext& ctx) {
	std::array<std::string_view, menu::kMaxDynamicNames> out{};
	std::array<deluge::ui::shell::TextScratch, menu::kMaxDynamicNames> scratch{};
	const size_t count = menu::bindings::names(node, ctx, out, scratch, menu::bindings::NameForm::Full);
	return {out.begin(), out.begin() + static_cast<std::ptrdiff_t>(count)};
}

/// @brief Sets the fake's DEVICE row at @p path to the number @p value.
void hold(spec::FakeHostRig& fakes, deluge::project::DevicePath path, int64_t value) {
	fakes.host.device.values.insert_or_assign(std::string{path}, deluge::serialization::Value::makeNumber(value));
}

/// @brief The last @p count stores the fake saw, each as "<path>=<text>", in order.
std::vector<std::string> last_stores(const spec::FakeHostRig& fakes, size_t count) {
	const auto& stores = fakes.host.device.stores;
	std::vector<std::string> out;
	for (size_t i = stores.size() > count ? stores.size() - count : 0; i < stores.size(); ++i) {
		out.push_back(stores[i].first + "=" + stores[i].second.str());
	}
	return out;
}

/// @brief The community features' nodes, in the page's order, each with its feature.
struct FeatureNode {
	const Node* node;
	Feature feature;
};
constexpr std::array<FeatureNode, deluge::project::kFeatureCount> kFeatureNodes{{
    {&nodes::feature_drum_randomizer, Feature::DRUM_RANDOMIZER},
    {&nodes::feature_fine_tempo, Feature::FINE_TEMPO_KNOB},
    {&nodes::feature_quantize, Feature::QUANTIZE},
    {&nodes::feature_catch_notes, Feature::CATCH_NOTES},
    {&nodes::feature_delete_unused_kit_rows, Feature::DELETE_UNUSED_KIT_ROWS},
    {&nodes::feature_alt_delay_params, Feature::ALT_GOLDEN_KNOB_DELAY_PARAMS},
    {&nodes::feature_sync_scaling, Feature::SYNC_SCALING_ACTION},
    {&nodes::feature_highlight_incoming_notes, Feature::HIGHLIGHT_INCOMING_NOTES},
    {&nodes::feature_norns_layout, Feature::DISPLAY_NORNS_LAYOUT},
    {&nodes::feature_sticky_shift, Feature::SHIFT_IS_STICKY},
    {&nodes::feature_light_shift, Feature::LIGHT_SHIFT_LED},
    {&nodes::feature_dx7_engine, Feature::ENABLE_DX7_ENGINE},
    {&nodes::feature_kb_sidebar_menu_exit, Feature::ENABLE_KEYBOARD_VIEW_SIDEBAR_MENU_EXIT},
    {&nodes::feature_launch_event_playhead, Feature::ENABLE_LAUNCH_EVENT_PLAYHEAD},
    {&nodes::feature_chord_keyboard, Feature::DISPLAY_CHORD_KEYBOARD},
    {&nodes::feature_alt_playback_start, Feature::ALTERNATIVE_PLAYBACK_START_BEHAVIOUR},
    {&nodes::feature_grid_loop_pads, Feature::ENABLE_GRID_VIEW_LOOP_PADS},
    {&nodes::feature_alt_tap_tempo, Feature::ALTERNATIVE_TAP_TEMPO_BEHAVIOUR},
    {&nodes::feature_horizontal_menus, Feature::HORIZONTAL_MENUS},
    {&nodes::feature_rounded_corners, Feature::ROUNDED_CORNERS},
    {&nodes::feature_trim_from_start, Feature::TRIM_FROM_START_OF_AUDIO_CLIP},
    {&nodes::feature_show_battery_level, Feature::SHOW_BATTERY_LEVEL},
    {&nodes::feature_shortcut_overlay, Feature::SHORTCUT_OVERLAY},
}};

} // namespace

// clang-format off
describe menu_settings_pages_fakes("the settings pages' DEVICE rows, over the ports' fakes and no song", ${
	it("lists the patch cable polarity on the defaults page and shows the fake's value", _ {
		Rig r;
		const MenuContext ctx = r.open(nodes::defaults_settings);
		const int32_t row = row_of(nodes::defaults_settings, nodes::default_patch_cable_polarity, ctx);
		to_be_at_least(expect(row), 0);
		r.turn(row, 10);
		expect(static_cast<int32_t>(menu_scene().state().list[0].cursor)).to_equal(row);

		// The fake's default is bipolar, the row's second choice; the names are the cable's own words.
		expect(menu::bindings::read(nodes::default_patch_cable_polarity, ctx)).to_equal(1);
		expect(names_of(nodes::default_patch_cable_polarity, ctx))
			.to_equal(std::vector<std::string>{"unipolar", "bipolar"});
		r.fakes.host.device.values.insert_or_assign(std::string{device_path::kPatchCablePolarity},
		                                            deluge::serialization::Value::makeScalar("unipolar"));
		expect(menu::bindings::read(nodes::default_patch_cable_polarity, ctx)).to_equal(0);
		expect(r.fakes.host.device.stores.empty()).to_be_true();
	});

	it("stores a turn of the polarity through the device port, one write", _ {
		Rig r;
		const MenuContext ctx = r.open(nodes::defaults_settings);
		r.turn(row_of(nodes::defaults_settings, nodes::default_patch_cable_polarity, ctx), 10);
		r.select(30);
		expect(menu_scene().focused()).to_equal(&nodes::default_patch_cable_polarity);
		(void)deluge::notify::dirty().drain();
		r.turn(-1, 50);
		expect(r.fakes.host.device.stores.size()).to_equal(size_t{1});
		if (!r.fakes.host.device.stores.empty()) {
			expect(r.fakes.host.device.stores.front().first).to_equal(std::string{device_path::kPatchCablePolarity});
			expect(r.fakes.host.device.stores.front().second.str()).to_equal(std::string{"unipolar"});
		}
		expect(menu::bindings::read(nodes::default_patch_cable_polarity, ctx)).to_equal(0);
		// The song's write sink saw nothing: a device setting is no song edit.
		expect(r.fakes.host.writes.requests.empty()).to_be_true();
	});

	it("changes nothing and raises nothing for a write the row refuses", _ {
		Rig r;
		const MenuContext ctx = r.open(nodes::defaults_settings);
		// The row forced to a word it cannot name: it reads as the default, and a value past its list is refused.
		r.fakes.host.device.values.insert_or_assign(std::string{device_path::kPatchCablePolarity},
		                                            deluge::serialization::Value::makeScalar("tripolar"));
		expect(menu::bindings::read(nodes::default_patch_cable_polarity, ctx)).to_equal(1);
		(void)deluge::notify::dirty().drain();
		menu::bindings::write(nodes::default_patch_cable_polarity, ctx, 2);
		menu::bindings::write(nodes::default_patch_cable_polarity, ctx, -1);
		expect(r.fakes.host.device.stores.empty()).to_be_true();
		expect(r.fakes.host.device.values.at(std::string{device_path::kPatchCablePolarity}).str())
			.to_equal(std::string{"tripolar"});
		expect(deluge::notify::dirty().drain().has(deluge::notify::Dirty::Oled)).to_be_false();
	});

	it("raises nothing for a write the device port refuses", _ {
		Rig r;
		const MenuContext ctx = r.open(nodes::defaults_settings);
		r.fakes.host.device.refuse.insert(std::string{device_path::kPatchCablePolarity});
		(void)deluge::notify::dirty().drain();
		menu::bindings::write(nodes::default_patch_cable_polarity, ctx, 0);
		// The write reached the port, which kept nothing and said so.
		expect(r.fakes.host.device.stores.size()).to_equal(size_t{1});
		expect(menu::bindings::read(nodes::default_patch_cable_polarity, ctx)).to_equal(1);
		expect(deluge::notify::dirty().drain().has(deluge::notify::Dirty::Oled)).to_be_false();
		deluge::notify::Notice notice{};
		expect(deluge::notify::notices().pop(notice)).to_be_false();
	});

	it("says restart to apply for a USB audio enable the device takes at its next start, and only then", _ {
		Rig r;
		const MenuContext ctx = r.open(nodes::usb_audio_settings);
		expect(row_of(nodes::usb_audio_settings, nodes::usb_audio_enabled, ctx)).to_equal(0);
		expect(menu::bindings::read(nodes::usb_audio_enabled, ctx)).to_equal(1);
		// A write the device has in effect at once says nothing.
		menu::bindings::write(nodes::usb_audio_enabled, ctx, 0);
		deluge::notify::Notice notice{};
		expect(deluge::notify::notices().pop(notice)).to_be_false();
		// One it takes only at its next start says so.
		r.fakes.host.device.accept_restart.insert(std::string{device_path::kUsbAudioEnabled});
		menu::bindings::write(nodes::usb_audio_enabled, ctx, 1);
		expect(last_stores(r.fakes, 2))
		    .to_equal(std::vector<std::string>{std::string{device_path::kUsbAudioEnabled} + "=0",
		                                       std::string{device_path::kUsbAudioEnabled} + "=1"});
		expect(deluge::notify::notices().pop(notice)).to_be_true();
		expect(static_cast<int>(notice.string_id))
			.to_equal(static_cast<int>(deluge::l10n::String::STRING_FOR_RESTART_TO_APPLY));
		expect(r.fakes.host.writes.requests.empty()).to_be_true();
	});

	it("names the audio and clock choices as their items do, and reads each row from the fake", _ {
		Rig r;
		const MenuContext ctx = r.open(nodes::root_settings);
		expect(names_of(nodes::usb_audio_output, ctx)).to_equal(std::vector<std::string>{"Main mix", "Line in"});
		expect(names_of(nodes::monitor_mode, ctx))
		    .to_equal(std::vector<std::string>{"Conditional", "Enabled", "Disabled"});
		expect(menu::bindings::read(nodes::trigger_in_ppqn, ctx)).to_equal(24);
		expect(menu::bindings::range(nodes::trigger_in_ppqn, ctx).min).to_equal(1);
		expect(menu::bindings::range(nodes::trigger_out_ppqn, ctx).max).to_equal(192);
		expect(menu::bindings::read(nodes::record_count_in, ctx)).to_equal(0);
		expect(menu::bindings::range(nodes::record_count_in, ctx).max).to_equal(4);
		expect(menu::bindings::read(nodes::midi_clock_out, ctx)).to_equal(1);
		expect(menu::bindings::read(nodes::tempo_magnitude_matching, ctx)).to_equal(0);
		hold(r.fakes, device_path::kTriggerInPpqn, 96);
		expect(menu::bindings::read(nodes::trigger_in_ppqn, ctx)).to_equal(96);
		menu::bindings::write(nodes::trigger_out_ppqn, ctx, 48);
		menu::bindings::write(nodes::midi_clock_in, ctx, 0);
		menu::bindings::write(nodes::monitor_mode, ctx, 2);
		expect(last_stores(r.fakes, 3))
		    .to_equal(std::vector<std::string>{std::string{device_path::kTriggerOutPpqn} + "=48",
		                                       std::string{device_path::kMidiClockIn} + "=0",
		                                       std::string{device_path::kMonitorMode} + "=off"});
		expect(r.fakes.host.writes.requests.empty()).to_be_true();
	});

	it("stores a stem-export option through the device port and reads it back, with no song edit", _ {
		Rig r;
		const MenuContext ctx = r.open(nodes::root_settings);
		// The export's own defaults: drums normalized, plain stems not.
		expect(menu::bindings::read(nodes::stem_normalization, ctx)).to_equal(0);
		expect(menu::bindings::read(nodes::stem_normalization_drums, ctx)).to_equal(1);
		menu::bindings::write(nodes::stem_normalization_drums, ctx, 0);
		menu::bindings::write(nodes::stem_mixdown, ctx, 1);
		expect(last_stores(r.fakes, 2))
		    .to_equal(std::vector<std::string>{std::string{device_path::kStemNormalizeDrums} + "=0",
		                                       std::string{device_path::kStemMixdown} + "=1"});
		expect(menu::bindings::read(nodes::stem_normalization_drums, ctx)).to_equal(0);
		expect(menu::bindings::read(nodes::stem_mixdown, ctx)).to_equal(1);
		expect(r.fakes.host.writes.requests.empty()).to_be_true();
	});

	it("shows the play cursor's three forms in their value order, read from the fake", _ {
		Rig r;
		const MenuContext ctx = r.open(nodes::root_settings);
		expect(row_of(nodes::root_settings, nodes::flash_status, ctx)).not_().to_equal(-1);
		expect(menu::bindings::read(nodes::flash_status, ctx))
			.to_equal(static_cast<int32_t>(deluge::project::FlashCursor::SLOW));
		expect(names_of(nodes::flash_status, ctx)).to_equal(std::vector<std::string>{"Fast", "Disabled", "Slow"});
		expect(menu::bindings::range(nodes::flash_status, ctx).max).to_equal(2);
	});

	it("lists every community feature on its page, in the page's order, each read from the fake", _ {
		Rig r;
		const MenuContext ctx = r.open(nodes::runtime_features);
		for (size_t at = 0; at < kFeatureNodes.size(); ++at) {
			const FeatureNode& want = kFeatureNodes[at];
			const deluge::project::FeatureInfo& info = deluge::project::feature_info(want.feature);
			const std::string label{info.path};
			expect(row_of(nodes::runtime_features, *want.node, ctx)).to_equal(static_cast<int32_t>(at), label);
			expect(std::string{want.node->bind.target.path}).to_equal(std::string{device_path::feature(want.feature)}, label);
			expect(static_cast<int>(want.node->kind))
			    .to_equal(static_cast<int>(deluge::project::is_toggle(info) ? menu::Kind::Toggle : menu::Kind::Enum), label);
			expect(menu::bindings::read(*want.node, ctx)).to_equal(static_cast<int32_t>(info.default_value), label);
		}
		// The fake's value is what a feature shows.
		r.fakes.host.device.values.insert_or_assign(std::string{device_path::feature(Feature::QUANTIZE)},
		                                            deluge::serialization::Value::makeNumber(0));
		expect(menu::bindings::read(nodes::feature_quantize, ctx)).to_equal(0);
		r.fakes.host.device.values.insert_or_assign(std::string{device_path::feature(Feature::SHORTCUT_OVERLAY)},
		                                            deluge::serialization::Value::makeScalar("menuAndClip"));
		expect(menu::bindings::read(nodes::feature_shortcut_overlay, ctx))
			.to_equal(static_cast<int32_t>(deluge::project::ShortcutOverlay::MENU_AND_CLIP));
		expect(r.fakes.host.device.stores.empty()).to_be_true();
	});

	it("names the two features with choices of their own in their values' order", _ {
		Rig r;
		const MenuContext ctx = r.open(nodes::runtime_features);
		expect(names_of(nodes::feature_sync_scaling, ctx))
			.to_equal(std::vector<std::string>{"Sync Scaling", "Fill mode"});
		expect(names_of(nodes::feature_shortcut_overlay, ctx))
			.to_equal(std::vector<std::string>{"Off", "On - Menu only", "On - Menu + Clip"});
		expect(menu::bindings::range(nodes::feature_shortcut_overlay, ctx).max).to_equal(2);
	});

	it("stores a feature's write through the device port: a toggle's number, a choice's word", _ {
		Rig r;
		const MenuContext ctx = r.open(nodes::runtime_features);
		menu::bindings::write(nodes::feature_quantize, ctx, 0);
		menu::bindings::write(nodes::feature_sync_scaling, ctx, 1);
		menu::bindings::write(nodes::feature_sticky_shift, ctx, 1);
		const auto& stores = r.fakes.host.device.stores;
		expect(stores.size()).to_equal(size_t{3});
		if (stores.size() == 3) {
			expect(stores[0].first).to_equal(std::string{device_path::feature(Feature::QUANTIZE)});
			expect(stores[0].second.str()).to_equal(std::string{"0"});
			expect(stores[1].first).to_equal(std::string{device_path::feature(Feature::SYNC_SCALING_ACTION)});
			expect(stores[1].second.str()).to_equal(std::string{"fill"});
			// The shift LED the sticky-shift write also lights is the host's to store: the menu writes one row.
			expect(stores[2].first).to_equal(std::string{device_path::feature(Feature::SHIFT_IS_STICKY)});
		}
		expect(menu::bindings::read(nodes::feature_sync_scaling, ctx))
			.to_equal(static_cast<int32_t>(deluge::project::SyncScalingAction::FILL));
		expect(r.fakes.host.writes.requests.empty()).to_be_true();
	});

	it("lays the open menu out by a horizontal-menus write on its next render", _ {
		Rig r;
		(void)r.open(nodes::voice_usage);
		expect(menu::is_horizontal(menu_scene().menu_context(), nodes::voice_usage)).to_be_true();
		const std::string horizontal = r.surfaces.pbm(0);
		menu::bindings::write(nodes::feature_horizontal_menus, menu_scene().menu_context(), 0);
		expect(r.fakes.host.device.stores.size()).to_equal(size_t{1});
		expect(menu::is_horizontal(menu_scene().menu_context(), nodes::voice_usage)).to_be_false();
		r.shell.tick(50);
		const std::string vertical = r.surfaces.pbm(0);
		expect(vertical).not_().to_equal(horizontal);
		menu::bindings::write(nodes::feature_horizontal_menus, menu_scene().menu_context(), 1);
		expect(menu::is_horizontal(menu_scene().menu_context(), nodes::voice_usage)).to_be_true();
		r.shell.tick(100);
		expect(r.surfaces.pbm(0)).to_equal(horizontal);
	});

	it("spells a range key with a sharp or a flat as the device's useSharps row says, read as it is drawn", _ {
		Rig r;
		MenuContext ctx = r.fakes.settings_context(/*with_model=*/false);
		ctx.addresses.slot = deluge::project::SlotAddress{.kind = deluge::project::SlotAddress::Kind::OUTPUT, .id = 7};
		ctx.source_index = 0;
		deluge::instrument::SoundState& state = r.fakes.host.sound.states[7];
		state.sources[0].ranges = {deluge::instrument::RangeState{.top_note = 60},
		                           deluge::instrument::RangeState{.top_note = 72}};
		// Range 1 starts at the note above range 0's top, 61: C sharp 3 or D flat 3.
		std::array<char, 32> buf{};
		deluge::ui::shell::TextScratch scratch;
		expect(std::string{nodes::range_row_label(ctx, 1, buf.data(), buf.size(), scratch)})
			.to_equal(std::string{"C#3    - top"});
		r.fakes.host.device.values.insert_or_assign(std::string{device_path::kUseSharps},
		                                            deluge::serialization::Value::makeNumber(0));
		expect(std::string{nodes::range_row_label(ctx, 1, buf.data(), buf.size(), scratch)})
			.to_equal(std::string{"D"} + deluge::gfx::oled::kFlatGlyph + "3    - top");
		r.fakes.host.device.values.insert_or_assign(std::string{device_path::kUseSharps},
		                                            deluge::serialization::Value::makeNumber(1));
		expect(std::string{nodes::range_row_label(ctx, 1, buf.data(), buf.size(), scratch)})
			.to_equal(std::string{"C#3    - top"});
		expect(r.fakes.host.device.stores.empty()).to_be_true();
	});

	it("lists the battery level only while the device's showBatteryLevel row is on", _ {
		Rig r;
		const MenuContext ctx = r.open(nodes::root_settings);
		expect(nodes::battery_level.relevant(ctx)).to_be_true();
		expect(row_of(nodes::root_settings, nodes::battery_level, ctx)).not_().to_equal(-1);
		const std::string battery{device_path::feature(Feature::SHOW_BATTERY_LEVEL)};
		r.fakes.host.device.values.insert_or_assign(battery, deluge::serialization::Value::makeNumber(0));
		expect(nodes::battery_level.relevant(ctx)).to_be_false();
		expect(row_of(nodes::root_settings, nodes::battery_level, ctx)).to_equal(-1);
		r.fakes.host.device.values.insert_or_assign(battery, deluge::serialization::Value::makeNumber(1));
		expect(nodes::battery_level.relevant(ctx)).to_be_true();
		// With no device port the readout follows its row's default, which is on.
		MenuContext unbound = ctx;
		unbound.ports.device = nullptr;
		expect(nodes::battery_level.relevant(unbound)).to_be_true();
	});

	it("pushes the tempo range's other end, as main's IntegerRange does, and stores both ends, the lower first", _ {
		Rig r;
		hold(r.fakes, device_path::kTempoLower, 100);
		hold(r.fakes, device_path::kTempoUpper, 150);
		(void)r.open(nodes::default_tempo);
		// A split range opens on its left end.
		expect(static_cast<int>(menu_scene().state().edit_edge)).to_equal(static_cast<int>(deluge::ui::oled::RangeEdge::Left));
		r.turn(60, 20);
		expect(last_stores(r.fakes, 2))
		    .to_equal(std::vector<std::string>{std::string{device_path::kTempoLower} + "=160",
		                                       std::string{device_path::kTempoUpper} + "=160"});
		expect(r.fakes.host.writes.requests.empty()).to_be_true();
	});

	it("bounds the key range's end by the other, as main's KeyRange does", _ {
		Rig r;
		hold(r.fakes, device_path::kKeyLower, 2);
		hold(r.fakes, device_path::kKeyUpper, 9);
		(void)r.open(nodes::default_key);
		// A key range opens on neither end; the horizontal encoder picks the left one.
		expect(static_cast<int>(menu_scene().state().edit_edge)).to_equal(static_cast<int>(deluge::ui::oled::RangeEdge::Off));
		r.horizontal(-1, 20);
		r.turn(10, 40);
		expect(last_stores(r.fakes, 2))
		    .to_equal(std::vector<std::string>{std::string{device_path::kKeyLower} + "=9",
		                                       std::string{device_path::kKeyUpper} + "=9"});
	});

	it("spells the key range's ends as the device's useSharps row says", _ {
		Rig r;
		const MenuContext ctx = r.open(nodes::default_key);
		std::array<char, deluge::ui::oled::kRangeTextMax> scratch{};
		expect(std::string{nodes::format_range_key(1, &ctx, scratch)}).to_equal(std::string{"C#"});
		hold(r.fakes, device_path::kUseSharps, 0);
		expect(std::string{nodes::format_range_key(1, &ctx, scratch)})
		    .to_equal(std::string{"D"} + deluge::gfx::oled::kFlatGlyph);
	});

	it("names the default swing interval against the magnitude row, which a resolution turn writes", _ {
		Rig r;
		hold(r.fakes, device_path::kMagnitude, 1);
		const MenuContext ctx = r.open(nodes::default_magnitude);
		std::array<char, 32> buf{};
		expect(std::string{menu::sync_level_name(nodes::default_swing_interval, ctx, 5, buf)})
		    .to_equal(std::string{"4th-notes"});
		r.turn(2, 20);
		expect(last_stores(r.fakes, 1))
		    .to_equal(std::vector<std::string>{std::string{device_path::kMagnitude} + "=3"});
		expect(std::string{menu::sync_level_name(nodes::default_swing_interval, ctx, 5, buf)})
		    .to_equal(std::string{"16th-notes"});
	});

	it("lists every scale name but the user scale's, and stores the chosen one's index", _ {
		Rig r;
		const MenuContext ctx = r.open(nodes::default_scales);
		std::vector<std::string> want;
		for (size_t i = 0; i < deluge::music::NUM_SCALELIKE; ++i) {
			if (i != deluge::music::USER_SCALE) {
				want.emplace_back(deluge::music::scalelikeNames[i]);
			}
		}
		expect(names_of(nodes::default_scale, ctx)).to_equal(want);
		expect(menu::bindings::read(nodes::default_scale, ctx)).to_equal(0);
		menu::bindings::write(nodes::default_scale, ctx, 16);
		expect(last_stores(r.fakes, 1)).to_equal(std::vector<std::string>{std::string{device_path::kScale} + "=random"});
		expect(menu::bindings::read(nodes::default_scale, ctx)).to_equal(16);
	});

	it("shows a stored number past its row's range as it is, as main's number widget does", _ {
		Rig r;
		const MenuContext ctx = r.open(nodes::defaults_settings);
		hold(r.fakes, device_path::kVelocity, 200);
		expect(menu::bindings::read(nodes::default_velocity, ctx)).to_equal(200);
		expect(menu::bindings::range(nodes::default_velocity, ctx).max).to_equal(127);
	});

	it("names the threshold mode, the startup song, the slice mode and the new clip type as their items do", _ {
		Rig r;
		const MenuContext ctx = r.open(nodes::defaults_settings);
		expect(names_of(nodes::default_threshold_recording_mode, ctx))
		    .to_equal(std::vector<std::string>{"Disabled", "Low", "Medium", "High"});
		expect(names_of(nodes::default_new_clip_type, ctx))
		    .to_equal(std::vector<std::string>{"SYNTH", "KIT", "MIDI", "CV", "AUDIO"});
		expect(names_of(nodes::default_slice_mode, ctx).size()).to_equal(size_t{4});
		expect(names_of(nodes::default_startup_song, ctx).size()).to_equal(size_t{4});
		// A new clip type the menu does not offer (the flash's NONE) shows as the last, as main's list clamps it.
		hold(r.fakes, device_path::kNewClipType, 255);
		expect(menu::bindings::read(nodes::default_new_clip_type, ctx)).to_equal(4);
		expect(r.fakes.host.device.stores.empty()).to_be_true();
	});

	it("names the pads' and the screen's choices as their items do, each read from the fake", _ {
		Rig r;
		const MenuContext ctx = r.open(nodes::pads_settings);
		expect(names_of(nodes::pads_keyboard_layout, ctx))
		    .to_equal(std::vector<std::string>{"QWERTY", "AZERTY", "QWERTZ"});
		expect(names_of(nodes::pad_colour_active, ctx).size()).to_equal(size_t{9});
		// Each colour's default is its slot's: green, red, yellow, blue, amber, magenta.
		const std::array<std::pair<const Node*, int32_t>, 6> colours{{
		    {&nodes::pad_colour_active, 1},
		    {&nodes::pad_colour_stopped, 0},
		    {&nodes::pad_colour_muted, 3},
		    {&nodes::pad_colour_soloed, 2},
		    {&nodes::pad_colour_fill, 6},
		    {&nodes::pad_colour_once, 5},
		}};
		for (const auto& [node, colour] : colours) {
			expect(menu::bindings::read(*node, ctx)).to_equal(colour);
		}
		expect(names_of(nodes::screensaver_mode, ctx).size()).to_equal(size_t{4});
		expect(menu::bindings::read(nodes::screensaver_mode, ctx)).to_equal(3);
		expect(menu::bindings::read(nodes::screensaver_timeout, ctx)).to_equal(5);
		expect(names_of(nodes::default_menu_highlighting, ctx).size()).to_equal(size_t{3});
		expect(menu::bindings::read(nodes::default_menu_highlighting, ctx)).to_equal(1);
		expect(names_of(nodes::sample_browser_preview, ctx).size()).to_equal(size_t{3});
		expect(menu::bindings::read(nodes::default_kb_velocity_glide, ctx)).to_equal(1);
		menu::bindings::write(nodes::pad_colour_muted, ctx, 8);
		expect(last_stores(r.fakes, 1))
		    .to_equal(std::vector<std::string>{std::string{device_path::kPadColourMuted} + "=pink"});
		expect(r.fakes.host.writes.requests.empty()).to_be_true();
	});

	it("shows a pad-grid layout the menu does not list as the last it lists, as main's list clamps it", _ {
		Rig r;
		// The device's chord layout, the fourth of its seven: the menu lists the first three.
		hold(r.fakes, device_path::kKeyboardLayout, 3);
		const MenuContext ctx = r.open(nodes::default_ui_keyboard);
		expect(row_of(nodes::default_ui_keyboard, nodes::default_keyboard_layout, ctx)).to_equal(0);
		r.select(10);
		expect(menu_scene().focused()).to_equal(&nodes::default_keyboard_layout);
		expect(names_of(nodes::default_keyboard_layout, ctx))
		    .to_equal(std::vector<std::string>{"Isomorphic", "In-Key", "Piano"});
		expect(static_cast<int32_t>(menu_scene().state().enum_index)).to_equal(2);
		r.shell.tick(40);
		// Showing it writes nothing.
		expect(r.fakes.host.device.stores.empty()).to_be_true();
		// A turn down picks the one before it.
		r.turn(-1, 60);
		expect(last_stores(r.fakes, 1))
		    .to_equal(std::vector<std::string>{std::string{device_path::kKeyboardLayout} + "=inKey"});
	});

	it("shows the pad brightness at four times its level and turns it four at a time, within 4 and 100", _ {
		Rig r;
		hold(r.fakes, device_path::kPadBrightness, 10);
		const MenuContext ctx = r.open(nodes::defaults_settings);
		expect(menu::bindings::read(nodes::default_pad_brightness, ctx)).to_equal(40);
		r.turn(row_of(nodes::defaults_settings, nodes::default_pad_brightness, ctx), 10);
		r.select(30);
		expect(menu_scene().focused()).to_equal(&nodes::default_pad_brightness);
		r.turn(1, 50);
		expect(last_stores(r.fakes, 1))
		    .to_equal(std::vector<std::string>{std::string{device_path::kPadBrightness} + "=11"});
		expect(menu_scene().state().edit_int).to_equal(44);
		// The flash's level 0, a device's with no saved settings, shows as 0; a turn down from it lands on the dimmest.
		hold(r.fakes, device_path::kPadBrightness, 0);
		expect(menu::bindings::read(nodes::default_pad_brightness, ctx)).to_equal(0);
		expect(nodes::pad_brightness_turn(ctx, 0, -1)).to_equal(std::optional<int32_t>{4});
		expect(nodes::pad_brightness_turn(ctx, 100, 1)).to_equal(std::optional<int32_t>{100});
		expect(nodes::pad_brightness_turn(ctx, 120, -1)).to_equal(std::optional<int32_t>{100});
	});

	it("leaves the open menu's layout as it was when the port refuses a horizontal-menus write", _ {
		Rig r;
		(void)r.open(nodes::voice_usage);
		r.fakes.host.device.refuse.insert(std::string{device_path::feature(Feature::HORIZONTAL_MENUS)});
		menu::bindings::write(nodes::feature_horizontal_menus, menu_scene().menu_context(), 0);
		expect(r.fakes.host.device.stores.size()).to_equal(size_t{1});
		expect(menu::is_horizontal(menu_scene().menu_context(), nodes::voice_usage)).to_be_true();
	});
});
// clang-format on

DELUGE_SPEC(menu_settings_pages_fakes_spec, menu_settings_pages_fakes)
