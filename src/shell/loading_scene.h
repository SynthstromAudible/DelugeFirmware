#pragma once
#include "shell/scene.h"
#include "ui/core/state.h"

#include <cstdint>

namespace deluge::ui::shell {

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
