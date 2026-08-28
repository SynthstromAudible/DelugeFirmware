#pragma once
#include "shell/scene.h"
#include "ui/core/state.h"

#include <cstdint>
#include <string_view>

namespace deluge::ui::shell {

/// @brief Shown from power-on until the application pushes its own root scene: the toolkit title
///        "DELUGE" plus the firmware version string `Shell::boot()` was given.
///
/// Ports the presentation half of legacy's boot sequence -- `renderUIsForOled` marking the OLED
/// dirty at boot (`deluge.cpp:120,133,371`, now `raise(Dirty::Oled)`, which is `Shell::boot()`'s
/// own repaint request rather than this scene's concern) -- with an actual splash where legacy had
/// none: the pre-root window used to simply be whatever `sessionView`/the last-opened UI drew as
/// soon as it existed. Takes no input; the application's own root replaces it via `init()`/`boot()`
/// callers moving the stack on, not through anything this scene returns.
class BootScene final : public Scene {
public:
	SceneKind kind() const override { return SceneKind::Boot; }

	/// @brief Set the version string drawn under the title.
	/// @param version Firmware version text; copied into a 24-byte buffer, truncated if longer.
	void set_firmware_version(std::string_view version);

	void render_oled(SurfaceRole role, ui::oled::OledFrame& frame) override;
	Result handle(const UiEvent&, const UiContext&) override { return Result::unhandled(); }

private:
	char version_[24] = {};
};

/// @brief Shown while a song is being read from disk: the "busy" corner animation, and nothing
///        else. Input is locked while it is up -- by the shell's song-swap protocol
///        (`Shell::song_loading`), not by this scene -- so it never has to refuse anything itself.
///
/// Ports the *presentation* half of legacy's `gui/ui/load/load_song_ui.*`: the working animation
/// and the input lock, not the file browser it also draws (out of scope here; the shell only shows
/// this scene for the duration between "loading started" and "loading finished").
class LoadingScene final : public Scene {
public:
	SceneKind kind() const override { return SceneKind::Loading; }

	/// @brief Restart the working animation from its first frame on every (re-)entry, the same
	///        "zero re-arms" idiom `WorkingScene::arm()` uses for the same `Blink`.
	bool on_enter(const SceneId&) override;

	void render_oled(SurfaceRole role, ui::oled::OledFrame& frame) override;

	/// @brief This *is* the root while loading (`Shell::song_loading` resets the whole stack to
	///        just this scene), so there is nothing beneath it to be "an overlay" over --
	///        `is_grid_overlay()`'s default (false) is correct and `dims_grid()` is moot.
	/// @brief Never transparent to input: input is locked at the `InputLowering` level for the
	///        whole time this scene is up, so nothing would ever reach it either way, but the flag
	///        itself says "this scene is a real (if temporary) root", not a passive overlay like
	///        the shell's popup/console/working scenes.
	bool input_transparent() const override { return false; }
	Result handle(const UiEvent&, const UiContext&) override { return Result::unhandled(); }

private:
	Blink blink_{};
};

} // namespace deluge::ui::shell
