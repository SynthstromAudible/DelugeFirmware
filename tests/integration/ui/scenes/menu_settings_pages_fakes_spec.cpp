/// @file
/// @brief The settings pages' DEVICE rows, driven through a real `MenuScene` and `Shell` over the ports' fakes, with no
///        song behind the menu: what a page shows is the fake's value, and what a turn writes reaches the fake's
///        store.

#include "ui/scenes/bind/device_reader.h"
#include "ui/scenes/bind/ports/device_settings.h"
#include "ui/scenes/bind/ports/midi_devices.h"
#include "ui/scenes/menu/bindings.h"
#include "ui/scenes/menu/context.h"
#include "ui/scenes/menu/menu_scene.h"
#include "ui/scenes/menu/node.h"
#include "ui/scenes/menu/node_id.h"
#include "ui/scenes/menu/nodes/midi_command.h"
#include "ui/scenes/menu/nodes/multi_range.h"
#include "ui/scenes/menu/nodes/nodes.h"
#include "ui/scenes/menu/nodes/settings.h"
#include "ui/scenes/menu/present.h"
#include "ui/scenes/menu/walk.h"
#include "ui/scenes/profile.h"
#include "ui/scenes/registry.h"

#include "base/notify/dirty_set.h"
#include "base/notify/notice_queue.h"
#include "gui/l10n/l10n.h"
#include "instruments/classic/sound_state.h"
#include "instruments/classic/state_access.h"
#include "midi/device_id.h"
#include "music/preset_scales.h"
#include "project/device_settings.h"
#include "project/features.h"
#include "project/instrument_slot.h"
#include "project/midi_command.h"
#include "ui/gfx/oled/glyphs.h"
#include "ui/shell/profile.h"
#include "ui/shell/profiles/deluge.h"
#include "ui/shell/scene.h"
#include "ui/shell/shell.h"
#include "ui/widgets/core/input.h"

#include "support/fake_host_rig.h"
#include "support/fake_menu_shell.h"
#include "support/mocks/fake_surfaces.h"
#include "support/popup_capture.h"
#include "support/printers/learned_binding_printer.h"

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

using namespace deluge::ui::scenes;
using deluge::ui::scenes::bind::LearnedBinding;
using deluge::ui::scenes::menu::MenuContext;
using deluge::ui::scenes::menu::Node;
using deluge::ui::scenes::spec::FakeMenuShell;
using device_path = deluge::project::device_path;
using deluge::project::Feature;

namespace {

namespace nodes = deluge::ui::scenes::menu::nodes;

using spec::row_of;

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

/// @brief One MIDI message of @p kind from the DIN port, on channel 4 with velocity or value 100.
/// @param kind  The message's kind.
/// @param data1 Its note or CC number.
deluge::ui::shell::UiEvent din_message(deluge::ui::shell::UiEvent::Kind kind, uint8_t data1) {
	return deluge::ui::shell::UiEvent{
	    .kind = kind, .channel = 4, .data1 = data1, .data2 = 100, .device = deluge::midi::kDin};
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
		FakeMenuShell r;
		const MenuContext ctx = r.open_settings(nodes::defaults_settings);
		const std::optional<int32_t> row = row_of(nodes::defaults_settings, nodes::default_patch_cable_polarity, ctx);
		expect(row.has_value()).to_be_true();
		r.turn(row.value_or(0), 10);
		expect(static_cast<int32_t>(menu_scene().state().list[0].cursor)).to_equal(row.value_or(0));

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
		FakeMenuShell r;
		const MenuContext ctx = r.open_settings(nodes::defaults_settings);
		r.turn(row_of(nodes::defaults_settings, nodes::default_patch_cable_polarity, ctx).value_or(0), 10);
		r.select(30);
		expect(menu_scene().focused()).to_equal(&nodes::default_patch_cable_polarity);
		(void)deluge::base::notify::dirty().drain();
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
		FakeMenuShell r;
		const MenuContext ctx = r.open_settings(nodes::defaults_settings);
		// The row forced to a word it cannot name: it reads as the default, and a value past its list is refused.
		r.fakes.host.device.values.insert_or_assign(std::string{device_path::kPatchCablePolarity},
		                                            deluge::serialization::Value::makeScalar("tripolar"));
		expect(menu::bindings::read(nodes::default_patch_cable_polarity, ctx)).to_equal(1);
		(void)deluge::base::notify::dirty().drain();
		menu::bindings::write(nodes::default_patch_cable_polarity, ctx, 2);
		menu::bindings::write(nodes::default_patch_cable_polarity, ctx, -1);
		expect(r.fakes.host.device.stores.empty()).to_be_true();
		expect(r.fakes.host.device.values.at(std::string{device_path::kPatchCablePolarity}).str())
			.to_equal(std::string{"tripolar"});
		expect(deluge::base::notify::dirty().drain().has(deluge::base::notify::Dirty::Oled)).to_be_false();
	});

	it("raises nothing for a write the device port refuses", _ {
		FakeMenuShell r;
		const MenuContext ctx = r.open_settings(nodes::defaults_settings);
		r.fakes.host.device.refuse.insert(std::string{device_path::kPatchCablePolarity});
		(void)deluge::base::notify::dirty().drain();
		deluge::ui::scenes::spec::PopupCapture popups;
		menu::bindings::write(nodes::default_patch_cable_polarity, ctx, 0);
		// The write reached the port, which kept nothing and said so.
		expect(r.fakes.host.device.stores.size()).to_equal(size_t{1});
		expect(menu::bindings::read(nodes::default_patch_cable_polarity, ctx)).to_equal(1);
		expect(deluge::base::notify::dirty().drain().has(deluge::base::notify::Dirty::Oled)).to_be_false();
		expect(popups.seen()).to_equal(std::vector<std::string>{});
	});

	it("says restart to apply for a USB audio enable the device takes at its next start, and only then", _ {
		FakeMenuShell r;
		const MenuContext ctx = r.open_settings(nodes::usb_audio_settings);
		expect(row_of(nodes::usb_audio_settings, nodes::usb_audio_enabled, ctx)).to_equal(std::optional<int32_t>{0});
		expect(menu::bindings::read(nodes::usb_audio_enabled, ctx)).to_equal(1);
		// A write the device has in effect at once says nothing.
		deluge::ui::scenes::spec::PopupCapture popups;
		menu::bindings::write(nodes::usb_audio_enabled, ctx, 0);
		expect(popups.seen()).to_equal(std::vector<std::string>{});
		// One it takes only at its next start says so.
		r.fakes.host.device.accept_restart.insert(std::string{device_path::kUsbAudioEnabled});
		menu::bindings::write(nodes::usb_audio_enabled, ctx, 1);
		expect(last_stores(r.fakes, 2))
		    .to_equal(std::vector<std::string>{std::string{device_path::kUsbAudioEnabled} + "=0",
		                                       std::string{device_path::kUsbAudioEnabled} + "=1"});
		expect(popups.seen())
		    .to_equal(std::vector<std::string>{
		        "popup: " + std::string(deluge::l10n::get(deluge::l10n::String::STRING_FOR_RESTART_TO_APPLY))});
		expect(r.fakes.host.writes.requests.empty()).to_be_true();
	});

	it("names the audio and clock choices as their items do, and reads each row from the fake", _ {
		FakeMenuShell r;
		const MenuContext ctx = r.open_settings(nodes::root_settings);
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
		FakeMenuShell r;
		const MenuContext ctx = r.open_settings(nodes::root_settings);
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
		FakeMenuShell r;
		const MenuContext ctx = r.open_settings(nodes::root_settings);
		expect(row_of(nodes::root_settings, nodes::flash_status, ctx).has_value()).to_be_true();
		expect(menu::bindings::read(nodes::flash_status, ctx))
			.to_equal(static_cast<int32_t>(deluge::project::FlashCursor::SLOW));
		expect(names_of(nodes::flash_status, ctx)).to_equal(std::vector<std::string>{"Fast", "Disabled", "Slow"});
		expect(menu::bindings::range(nodes::flash_status, ctx).max).to_equal(2);
	});

	it("lists every community feature on its page, in the page's order, each read from the fake", _ {
		FakeMenuShell r;
		const MenuContext ctx = r.open_settings(nodes::runtime_features);
		for (size_t at = 0; at < kFeatureNodes.size(); ++at) {
			const FeatureNode& want = kFeatureNodes[at];
			const deluge::project::FeatureInfo& info = deluge::project::feature_info(want.feature);
			const std::string label{info.path};
			expect(row_of(nodes::runtime_features, *want.node, ctx)).to_equal(std::optional{static_cast<int32_t>(at)}, label);
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
		FakeMenuShell r;
		const MenuContext ctx = r.open_settings(nodes::runtime_features);
		expect(names_of(nodes::feature_sync_scaling, ctx))
			.to_equal(std::vector<std::string>{"Sync Scaling", "Fill mode"});
		expect(names_of(nodes::feature_shortcut_overlay, ctx))
			.to_equal(std::vector<std::string>{"Off", "On - Menu only", "On - Menu + Clip"});
		expect(menu::bindings::range(nodes::feature_shortcut_overlay, ctx).max).to_equal(2);
	});

	it("stores a feature's write through the device port: a toggle's number, a choice's word", _ {
		FakeMenuShell r;
		const MenuContext ctx = r.open_settings(nodes::runtime_features);
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
		FakeMenuShell r;
		(void)r.open_settings(nodes::voice_usage);
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
		FakeMenuShell r;
		MenuContext ctx = r.fakes.settings_context(/*with_model=*/false);
		ctx.addresses.slot = deluge::project::SlotAddress{.kind = deluge::project::SlotAddress::Kind::OUTPUT, .id = 7};
		ctx.source_index = 0;
		deluge::instruments::classic::SoundState& state = r.fakes.host.sound.states[7];
		state = deluge::instruments::classic::with_ranges(state, deluge::instruments::classic::SourceId::A,
		                                        {deluge::synth::SourceRange{.top_note = 60},
		                                         deluge::synth::SourceRange{.top_note = 72}});
		// Range 1 starts at the note above range 0's top, 61: C sharp 3 or D flat 3.
		std::array<char, 32> buf{};
		deluge::ui::shell::TextScratch scratch;
		expect(std::string{nodes::range_row_label(ctx, 1, buf.data(), buf.size(), scratch)})
			.to_equal(std::string{"C#3    - top"});
		r.fakes.host.device.values.insert_or_assign(std::string{device_path::kUseSharps},
		                                            deluge::serialization::Value::makeNumber(0));
		expect(std::string{nodes::range_row_label(ctx, 1, buf.data(), buf.size(), scratch)})
			.to_equal(std::string{"D"} + deluge::ui::gfx::oled::kFlatGlyph + "3    - top");
		r.fakes.host.device.values.insert_or_assign(std::string{device_path::kUseSharps},
		                                            deluge::serialization::Value::makeNumber(1));
		expect(std::string{nodes::range_row_label(ctx, 1, buf.data(), buf.size(), scratch)})
			.to_equal(std::string{"C#3    - top"});
		expect(r.fakes.host.device.stores.empty()).to_be_true();
	});

	it("lists the battery level only while the device's showBatteryLevel row is on", _ {
		FakeMenuShell r;
		const MenuContext ctx = r.open_settings(nodes::root_settings);
		expect(nodes::battery_level.relevant(ctx)).to_be_true();
		expect(row_of(nodes::root_settings, nodes::battery_level, ctx).has_value()).to_be_true();
		const std::string battery{device_path::feature(Feature::SHOW_BATTERY_LEVEL)};
		r.fakes.host.device.values.insert_or_assign(battery, deluge::serialization::Value::makeNumber(0));
		expect(nodes::battery_level.relevant(ctx)).to_be_false();
		expect(row_of(nodes::root_settings, nodes::battery_level, ctx).has_value()).to_be_false();
		r.fakes.host.device.values.insert_or_assign(battery, deluge::serialization::Value::makeNumber(1));
		expect(nodes::battery_level.relevant(ctx)).to_be_true();
		// With no device port the readout follows its row's default, which is on.
		MenuContext unbound = ctx;
		unbound.ports.device = nullptr;
		expect(nodes::battery_level.relevant(unbound)).to_be_true();
	});

	it("pushes the tempo range's other end, as main's IntegerRange does, and stores both ends, the lower first", _ {
		FakeMenuShell r;
		hold(r.fakes, device_path::kTempoLower, 100);
		hold(r.fakes, device_path::kTempoUpper, 150);
		(void)r.open_settings(nodes::default_tempo);
		// A split range opens on its left end.
		expect(static_cast<int>(menu_scene().state().edit_edge)).to_equal(static_cast<int>(deluge::ui::widgets::oled::RangeEdge::Left));
		r.turn(60, 20);
		expect(last_stores(r.fakes, 2))
		    .to_equal(std::vector<std::string>{std::string{device_path::kTempoLower} + "=160",
		                                       std::string{device_path::kTempoUpper} + "=160"});
		expect(r.fakes.host.writes.requests.empty()).to_be_true();
	});

	it("bounds the key range's end by the other, as main's KeyRange does", _ {
		FakeMenuShell r;
		hold(r.fakes, device_path::kKeyLower, 2);
		hold(r.fakes, device_path::kKeyUpper, 9);
		(void)r.open_settings(nodes::default_key);
		// A key range opens on neither end; the horizontal encoder picks the left one.
		expect(static_cast<int>(menu_scene().state().edit_edge)).to_equal(static_cast<int>(deluge::ui::widgets::oled::RangeEdge::Off));
		r.horizontal(-1, 20);
		r.turn(10, 40);
		expect(last_stores(r.fakes, 2))
		    .to_equal(std::vector<std::string>{std::string{device_path::kKeyLower} + "=9",
		                                       std::string{device_path::kKeyUpper} + "=9"});
	});

	it("spells the key range's ends as the device's useSharps row says", _ {
		FakeMenuShell r;
		const MenuContext ctx = r.open_settings(nodes::default_key);
		std::array<char, deluge::ui::widgets::oled::kRangeTextMax> scratch{};
		expect(std::string{nodes::format_range_key(1, &ctx, scratch)}).to_equal(std::string{"C#"});
		hold(r.fakes, device_path::kUseSharps, 0);
		expect(std::string{nodes::format_range_key(1, &ctx, scratch)})
		    .to_equal(std::string{"D"} + deluge::ui::gfx::oled::kFlatGlyph);
	});

	it("names the default swing interval against the magnitude row, which a resolution turn writes", _ {
		FakeMenuShell r;
		hold(r.fakes, device_path::kMagnitude, 1);
		const MenuContext ctx = r.open_settings(nodes::default_magnitude);
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
		FakeMenuShell r;
		const MenuContext ctx = r.open_settings(nodes::default_scales);
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
		FakeMenuShell r;
		const MenuContext ctx = r.open_settings(nodes::defaults_settings);
		hold(r.fakes, device_path::kVelocity, 200);
		expect(menu::bindings::read(nodes::default_velocity, ctx)).to_equal(200);
		expect(menu::bindings::range(nodes::default_velocity, ctx).max).to_equal(127);
	});

	it("names the threshold mode, the startup song, the slice mode and the new clip type as their items do", _ {
		FakeMenuShell r;
		const MenuContext ctx = r.open_settings(nodes::defaults_settings);
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
		FakeMenuShell r;
		const MenuContext ctx = r.open_settings(nodes::pads_settings);
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
		FakeMenuShell r;
		// The device's chord layout, the fourth of its seven: the menu lists the first three.
		hold(r.fakes, device_path::kKeyboardLayout, 3);
		const MenuContext ctx = r.open_settings(nodes::default_ui_keyboard);
		expect(row_of(nodes::default_ui_keyboard, nodes::default_keyboard_layout, ctx)).to_equal(std::optional<int32_t>{0});
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
		FakeMenuShell r;
		hold(r.fakes, device_path::kPadBrightness, 10);
		const MenuContext ctx = r.open_settings(nodes::defaults_settings);
		expect(menu::bindings::read(nodes::default_pad_brightness, ctx)).to_equal(40);
		r.turn(row_of(nodes::defaults_settings, nodes::default_pad_brightness, ctx).value_or(0), 10);
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
		FakeMenuShell r;
		(void)r.open_settings(nodes::voice_usage);
		r.fakes.host.device.refuse.insert(std::string{device_path::feature(Feature::HORIZONTAL_MENUS)});
		menu::bindings::write(nodes::feature_horizontal_menus, menu_scene().menu_context(), 0);
		expect(r.fakes.host.device.stores.size()).to_equal(size_t{1});
		expect(menu::is_horizontal(menu_scene().menu_context(), nodes::voice_usage)).to_be_true();
	});

	it("reads and writes each CV output's own rows, the output the selector chose", _ {
		FakeMenuShell r;
		hold(r.fakes, device_path::cv_volts_per_octave(1).value(), 150);
		hold(r.fakes, device_path::cv_transpose(1).value(), -250);
		const MenuContext ctx = r.open_settings(nodes::root_settings);
		MenuContext first = ctx;
		first.source_index = 0;
		MenuContext second = ctx;
		second.source_index = 1;
		expect(menu::bindings::read(nodes::cv_volts, first)).to_equal(100);
		expect(menu::bindings::read(nodes::cv_volts, second)).to_equal(150);
		expect(menu::bindings::read(nodes::cv_transpose, first)).to_equal(0);
		expect(menu::bindings::read(nodes::cv_transpose, second)).to_equal(-250);
		expect(menu::bindings::range(nodes::cv_volts, second).max).to_equal(200);
		expect(menu::bindings::range(nodes::cv_transpose, second).min).to_equal(-9600);
		expect(menu::bindings::range(nodes::cv_transpose, second).max).to_equal(9600);
		menu::bindings::write(nodes::cv_transpose, second, 1234);
		menu::bindings::write(nodes::cv_volts, first, 0);
		expect(last_stores(r.fakes, 2))
		    .to_equal(std::vector<std::string>{"cv/1/transpose=1234", "cv/0/voltsPerOctave=0"});
		expect(r.fakes.host.writes.requests.empty()).to_be_true();
	});

	it("reads each gate output's own mode, and offers the clock on the clock's gate and the run signal on the run's", _ {
		FakeMenuShell r;
		hold(r.fakes, device_path::gate_mode(1).value(), 1);
		hold(r.fakes, device_path::gate_mode(deluge::project::kRunGateOutput).value(), 2);
		const MenuContext ctx = r.open_settings(nodes::root_settings);
		const auto at = [&](size_t output) {
			MenuContext out = ctx;
			out.source_index = static_cast<int8_t>(output);
			return out;
		};
		expect(menu::bindings::read(nodes::gate_mode, at(0))).to_equal(0);
		expect(menu::bindings::read(nodes::gate_mode, at(1))).to_equal(1);
		expect(menu::bindings::read(nodes::gate_mode, at(deluge::project::kRunGateOutput))).to_equal(2);
		expect(names_of(nodes::gate_mode, at(0))).to_equal(std::vector<std::string>{"V-Trig", "S-Trig"});
		expect(names_of(nodes::gate_mode, at(1))).to_equal(std::vector<std::string>{"V-Trig", "S-Trig"});
		expect(names_of(nodes::gate_mode, at(deluge::project::kRunGateOutput)))
		    .to_equal(std::vector<std::string>{"V-Trig", "S-Trig", "Run signal"});
		expect(names_of(nodes::gate_mode, at(deluge::project::kClockGateOutput)))
		    .to_equal(std::vector<std::string>{"V-Trig", "S-Trig", "Clock"});
		expect(menu::bindings::range(nodes::gate_mode, at(0)).max).to_equal(1);
		expect(menu::bindings::range(nodes::gate_mode, at(deluge::project::kClockGateOutput)).max).to_equal(2);
		// The clock is the clock's gate's alone: output 0 has no third choice to write.
		menu::bindings::write(nodes::gate_mode, at(0), 2);
		expect(r.fakes.host.device.stores.empty()).to_be_true();
		menu::bindings::write(nodes::gate_mode, at(deluge::project::kClockGateOutput), 2);
		menu::bindings::write(nodes::gate_off_time, ctx, 25);
		expect(last_stores(r.fakes, 2))
		    .to_equal(std::vector<std::string>{"gate/3/mode=clock", std::string{device_path::kGateMinOffTime} + "=25"});
		expect(menu::bindings::read(nodes::gate_off_time, ctx)).to_equal(25);
		expect(r.fakes.host.writes.requests.empty()).to_be_true();
	});

	it("names the MIDI follow feedback's channels as its item does, the track's alone and with each follow channel", _ {
		FakeMenuShell r;
		const MenuContext ctx = r.open_settings(nodes::midi_follow_feedback);
		expect(names_of(nodes::midi_follow_feedback_channel, ctx))
		    .to_equal(std::vector<std::string>{"None", "Channel A", "Channel B", "Channel C", "Channel Track",
		                                       "Channel Track + Channel A", "Channel Track + Channel B",
		                                       "Channel Track + Channel C"});
		expect(names_of(nodes::midi_follow_feedback_automation, ctx))
		    .to_equal(std::vector<std::string>{"Disabled", "Low", "Medium", "High"});
		expect(menu::bindings::read(nodes::midi_follow_feedback_channel, ctx)).to_equal(0);
		hold(r.fakes, device_path::kFollowFeedbackChannel, 6);
		expect(menu::bindings::read(nodes::midi_follow_feedback_channel, ctx)).to_equal(6);
		menu::bindings::write(nodes::midi_follow_feedback_channel, ctx, 7);
		menu::bindings::write(nodes::midi_follow_feedback_automation, ctx, 2);
		menu::bindings::write(nodes::midi_follow_feedback_filter, ctx, 1);
		expect(last_stores(r.fakes, 3))
		    .to_equal(std::vector<std::string>{std::string{device_path::kFollowFeedbackChannel} + "=trackAndC",
		                                       std::string{device_path::kFollowFeedbackAutomation} + "=medium",
		                                       std::string{device_path::kFollowFeedbackFilter} + "=1"});
		expect(std::to_underlying(bind::read_device<deluge::project::FeedbackAutomation>(
		           ctx.ports.device, device_path::kFollowFeedbackAutomation)))
		    .to_equal(std::to_underlying(deluge::project::FeedbackAutomation::MEDIUM));
		expect(r.fakes.host.writes.requests.empty()).to_be_true();
	});

	it("names the MIDI page's choices as their items do, and stores each of its rows through the device port", _ {
		FakeMenuShell r;
		const MenuContext ctx = r.open_settings(nodes::midi_settings);
		expect(names_of(nodes::midi_takeover, ctx))
		    .to_equal(std::vector<std::string>{"Jump", "Pickup", "Scale", "Relative"});
		expect(names_of(nodes::midi_transpose_method, ctx)).to_equal(std::vector<std::string>{"In Key", "Chromatic"});
		expect(menu::bindings::read(nodes::midi_follow_kit_root_note, ctx)).to_equal(36);
		expect(menu::bindings::range(nodes::midi_follow_kit_root_note, ctx).max).to_equal(127);
		expect(menu::bindings::read(nodes::midi_input_differentiation, ctx)).to_equal(0);
		menu::bindings::write(nodes::midi_takeover, ctx, 3);
		menu::bindings::write(nodes::midi_thru, ctx, 1);
		menu::bindings::write(nodes::midi_select_kit_row, ctx, 1);
		menu::bindings::write(nodes::midi_input_differentiation, ctx, 1);
		menu::bindings::write(nodes::midi_follow_display_param, ctx, 1);
		menu::bindings::write(nodes::midi_follow_kit_root_note, ctx, 48);
		expect(last_stores(r.fakes, 6))
		    .to_equal(std::vector<std::string>{std::string{device_path::kMidiTakeover} + "=relative",
		                                       std::string{device_path::kMidiThru} + "=1",
		                                       std::string{device_path::kMidiSelectKitRow} + "=1",
		                                       std::string{device_path::kDifferentiateInputs} + "=1",
		                                       std::string{device_path::kFollowDisplayParam} + "=1",
		                                       std::string{device_path::kFollowKitRootNote} + "=48"});
		expect(r.fakes.host.writes.requests.empty()).to_be_true();
	});

	it("learns the kit root note from a note-on as a write of its row, and nothing from a CC", _ {
		FakeMenuShell r;
		r.open_settings(nodes::midi_follow_kit_root_note);
		// Learn mode takes the CC, and the item learns nothing from it.
		(void)r.offer_while_learning(din_message(deluge::ui::shell::UiEvent::Kind::MidiCc, 20));
		expect(r.fakes.host.device.stores.empty()).to_be_true();
		expect(r.offer_while_learning(din_message(deluge::ui::shell::UiEvent::Kind::MidiNoteOn, 61))).to_be_true();
		expect(last_stores(r.fakes, 1))
		    .to_equal(std::vector<std::string>{std::string{device_path::kFollowKitRootNote} + "=61"});
		expect(r.fakes.host.device.stores.size()).to_equal(size_t{1});
		// The item on its own screen shows the note it learned.
		expect(menu_scene().state().edit_int).to_equal(61);
		expect(r.fakes.host.writes.requests.empty()).to_be_true();
	});

	it("shows each command's own binding from the MIDI devices fake, and its device by the fake's name", _ {
		using deluge::project::GlobalCommand;
		const LearnedBinding cc{.device = deluge::midi::kDin,
		                        .channel_or_zone = static_cast<uint8_t>(deluge::project::kFirstCcChannel + 3),
		                        .note_or_cc = 42};
		for (const Node* node : nodes::kMidiCommandsChildren) {
			const auto* item = dynamic_cast<const nodes::MidiCommandItem*>(node->custom);
			expect(item).not_().to_be_null();
			if (item == nullptr) {
				continue;
			}
			const auto panel = [&](std::optional<LearnedBinding> binding, GlobalCommand on) {
				FakeMenuShell r;
				if (binding) {
					r.fakes.host.midi.globals.insert_or_assign(on, *binding);
				}
				r.open_settings(*node);
				r.shell.tick(40);
				return r.surfaces.pbm(0);
			};
			// Another command's binding is not this one's: the screen is the unassigned one.
			const GlobalCommand other =
			    item->command() == GlobalCommand::SHIFT ? GlobalCommand::PLAY : GlobalCommand::SHIFT;
			const std::string unassigned = panel(std::nullopt, item->command());
			expect(panel(cc, other)).to_equal(unassigned);
			const std::string learned = panel(cc, item->command());
			expect(learned).not_().to_equal(unassigned);
			// The device is named by the fake, and a binding with none is any device's.
			LearnedBinding any = cc;
			any.device.reset();
			expect(panel(any, item->command())).not_().to_equal(learned);
		}
	});

	it("records a command's learn and a turn's unlearn on the learn sink, keeps them in the fake, and draws the page unassigned after the unlearn", _ {
		using deluge::project::GlobalCommand;
		using Call = bind::test::RecordingLearnSink::Call;
		FakeMenuShell r;
		r.open_settings(nodes::midi_command_record);
		r.shell.tick(40);
		// The page as it opens with nothing learned: the unassigned one.
		const std::string unassigned = r.oled();
		expect(r.offer_while_learning(din_message(deluge::ui::shell::UiEvent::Kind::MidiNoteOn, 61))).to_be_true();
		const LearnedBinding note{.device = deluge::midi::kDin, .channel_or_zone = 4, .note_or_cc = 61};
		const auto& calls = r.fakes.host.learn.calls;
		expect(calls.size()).to_equal(size_t{1});
		if (calls.size() == 1) {
			expect(calls[0].kind).to_equal(Call::Kind::LEARN_GLOBAL);
			expect(calls[0].global).to_equal(GlobalCommand::RECORD);
			expect(calls[0].binding).to_equal(std::optional{note});
		}
		expect(r.fakes.host.midi.global_command(GlobalCommand::RECORD)).to_equal(std::optional{note});
		// A CC is learned in its band; program changes are refused on FILL alone.
		expect(r.offer_while_learning(din_message(deluge::ui::shell::UiEvent::Kind::MidiCc, 20))).to_be_true();
		const LearnedBinding cc{.device = deluge::midi::kDin,
		                        .channel_or_zone = static_cast<uint8_t>(deluge::project::kFirstCcChannel + 4),
		                        .note_or_cc = 20};
		expect(r.fakes.host.midi.global_command(GlobalCommand::RECORD)).to_equal(std::optional{cc});
		// The learned command draws its binding, so the page drawn after the turn below is a change.
		r.shell.tick(50);
		expect(r.oled()).not_().to_equal(unassigned);
		// Any turn of the select encoder unlearns it, and the page draws it unassigned again.
		r.turn(-1, 60);
		expect(calls.size()).to_equal(size_t{3});
		if (calls.size() == 3) {
			expect(calls[2].kind).to_equal(Call::Kind::LEARN_GLOBAL);
			expect(calls[2].binding.has_value()).to_be_false();
		}
		expect(r.fakes.host.midi.global_command(GlobalCommand::RECORD).has_value()).to_be_false();
		expect(r.oled()).to_equal(unassigned);
		expect(r.fakes.host.writes.requests.empty()).to_be_true();
	});

	it("reads a follow input's channel from the fake, keeps its device on a turn, and learns and unlearns through the sink", _ {
		using deluge::project::FollowInput;
		using Call = bind::test::RecordingLearnSink::Call;
		FakeMenuShell r;
		r.fakes.host.midi.follows.insert_or_assign(FollowInput::TRACK_1,
		                                           LearnedBinding{.device = deluge::midi::kDin, .channel_or_zone = 3});
		const MenuContext ctx = r.open_settings(nodes::midi_follow_track1);
		const LearnedBinding din_4{.device = deluge::midi::kDin, .channel_or_zone = 4};
		expect(menu::bindings::read(nodes::midi_follow_track1, ctx)).to_equal(3);
		expect(menu::bindings::read(nodes::midi_follow_channel_a, ctx)).to_equal(int32_t{MIDI_CHANNEL_NONE});
		r.turn(1, 60);
		expect(r.fakes.host.midi.follow_input(FollowInput::TRACK_1)).to_equal(std::optional{din_4});
		// A note learns the device and the channel it came on.
		r.fakes.host.midi.follows.insert_or_assign(FollowInput::TRACK_1, LearnedBinding{.channel_or_zone = 9});
		expect(r.offer_while_learning(din_message(deluge::ui::shell::UiEvent::Kind::MidiNoteOn, 61))).to_be_true();
		expect(r.fakes.host.midi.follow_input(FollowInput::TRACK_1)).to_equal(std::optional{din_4});
		menu_scene().unlearn();
		expect(r.fakes.host.midi.follow_input(FollowInput::TRACK_1).has_value()).to_be_false();
		const auto& calls = r.fakes.host.learn.calls;
		expect(calls.empty()).to_be_false();
		if (!calls.empty()) {
			expect(calls.back().kind).to_equal(Call::Kind::LEARN_FOLLOW);
			expect(calls.back().follow).to_equal(FollowInput::TRACK_1);
			expect(calls.back().binding.has_value()).to_be_false();
		}
	});

	it("names a follow input's device with no channel, as main draws it, and keeps the device on a turn", _ {
		using deluge::project::FollowInput;
		// A settings file can set the device and leave the channel unassigned.
		const LearnedBinding device_only{.device = deluge::midi::kDin};
		const auto panel = [](std::optional<LearnedBinding> binding, std::string name) {
			FakeMenuShell r;
			r.fakes.host.midi.names.insert_or_assign(deluge::midi::kDin.value, std::move(name));
			if (binding) {
				r.fakes.host.midi.follows.insert_or_assign(FollowInput::TRACK_1, *binding);
			}
			r.open_settings(nodes::midi_follow_track1);
			r.shell.tick(40);
			return r.surfaces.pbm(0);
		};
		const std::string unassigned = panel(std::nullopt, "DIN");
		const std::string named = panel(device_only, "DIN");
		expect(named).not_().to_equal(unassigned);
		expect(panel(device_only, "OTHER")).not_().to_equal(named);

		FakeMenuShell r;
		r.fakes.host.midi.follows.insert_or_assign(FollowInput::TRACK_1, device_only);
		const MenuContext ctx = r.open_settings(nodes::midi_follow_track1);
		expect(menu::bindings::read(nodes::midi_follow_track1, ctx)).to_equal(int32_t{MIDI_CHANNEL_NONE});
		// A turn up from unassigned lands on channel 1 and keeps the device.
		r.turn(1, 60);
		expect(r.fakes.host.midi.follow_input(FollowInput::TRACK_1))
		    .to_equal(std::optional{LearnedBinding{.device = deluge::midi::kDin, .channel_or_zone = 0}});
	});

	it("shows the chord transpose method, which the menu does not offer, as chromatic, as main's list clamps it", _ {
		FakeMenuShell r;
		// The device's third method: the menu lists the first two.
		hold(r.fakes, device_path::kMidiTranspose, 2);
		const MenuContext ctx = r.open_settings(nodes::midi_transpose_method);
		expect(menu::bindings::read(nodes::midi_transpose_method, ctx)).to_equal(1);
		expect(static_cast<int32_t>(menu_scene().state().enum_index)).to_equal(1);
		r.shell.tick(40);
		// Showing it writes nothing.
		expect(r.fakes.host.device.stores.empty()).to_be_true();
		// A turn down picks the one before it.
		r.turn(-1, 60);
		expect(last_stores(r.fakes, 1))
		    .to_equal(std::vector<std::string>{std::string{device_path::kMidiTranspose} + "=inKey"});
	});
});
// clang-format on

DELUGE_SPEC(menu_settings_pages_fakes_spec, menu_settings_pages_fakes)
