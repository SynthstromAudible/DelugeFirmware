#pragma once

/// @file
/// @brief `FakeMenuShell`: a real `Shell` and `MenuScene` over fake surfaces, with the menu's contexts over
///        `FakeHostRig`'s fakes, and the walk's read-backs a fake-only menu spec asserts on.

#include "scenes/menu/bindings.h"
#include "scenes/menu/context.h"
#include "scenes/menu/menu_scene.h"
#include "scenes/menu/node.h"
#include "scenes/menu/node_id.h"
#include "scenes/menu/nodes/nodes.h"
#include "scenes/menu/present.h"
#include "scenes/profile.h"
#include "scenes/registry.h"

#include "base/notify/dirty_set.h"
#include "base/notify/notice_queue.h"
#include "gui/l10n/l10n.h"
#include "gui/l10n/strings.h"
#include "shell/profiles/deluge.h"
#include "shell/scene.h"
#include "shell/shell.h"

#include "support/fake_host_rig.h"
#include "support/mocks/fake_surfaces.h"

#include <array>
#include <cstdint>
#include <optional>
#include <string>

namespace deluge::scenes::spec {

/// @brief A `Shell` over fake surfaces with the app's scene registry, and the menu's contexts over a `FakeHostRig`.
///        Each one starts the menu as a boot leaves it and pops every scene when it goes, so examples do not see
///        each other's walk.
struct FakeMenuShell {
	/// Board encoder 3 is Select (`kDelugeEncoders`); the matrix buttons are `kMatrix`'s (`shell/profiles/deluge.cpp`).
	/// @{
	static constexpr uint8_t kHorizontalEncoder = 1;
	static constexpr uint8_t kSelectEncoder = 3;
	static constexpr uint8_t kSelectEncButtonX = 4, kSelectEncButtonY = 3;
	static constexpr uint8_t kBackX = 7, kBackY = 1;
	static constexpr uint8_t kShiftX = 8, kShiftY = 0;
	static constexpr uint8_t kLearnX = 7, kLearnY = 0;
	static constexpr uint8_t kSynthX = 5, kSynthY = 0;
	static constexpr uint8_t kKitX = 5, kKitY = 1;
	static constexpr uint8_t kCrossScreenX = 6, kCrossScreenY = 2;
	/// @}

	FakeHostRig fakes;                               ///< the ports' fakes and the model song every context reads
	ui::shell::SceneRegistry reg = scene_registry(); ///< the app's scenes, so the shell can make the menu scene
	/// The Deluge board's surfaces, faked: the spec reads the drawn OLED and pads back from here.
	uitest::FakeSurfaceSet surfaces{ui::shell::kDelugeBoard, deluge_profile()};
	/// The shell the menu runs in, lowering the board events the spec sends and ticking the scene stack.
	ui::shell::Shell shell{surfaces, deluge_profile(), reg, ui::shell::Services{}, ui::shell::ShellConfig{}};

	FakeMenuShell() {
		l10n::chosenLanguage = &l10n::built_in::english;
		base::notify::Notice discard;
		while (base::notify::notices().pop(discard)) {}
		(void)base::notify::dirty().drain();
		menu_scene().reset_process_state();
	}
	FakeMenuShell(const FakeMenuShell&) = delete;
	FakeMenuShell& operator=(const FakeMenuShell&) = delete;
	~FakeMenuShell() {
		while (shell.stack().depth() > 0) {
			shell.stack().pop(ui::shell::SceneKind::Null);
		}
		menu_scene().reset_process_state();
	}

	/// @brief Open @p page on @p ctx as the one application scene, and render it once.
	/// @param page The node the menu opens on.
	/// @param ctx  The context it opens in; one of `fakes`' contexts.
	/// @param at_ms The tick's time.
	/// @return @p ctx.
	menu::MenuContext open(const menu::Node& page, const menu::MenuContext& ctx, uint32_t at_ms = 0) {
		(void)menu::assign_node_ids(menu::nodes::kAllRoots);
		menu_scene().open(page, ctx);
		shell.init(ui::shell::SceneId{ui::shell::SceneKind::FirstApp, menu::node_id(page)});
		shell.tick(at_ms);
		return ctx;
	}
	/// @brief Open @p page on the synth clip's context (`FakeHostRig::synth_context`).
	/// @param page The node the menu opens on.
	/// @return The context it opened in.
	menu::MenuContext open_synth(const menu::Node& page) { return open(page, fakes.synth_context()); }
	/// @brief Open @p page on the settings root's context with no model (`FakeHostRig::settings_context`).
	/// @param page The node the menu opens on.
	/// @return The context it opened in.
	menu::MenuContext open_settings(const menu::Node& page) {
		return open(page, fakes.settings_context(/*with_model=*/false));
	}

	/// @brief @p detents clicks of the select encoder (two edges a click), and the two ticks that render and commit
	///        the edit.
	/// @param detents The clicks, negative to turn left.
	/// @param at_ms   When the turn arrives; the ticks follow at @p at_ms + 5 and + 10.
	void turn(int32_t detents, uint32_t at_ms) {
		shell.on_board_event(
		    DelugeInputEvent{DELUGE_EVENT_ENCODER, kSelectEncoder, 0, static_cast<int16_t>(detents * 2)}, at_ms);
		shell.tick(at_ms + 5);
		shell.tick(at_ms + 10);
	}
	/// @brief @p detents clicks of the horizontal encoder, which picks a range's end, and the two ticks after it.
	/// @param detents The clicks, negative to turn left.
	/// @param at_ms   When the turn arrives; the ticks follow at @p at_ms + 5 and + 10.
	void horizontal(int32_t detents, uint32_t at_ms) {
		shell.on_board_event(
		    DelugeInputEvent{DELUGE_EVENT_ENCODER, kHorizontalEncoder, 0, static_cast<int16_t>(detents * 2)}, at_ms);
		shell.tick(at_ms + 5);
		shell.tick(at_ms + 10);
	}
	/// @brief One edge of matrix button (@p x, @p y), with no tick.
	/// @param x     The button's matrix column.
	/// @param y     The button's matrix row.
	/// @param on    True for the press, false for the release.
	/// @param at_ms When the edge arrives.
	void button(uint8_t x, uint8_t y, bool on, uint32_t at_ms) {
		shell.on_board_event(DelugeInputEvent{DELUGE_EVENT_BUTTON, x, y, static_cast<int16_t>(on ? 255 : 0)}, at_ms);
	}
	/// @brief A press and release of matrix button (@p x, @p y), each settled by a tick.
	/// @param x     The button's matrix column.
	/// @param y     The button's matrix row.
	/// @param at_ms When the press arrives; a tick follows at @p at_ms + 5, the release at + 10 and its tick at + 15.
	void press(uint8_t x, uint8_t y, uint32_t at_ms) {
		button(x, y, true, at_ms);
		shell.tick(at_ms + 5);
		button(x, y, false, at_ms + 10);
		shell.tick(at_ms + 15);
	}
	/// @brief One edge of main pad (@p x, @p y), and the tick that settles it.
	/// @param x     The pad's column.
	/// @param y     The pad's row.
	/// @param on    True for the press, false for the release.
	/// @param at_ms When the edge arrives; the tick follows at @p at_ms + 5.
	void pad(uint8_t x, uint8_t y, bool on, uint32_t at_ms) {
		shell.on_board_event(DelugeInputEvent{DELUGE_EVENT_PAD, x, y, static_cast<int16_t>(on ? 64 : 0)}, at_ms);
		shell.tick(at_ms + 5);
	}
	/// @brief SHIFT down or up, and the tick that settles it.
	/// @param down  True to press SHIFT, false to release it.
	/// @param at_ms When the edge arrives; the tick follows at @p at_ms + 5.
	void shift(bool down, uint32_t at_ms) {
		button(kShiftX, kShiftY, down, at_ms);
		shell.tick(at_ms + 5);
	}
	/// @brief SHIFT held across one press and release of main pad (@p x, @p y): a shortcut or patching-source press.
	/// @param x     The pad's column.
	/// @param y     The pad's row.
	/// @param at_ms When SHIFT goes down; the pad's edges follow at + 10 and + 20, SHIFT's release at + 30.
	void shift_pad(uint8_t x, uint8_t y, uint32_t at_ms) {
		shift(true, at_ms);
		pad(x, y, true, at_ms + 10);
		pad(x, y, false, at_ms + 20);
		shift(false, at_ms + 30);
	}
	/// @brief SHIFT held across a press of LEARN: the menu's unlearn of the focused item.
	/// @param at_ms When SHIFT goes down; LEARN's press follows at + 10 and SHIFT's release at + 30.
	void shift_learn(uint32_t at_ms) {
		shift(true, at_ms);
		press(kLearnX, kLearnY, at_ms + 10);
		shift(false, at_ms + 30);
	}
	/// @brief Press and release the select encoder, as `press` does.
	/// @param at_ms When the press arrives.
	void select(uint32_t at_ms) { press(kSelectEncButtonX, kSelectEncButtonY, at_ms); }
	/// @brief A short press of BACK, as `press` does.
	/// @param at_ms When the press arrives.
	void back(uint32_t at_ms) { press(kBackX, kBackY, at_ms); }
	/// @brief LEARN held while @p event arrives, as the menu takes a MIDI message or a gold knob in learn mode. The
	///        three events go to the menu scene directly, with no tick.
	/// @param event The event offered while LEARN is down.
	/// @return Whether the menu used @p event.
	bool offer_while_learning(const ui::shell::UiEvent& event) {
		using ui::shell::ButtonId;
		using ui::shell::UiEvent;
		const ui::shell::ButtonSet held{};
		const ui::widgets::core::PadMask pads{};
		const ui::shell::UiContext ui_ctx{held, pads, false, false, 0, deluge_profile()};
		(void)menu_scene().handle(UiEvent{.kind = UiEvent::Kind::Button, .on = true, .button = ButtonId::Learn},
		                          ui_ctx);
		const bool used = menu_scene().handle(event, ui_ctx).handled;
		(void)menu_scene().handle(UiEvent{.kind = UiEvent::Kind::Button, .on = false, .button = ButtonId::Learn},
		                          ui_ctx);
		return used;
	}
	/// @brief The main OLED as the last tick drew it, as P1 PBM text.
	[[nodiscard]] std::string oled() { return surfaces.pbm(0); }
};

/// @brief The focused page's title, as the open menu draws it.
inline std::string page_title() {
	menu::NodeText scratch;
	return std::string{menu_scene().page_title(scratch)};
}

/// @brief The English text of string @p id.
inline std::string text_of(l10n::String id) {
	return std::string{l10n::get(id)};
}

/// @brief The open menu's walk depth.
inline int32_t walk_depth() {
	return menu_scene().state().depth;
}

/// @brief The row the cursor stands on at the walk's current depth.
inline int32_t cursor_row() {
	const auto& st = menu_scene().state();
	return st.list[st.depth].cursor;
}

/// @brief Where @p node is listed on @p page in @p ctx.
/// @return Its row, or nothing when @p page does not list it.
inline std::optional<int32_t> row_of(const menu::Node& page, const menu::Node& node, const menu::MenuContext& ctx) {
	std::array<const menu::Node*, menu::kMaxDynamicChildren> rows{};
	const size_t count = menu::bindings::listed_children(page, ctx, rows, menu::bindings::ListFor::Rows);
	for (size_t i = 0; i < count; ++i) {
		if (rows[i] == &node) {
			return static_cast<int32_t>(i);
		}
	}
	return std::nullopt;
}

/// @brief The node @p page lists at @p row in @p ctx.
/// @return It, or null past the list's end.
inline const menu::Node* listed_at(const menu::Node& page, size_t row, const menu::MenuContext& ctx) {
	std::array<const menu::Node*, menu::kMaxDynamicChildren> rows{};
	const size_t count = menu::bindings::listed_children(page, ctx, rows, menu::bindings::ListFor::Rows);
	return row < count ? rows[row] : nullptr;
}

} // namespace deluge::scenes::spec
