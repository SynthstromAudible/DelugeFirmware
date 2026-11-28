/*
 * Copyright © 2026 Synthstrom Audible Limited
 *
 * This file is part of The Synthstrom Audible Deluge Firmware.
 *
 * The Synthstrom Audible Deluge Firmware is free software: you can redistribute it and/or modify it under the
 * terms of the GNU General Public License as published by the Free Software Foundation,
 * either version 3 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY;
 * without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along with this program.
 * If not, see <https://www.gnu.org/licenses/>.
 */

#pragma once

/// @file
/// @brief A legacy root view whose body is a scene: the scene sits at the bottom of the shared shell's stack, and the
///        view feeds it, ticks the shell and serves what the shell drew to legacy's renderers.

#include "base/notify/dirty_set.h"
#include "gui/ui/root_ui.h"
#include "gui/ui/shell_host/shell_host.h"
#include "gui/ui/shell_runtime.h"
#include "gui/ui/ui.h"
#include "hid/button.h"
#include "hid/encoder_input.h"
#include "hid/led/pad_leds.h"
#include "model/clip/clip_minder.h"
#include "model/edits/clip_address_for.h"
#include "model/song/song.h"
#include "ui/shell/scene.h"
#include "ui/widgets/grid/effects.h"

#include <algorithm>
#include <cstdint>

namespace deluge::gui::ui {

/// @brief What a root view answers about itself when legacy code asks it, which its scene cannot: the legacy view's
///        own answers.
struct RootViewTraits {
	/// What `getUIType()` answers, which legacy code compares.
	UIType type;
	/// What the gold knobs edit while the view is the root (`getUIModControllableContext`).
	UIModControllableContext knobs = UIModControllableContext::NONE;
	/// Whether the view is a clip screen: `toClipMinder()` answers its own `ClipMinder`, which is what
	/// `rootUIIsClipMinderScreen()` and the clip-screen arms of the menus, knobs and undo ask.
	bool clip_minder = false;
	/// `supportsTriplets()`.
	bool triplets = true;
};

/// @brief A legacy root view whose body is a scene.
///
/// The scene is the root of the shared shell (`ShellRuntime`): a menu the guest opens over this view goes on top of
/// it, and closes back to it. While this view is the current UI it feeds the shell like the guest does; while a menu
/// is open over it, the input the menu leaves (a pad outside its regions) reaches the root scene directly. The gold
/// knobs and the mod buttons are `root_knobs`' (the `UI` defaults), as under every legacy root view.
/// It is not a `TimelineView`: `toTimelineView()` answers null, so legacy's timeline arms (undo's scroll animation,
/// the triplets LED) are told apart and answered by the scene instead.
/// @tparam SceneT A root scene (`is_root()`) with `open(std::optional<project::ClipAddress>, const project::Song*)`
///                and `clip()`, the clip it shows.
template <typename SceneT>
class ShellRootView final : public RootUI {
public:
	/// @brief What the root view does to the shared legacy `View` as its focus comes back (the knobs onto its clip),
	///        which the scene, below `View`, cannot.
	using FocusHook = void (*)();

	/// @param scene    The scene's singleton.
	/// @param kind     The kind it is registered under.
	/// @param traits   The legacy view's own answers about itself.
	/// @param focus    The `View` half of a focus coming back, or null.
	/// @param runtime  The shell it hosts the scene on.
	ShellRootView(SceneT& (*scene)(), deluge::ui::shell::SceneKind kind, RootViewTraits traits, FocusHook focus,
	              ShellRuntime& (*runtime)() = &shellRuntime)
	    : scene_(scene), kind_(kind), traits_(traits), focus_(focus), runtime_(runtime) {}

	/// @return The scene.
	[[nodiscard]] SceneT& scene() const { return scene_(); }

	bool opened() override {
		ShellRuntime& rt = runtime_();
		::Clip* clip = currentSong != nullptr ? currentSong->getCurrentClip() : nullptr;
		scene().open(clip != nullptr ? deluge::edits::clipAddressFor(*clip) : std::nullopt,
		             currentSong != nullptr ? &currentSong->project : nullptr);
		if (!rt.shell().stack().replace_root(deluge::ui::shell::SceneId{kind_, 0})) {
			return false;
		}
		rt.setRootHost(this);
		rt.shell().boot();
		greyout_ = {};
		focusRegained();
		return true;
	}

	void focusRegained() override {
		if (focus_ != nullptr) {
			focus_();
		}
		ShellRuntime& rt = runtime_();
		// The view showed whichever clip is current; undo comes back to it on another clip without opening it again.
		if (currentSong != nullptr) {
			::Clip* clip = currentSong->getCurrentClip();
			const std::optional<deluge::project::ClipAddress> at =
			    clip != nullptr ? deluge::edits::clipAddressFor(*clip) : std::nullopt;
			if (at != scene().clip()) {
				scene().open(at, &currentSong->project);
			}
		}
		if (rt.shell().stack().depth() > 0) {
			rt.shell().stack().input_top().on_resume();
		}
		// Legacy has drawn the pads and LEDs since the scene last sent them (the view before, a UI over this one), so
		// the next frame sends them all again.
		rt.surfaces().begin_session();
		deluge::base::notify::raise(deluge::base::notify::Dirty::GridMain);
		deluge::base::notify::raise(deluge::base::notify::Dirty::GridSidebar);
		ShellRuntime::armTickSoon();
	}

	/// @brief The song's clip scroll and zoom changed under the view (an undo): the scene takes them, sliding, as
	///        `TimelineView::initiateXScroll` and `initiateXZoom` did.
	void navigationRestored() {
		scene().navigation_restored();
		ShellRuntime::armTickSoon();
	}

	ActionResult buttonAction(deluge::hid::Button b, bool on) override {
		const Cartesian xy = deluge::hid::button::toXY(b);
		const DelugeInputEvent ev{DELUGE_EVENT_BUTTON, static_cast<uint8_t>(xy.x), static_cast<uint8_t>(xy.y),
		                          static_cast<int16_t>(on ? 255 : 0)};
		if (!current()) {
			// Passed down from a menu open over this view: the root scene's alone.
			(void)runtime_().shell().route_to_root(
			    deluge::ui::shell::UiEvent{
			        .kind = deluge::ui::shell::UiEvent::Kind::Button, .on = on, .button = buttonAt(xy.x, xy.y)},
			    runtime_().nowMs());
			return ActionResult::DEALT_WITH;
		}
		if (b == deluge::hid::button::MOD_ENCODER_0 || b == deluge::hid::button::MOD_ENCODER_1) {
			// The gold knobs are `root_knobs`', as under every legacy root view: `Buttons::buttonAction`'s tail hands
			// the push to `modEncoderButtonAction`. The scene sees it as the press it is.
			(void)runtime_().shell().route_to_root(
			    deluge::ui::shell::UiEvent{
			        .kind = deluge::ui::shell::UiEvent::Kind::EncoderButton, .on = on, .button = buttonAt(xy.x, xy.y)},
			    runtime_().nowMs());
			ShellRuntime::armTickSoon();
			return ActionResult::NOT_DEALT_WITH;
		}
		runtime_().feed(ev);
		ShellRuntime::armTickSoon();
		// As the guest: the transport and SHIFT arms after the current UI's are legacy's tail, which the shell does
		// not take.
		return ActionResult::NOT_DEALT_WITH;
	}

	ActionResult padAction(int32_t x, int32_t y, int32_t velocity) override {
		if (!current()) {
			(void)runtime_().shell().route_to_root(
			    deluge::ui::shell::UiEvent{.kind = deluge::ui::shell::UiEvent::Kind::Pad,
			                               .x = static_cast<uint8_t>(x),
			                               .y = static_cast<uint8_t>(y),
			                               .velocity = static_cast<uint8_t>(std::clamp<int32_t>(velocity, 0, 127)),
			                               .on = velocity != 0},
			    runtime_().nowMs());
			return ActionResult::DEALT_WITH;
		}
		runtime_().feedPad(static_cast<uint8_t>(x), static_cast<uint8_t>(y), velocity, false);
		ShellRuntime::armTickSoon();
		return ActionResult::DEALT_WITH;
	}

	ActionResult horizontalEncoderAction(int32_t offset) override {
		feedTurn(deluge::ui::widgets::core::EncoderRole::Horizontal, offset);
		return ActionResult::DEALT_WITH;
	}

	ActionResult verticalEncoderAction(int32_t offset) override {
		feedTurn(deluge::ui::widgets::core::EncoderRole::Vertical, offset);
		return ActionResult::DEALT_WITH;
	}

	void selectEncoderAction(int8_t offset) override {
		runtime_().feedDetents(deluge::ui::widgets::core::EncoderRole::Select, offset,
		                       deluge::hid::encoders::selectTurnTimeUs());
		ShellRuntime::armTickSoon();
	}

	/// @brief Nothing: the scene draws on the shell's frames (`Scene::frame_driven`). `UI::graphicsRoutine` would
	/// forward
	///        to the root view, which is this one.
	void graphicsRoutine() override {}

	ActionResult timerCallback() override {
		ShellRuntime& rt = runtime_();
		const uint32_t now = rt.nowMs();
		// No owner: the notices are legacy's display's, as under every legacy root view (`notify_drain`).
		rt.tick(now, nullptr);
		// After the tick, which drew and sent the scene's frame, so a browser a press opened draws over it.
		deluge::gui::shell_host::dispatchRootViewOpens();
		rt.rearm(now, this);
		reassessGreyout();
		return ActionResult::DEALT_WITH;
	}

	bool renderMainPads(uint32_t whichRows, RGB image[][kDisplayWidth + kSideBarWidth],
	                    uint8_t occupancyMask[][kDisplayWidth + kSideBarWidth], bool drawUndefinedArea) override {
		(void)occupancyMask;
		(void)drawUndefinedArea;
		runtime_().copyGrid(whichRows, image, 0, kDisplayWidth);
		return true;
	}

	bool renderSidebar(uint32_t whichRows, RGB image[][kDisplayWidth + kSideBarWidth],
	                   uint8_t occupancyMask[][kDisplayWidth + kSideBarWidth]) override {
		if (image == nullptr) {
			return true;
		}
		runtime_().copyGrid(whichRows, image, kDisplayWidth, kDisplayWidth + kSideBarWidth);
		if (occupancyMask != nullptr) {
			for (int32_t y = 0; y < kDisplayHeight; y++) {
				PadLEDs::refreshSidebarOccupancy(image[y], occupancyMask[y]);
			}
		}
		return true;
	}

	/// @brief The root scene's greyout while this view is the current UI (`ClipView::getGreyoutColsAndRows` asks
	///        `getCurrentUI() == this`); false, "obey the UI under me", otherwise or when it greys nothing.
	bool getGreyoutColsAndRows(uint32_t* cols, uint32_t* rows) override {
		const deluge::ui::widgets::grid::GreyoutMask mask = greyoutMask();
		if (mask.cols == 0 && mask.rows == 0) {
			return false;
		}
		*cols = mask.cols;
		*rows = mask.rows;
		return true;
	}

	ClipMinder* toClipMinder() override { return traits_.clip_minder ? &minder_ : nullptr; }
	[[nodiscard]] bool supportsTriplets() const override { return traits_.triplets; }
	UIModControllableContext getUIModControllableContext() override { return traits_.knobs; }

	/// @brief Redraw requests: the scene takes the model's changes itself (`Shell::apply_dirty`), so each only wakes
	/// the
	///        shell.
	/// @{
	void playbackEnded() override { ShellRuntime::armTickSoon(); }
	void clipNeedsReRendering(Clip* clip) override {
		(void)clip;
		ShellRuntime::armTickSoon();
	}
	void sampleNeedsReRendering(Sample* sample) override {
		(void)sample;
		ShellRuntime::armTickSoon();
	}
	void midiLearnFlash() override { ShellRuntime::armTickSoon(); }
	/// @}

	void renderOLED(deluge::hid::display::oled_canvas::Canvas& canvas) override { runtime_().copyOled(canvas); }

	UIType getUIType() override { return traits_.type; }

private:
	/// @return The profile's button at matrix cell (@p x, @p y), or `None`.
	[[nodiscard]] deluge::ui::shell::ButtonId buttonAt(int32_t x, int32_t y) const {
		const deluge::ui::shell::BoardProfile& p = runtime_().shell().lowering().profile();
		const auto i = static_cast<size_t>(y) * p.button_cols + static_cast<size_t>(x);
		return x >= 0 && y >= 0 && i < p.button_by_matrix.size() ? p.button_by_matrix[i]
		                                                         : deluge::ui::shell::ButtonId::None;
	}

	/// @return Whether this view is the current UI (nothing open over it).
	[[nodiscard]] bool current() const { return getCurrentUI() == this; }

	/// @return The root scene's greyout while this view is current, else nothing.
	[[nodiscard]] deluge::ui::widgets::grid::GreyoutMask greyoutMask() const {
		const deluge::ui::shell::Scene* root = runtime_().shell().stack().root();
		return current() && root != nullptr ? root->greyout_mask() : deluge::ui::widgets::grid::GreyoutMask{};
	}

	/// @brief Tell the pad driver when the greyout changed, as legacy's views do through `reassessGreyout`: at once
	///        where the scene says so (an overview appearing), fading otherwise.
	void reassessGreyout() {
		const deluge::ui::widgets::grid::GreyoutMask now = greyoutMask();
		if (now.cols == greyout_.cols && now.rows == greyout_.rows) {
			return;
		}
		greyout_ = now;
		const deluge::ui::shell::Scene* root = runtime_().shell().stack().root();
		PadLEDs::reassessGreyout(root != nullptr && root->greyout_changes_instantly());
	}

	/// @brief An encoder turn: fed while current, otherwise the root scene's alone.
	void feedTurn(deluge::ui::widgets::core::EncoderRole role, int32_t offset) {
		if (!current()) {
			(void)runtime_().shell().route_to_root(
			    deluge::ui::shell::UiEvent{.kind = deluge::ui::shell::UiEvent::Kind::Encoder,
			                               .encoder = role,
			                               .delta = static_cast<int8_t>(std::clamp<int32_t>(offset, -127, 127))},
			    runtime_().nowMs());
			return;
		}
		runtime_().feedDetents(role, offset);
		ShellRuntime::armTickSoon();
	}

	SceneT& (*scene_)();
	deluge::ui::shell::SceneKind kind_;
	RootViewTraits traits_;
	FocusHook focus_;
	ShellRuntime& (*runtime_)();
	/// What `toClipMinder()` answers when the view is a clip screen.
	ClipMinder minder_;
	/// The greyout the pad driver was last told of.
	deluge::ui::widgets::grid::GreyoutMask greyout_{};
};

} // namespace deluge::gui::ui
