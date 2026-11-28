/// @file
/// @brief `AudioClipScene` -- the port of `AudioClipView`'s render and navigation half -- driven
///        through a real `Shell` over `uitest::FakeSurfaceSet`, against a real `AudioClip`, a real
///        `Sample`, and against the real legacy `AudioClipView`.
///
/// @par The legacy oracle
///
/// Every comparison here runs the same state twice, each side from its own sentinel:
///
///   * the port, through the `Shell` -- `audio_clip_scene().open(clip, &song->project)` and then
///     `Shell::tick`, rendered into `FakeSurfaceSet`;
///   * legacy, through **`AudioClipView`'s own public entry points** --
///     `renderMainPads`, `renderSidebar`, `renderOLED`, `graphicsRoutine`, `padAction`,
///     `timerCallback`, `getMaxLength`, `getMaxZoom` -- over the same `Song`, whose
///     `xScroll`/`xZoom`/`loopLength` the spec sets and **both** sides then read for themselves.
///
/// Nothing the port computed is ever handed to legacy: the port reads scroll and zoom through
/// `TimelineState::adopt(song)` and legacy reads them through `currentSong->persistentView.xScroll[NAVIGATION_CLIP]`,
/// so the two derive the same view independently from the one place the spec wrote.
///
/// @par The pad comparison composes legacy the way the DEVICE does, not the way `renderMainPads` does
///
/// `AudioClipView` paints the playhead nowhere near `renderMainPads`: `graphicsRoutine`
/// (`gui/views/audio_clip_view.cpp`) hands a column to `PadLEDs::setTickSquares`, and the
/// **driver** substitutes the cursor colour on its way out, in `PadLEDs::prepareColour`
/// (`hid/led/pad_leds.cpp`, called per pad from `PadLEDs::sortLedsForCol`). So the image
/// a pad actually shows is `prepareColour(x, y, image[y][x])`, and that -- not the bare
/// `renderMainPads` output -- is what the port's single immediate-mode `render_grid` is the port of.
/// `legacy_pads()` below therefore runs legacy's own `graphicsRoutine`, legacy's own
/// `renderMainPads`, legacy's own `renderSidebar` and legacy's own `prepareColour`, all four, and
/// composes nothing itself.
///
/// It renders into `PadLEDs::image` rather than a local buffer deliberately: `renderMainPads`' scan-failure arm tests
/// `image == PadLEDs::image` (`AudioClipView::renderMainPads` in `gui/views/audio_clip_view.cpp`) and takes a
/// *different* branch for any other buffer, so a local array would be comparing the port against a path the device
/// never runs.
///
/// @par The sample fixture, and why it never touches the card
///
/// Identical in shape and reason to `marker_scene_spec.cpp`'s and `slicer_scene_spec.cpp`'s: a spec process has no
/// mounted card, so the fixture seeds the sample's peak index (`support/peak_seed.h`) with every block complete and
/// its level-0 value kept. A column on 256-frame block boundaries is answered from the index with no frame read.
///
/// @par What is NOT covered here, and why
///
///   * **The `UI_MODE_HOLDING_SONG_BUTTON` macro arm of `renderSidebar`** (`AudioClipView::renderSidebar`). Not
///     ported -- see the `@warning` in `audio_clip_scene.h`. Its gate is set by
///     `AudioClipView::buttonAction`'s `SESSION_VIEW` arm, so there
///     is nothing in this half to drive it with either.
///   * **The four `PadLEDs` transition modes** (`UI_MODE_INSTRUMENT_CLIP_COLLAPSING`,
///     `UI_MODE_IMPLODE_ANIMATION`, `UI_MODE_EXPLODE_ANIMATION`, `UI_MODE_AUDIO_CLIP_COLLAPSING`).
///     The port carries no transitions at all, so those early returns have no counterpart to
///     compare.
///   * **`AudioClipView::renderOLED` at 64 rows.** `OLED_MAIN_HEIGHT_PIXELS` is a compile-time 48
///     (`board_config.h`), so legacy cannot be run at the other geometry; the 64-row coverage
///     is a golden, exactly as `marker_scene_spec`'s and `browser_scene_spec`'s are.
///
/// @note The spec may include `gui/` and `hid/` (for the legacy view and the pad driver); the
///       library may not. Same rule `marker_scene_spec.cpp` and `slicer_scene_spec.cpp` run under.

#include "support/audio_clip_rig.h"

#include "model/consequence/consequence_audio_clip_set_sample.h"
#include "model/model_stack.h"

#include "support/spec_main.h"

namespace {
/// @brief The rig's whole grid as text, one pad's colour after another, for comparing frames.
std::string grid_text(Rig& r) {
	std::string out;
	const deluge::ui::gfx::grid::PadGrid& g = r.fake.grid().grid();
	for (uint8_t y = 0; y < 8; y++) {
		for (uint8_t x = 0; x < 18; x++) {
			const auto c = g.at(x, y);
			out += std::to_string(c.r) + ',' + std::to_string(c.g) + ',' + std::to_string(c.b) + ' ';
		}
		out += '\n';
	}
	return out;
}
} // namespace

// clang-format off
// NOLINTBEGIN(readability-function-cognitive-complexity)
describe audio_clip_scene_spec_body("scenes/audio_clip AudioClipScene", $ {
	// --- the scene's identity and predicates ----------------------------------------------------

	it("is registered at FirstApp + 9 and reports that kind", _ {
		expect(static_cast<int32_t>(kAudioClipSceneKind))
		    .to_equal(static_cast<int32_t>(deluge::ui::shell::SceneKind::FirstApp) + 9);
		expect(scene_registry().find(kAudioClipSceneKind)).to_equal(&audio_clip_scene());
		expect(audio_clip_scene().kind()).to_equal(kAudioClipSceneKind);
	});

	it("is the FIRST root scene in the registry", _ {
		// `AudioClipView` -> ... -> `TimelineView : public RootUI`. Every other registered scene is
		// a plain `UI` in legacy, so this is the only one that may claim it -- asserted over the
		// whole table so a later scene claiming it wrongly shows up here.
		const deluge::ui::shell::SceneRegistry reg = scene_registry();
		int32_t roots = 0;
		for (deluge::ui::shell::Scene* s : reg.by_kind) {
			if (s != nullptr && s->is_root()) {
				roots++;
				expect(s->kind()).to_equal(kAudioClipSceneKind);
			}
		}
		expect(roots).to_equal(1);
	});

	it("is the grid base, occludes what is beneath, dims nothing and takes input", _ {
		AudioClipScene& s = audio_clip_scene();
		expect(s.is_grid_overlay()).to_be_false();
		expect(s.occludes_grid()).to_be_true();
		expect(s.oled_transparent(deluge::ui::shell::SurfaceRole::Main)).to_be_false();
		expect(s.input_transparent()).to_be_false();
		// `greyout_mask()` is left at the base's all-zero: `getUIGreyoutColsAndRows`
		// (`gui/ui/ui.cpp`) finds no `getGreyoutColsAndRows` anywhere on this chain.
		expect(s.greyout_mask().rows).to_equal(0u);
		expect(s.greyout_mask().cols).to_equal(0u);
	});

	// --- the navigation answers -----------------------------------------------------------------

	it("answers getMaxLength and getMaxZoom exactly as legacy does, in both marker states", _ {
		Rig r;
		r.seed(Sentinel{});
		r.open();
		r.tick(0);

		// Markers down on both sides.
		expect(r.scene().max_length()).to_equal(legacy_view.getMaxLength());
		expect(r.scene().max_zoom()).to_equal(legacy_view.getMaxZoom());
		expect(r.scene().max_length()).to_equal(static_cast<uint32_t>(r.f.clip->loopLength()));

		// End marker up on both sides, each through its own front door.
		expect(raise_legacy_end_marker(r)).to_be_true();
		r.scene().set_end_marker_visible(true);
		expect(r.scene().max_length()).to_equal(legacy_view.getMaxLength());
		expect(r.scene().max_zoom()).to_equal(legacy_view.getMaxZoom());
		expect(r.scene().max_length()).to_equal(static_cast<uint32_t>(r.f.clip->loopLength()) + 1);
		lower_legacy_end_marker(r);
	});

	it("answers scrollable_and_zoomable from the clip, as setupScroll's guard does", _ {
		Rig r;
		r.seed(Sentinel{});
		r.open();
		expect(r.scene().scrollable_and_zoomable()).to_equal(r.f.clip->currentlyScrollableAndZoomable());
		expect(r.scene().scrollable_and_zoomable()).to_be_true();
	});

	it("adopts the song's scroll and zoom, with supportsTriplets false as AudioClipView has it", _ {
		Rig r;
		r.seed(Sentinel{.x_scroll = 96, .x_zoom = 24});
		r.open();
		expect(r.scene().timeline().x_scroll).to_equal(96);
		expect(static_cast<int32_t>(r.scene().timeline().x_zoom)).to_equal(24);
		// `AudioClipView::supportsTriplets()` returns false (`audio_clip_view.h`), and `adopt`
		// must not overwrite it with the song's `tripletsOn`.
		r.f.song->persistentView.tripletsOn = true;
		r.open();
		expect(r.scene().timeline().supports_triplets).to_be_false();
		expect(r.scene().timeline().in_triplets_view()).to_be_false();
		r.f.song->persistentView.tripletsOn = false;
	});

	// --- the render, against legacy, at both geometries ------------------------------------------

	it("draws the same waveform and sidebar as legacy, at 48 rows", _ {
		Rig r;
		r.seed(Sentinel{});
		r.open();
		r.tick(0);
		// 16 columns of waveform, so well over one bar per column even at the quietest.
		expect_pads_match(self, r, legacy_pads(), PadExpectation::waveform(16));
	});

	it("draws the new sample's waveform when the clip's sample changes under it", _ {
		Rig r;
		r.seed(Sentinel{});
		r.open();
		r.tick(0);
		Sample* flat = make_sample();
		peak_seed::seed_peaks(*flat, [](int32_t) { return std::pair<int8_t, int8_t>{-100, 100}; });
		r.f.clip->sampleHolder.audioFile = flat;
		deluge::base::notify::raise(
		    deluge::base::notify::Dirty::GridMain); // a repaint, which says nothing of the sample
		r.tick(10);
		expect_pads_match(self, r, legacy_pads(), PadExpectation::waveform(16));
		r.f.clip->sampleHolder.audioFile = r.sample;
	});

	it("draws the same pads as legacy at 64 rows", _ {
		// The pad grid is 8 rows on both boards -- the 64 is the OLED panel's height -- so legacy's
		// image is still the whole oracle here, and this example proves the port does not read the
		// panel geometry while painting pads.
		Rig r{board_64(), profile_64()};
		r.seed(Sentinel{});
		r.open();
		r.tick(0);
		expect_pads_match(self, r, legacy_pads(), PadExpectation::waveform(16));
	});

	it("draws the same waveform and grey region as legacy when the clip is scrolled", _ {
		// Four columns' worth of scroll. The clip's end lands at column 11 (`(768 - 192 - 1) /
		// 48`), so columns 12..15 are the grey "past the end" region -- the `drawUndefinedArea` arm of
		// `AudioClipView::renderMainPads` (`audio_clip_view.cpp`) -- and columns 0..11 are the scrolled
		// waveform.
		Rig r;
		r.seed(Sentinel{.x_scroll = 192});
		r.open();
		r.tick(0);
		// The grey region alone is 4 columns x 8 rows.
		expect_pads_match(self, r, legacy_pads(), PadExpectation::waveform(32 + 12));
	});

	it("draws the same waveform and grey region as legacy when the clip is zoomed out", _ {
		// 96 ticks per column is 65536 frames, four whole clusters. The whole clip fits in eight
		// columns, so 8..15 are grey.
		Rig r;
		r.seed(Sentinel{.x_zoom = 96});
		r.open();
		r.tick(0);
		expect_pads_match(self, r, legacy_pads(), PadExpectation::waveform(64 + 8));
	});

	it("draws the same pads as legacy for a reversed sample", _ {
		Rig r;
		r.seed(Sentinel{});
		r.f.clip->sampleControls.reversed = true;
		r.open();
		r.tick(0);
		expect_pads_match(self, r, legacy_pads(), PadExpectation::waveform(16));
		r.f.clip->sampleControls.reversed = false;
	});

	it("draws the same end-marker column as legacy, in both blink phases", _ {
		Rig r;
		r.seed(Sentinel{});
		r.open();

		// Both sides raise the marker through their own front door, BEFORE the first tick:
		// `set_end_marker_visible` is a state write, not an event, so it dirties no repaint gate --
		// legacy's own `uiNeedsRendering(this, 0xFFFFFFFF, 0)` (`AudioClipView::padAction`) is the
		// half of that arm the shell owns rather than the scene. Found by writing the tick after
		// the setter and watching the port render the frame from before it.
		// `raise_legacy_end_marker` asserts legacy's flag took, by reading legacy's own
		// `getMaxLength`.
		expect(raise_legacy_end_marker(r)).to_be_true();
		r.scene().set_end_marker_visible(true);
		// Both start on the bright half: legacy's `padAction` sets `blinkOn = true`
		// (`AudioClipView::padAction`), the port's setter does the same.
		expect(r.scene().blink_on()).to_be_true();
		r.tick(0);
		// The end column at scroll 0, zoom 48 and length 768 is `(768 - 1) / 48` = 15, and on
		// the bright phase it is `colours::red` (`AudioClipView::renderMainPads` in `audio_clip_view.cpp`).
		expect_pads_match(self, r, legacy_pads(),
		                  PadExpectation::waveform(16).and_column(15, deluge::gui::colours::red));

		// One blink phase later, both should be on the dim half -- legacy through its own
		// `AudioClipView::timerCallback` (`gui/views/audio_clip_view.cpp`), the port through the deadline
		// `next_timer()` armed on the tick above, with no input at all.
		(void)legacy_view.timerCallback();
		r.tick(deluge::ui::scenes::audio_clip::kMarkerBlinkMs);
		expect(r.scene().blink_on()).to_be_false();
		// The subject of this half: on the dim phase the end column is `colours::red_dull`
		// (`AudioClipView::renderMainPads` in `audio_clip_view.cpp`), where the bright phase drew
		// `colours::red`. The end column at scroll 0, zoom 48 and length 768 is `(768 - 1) / 48` = 15.
		expect_pads_match(self, r, legacy_pads(),
		                  PadExpectation::waveform(16).and_column(15, deluge::gui::colours::red_dull));

		lower_legacy_end_marker(r);
	});

	// --- the playhead ---------------------------------------------------------------------------

	it("draws no cursor when playback is stopped, exactly as legacy does not", _ {
		Rig r;
		r.seed(Sentinel{});
		r.open();
		r.tick(0);
		// `graphicsRoutine`'s first arm (`audio_clip_view.cpp`) -> 255.
		expect(r.scene().last_tick_square()).to_equal(255);
		expect_pads_match(self, r, legacy_pads(), PadExpectation::waveform_without_cursor(16));
	});

	it("draws the play cursor in the same column as legacy, at three positions", _ {
		Rig r;
		playbackHandler.playbackState = PLAYBACK_SWITCHED_ON | PLAYBACK_CLOCK_INTERNAL_ACTIVE;
		playbackHandler.ticksLeftInCountIn = 0;
		for (const int32_t pos : {0, 240, 720}) {
			r.seed(Sentinel{.last_processed_pos = pos});
			r.open();
			r.tick(static_cast<uint32_t>(pos));
			// The expected column, derived independently of BOTH sides from the sentinel: at scroll
			// 0 and zoom 48, tick `pos` is column `pos / 48`.
			expect(r.scene().last_tick_square()).to_equal(pos / 48);
			// The cursor alone is a whole column of `kPlayheadColour`, on top of the waveform.
			expect_pads_match(self, r, legacy_pads(), PadExpectation::waveform_with_cursor(16 + 8));
		}
		playbackHandler.playbackState = 0;
	});

	it("draws no cursor while a count-in is running, exactly as legacy does not", _ {
		Rig r;
		playbackHandler.playbackState = PLAYBACK_SWITCHED_ON | PLAYBACK_CLOCK_INTERNAL_ACTIVE;
		playbackHandler.ticksLeftInCountIn = 4;
		r.seed(Sentinel{.last_processed_pos = 240});
		r.open();
		r.tick(0);
		expect(r.scene().last_tick_square()).to_equal(255);
		expect_pads_match(self, r, legacy_pads(), PadExpectation::waveform_without_cursor(16));
		playbackHandler.ticksLeftInCountIn = 0;
		playbackHandler.playbackState = 0;
	});

	it("draws no cursor when the clip is not active, exactly as legacy does not", _ {
		Rig r;
		playbackHandler.playbackState = PLAYBACK_SWITCHED_ON | PLAYBACK_CLOCK_INTERNAL_ACTIVE;
		r.seed(Sentinel{.last_processed_pos = 240});
		r.f.clip->activeIfNoSolo = false;
		r.open();
		r.tick(0);
		expect(r.scene().last_tick_square()).to_equal(255);
		expect_pads_match(self, r, legacy_pads(), PadExpectation::waveform_without_cursor(16));
		r.f.clip->activeIfNoSolo = true;
		playbackHandler.playbackState = 0;
	});

	it("draws no cursor at all with the flash cursor OFF, on either side", _ {
		Rig r;
		playbackHandler.playbackState = PLAYBACK_SWITCHED_ON | PLAYBACK_CLOCK_INTERNAL_ACTIVE;
		r.seed(Sentinel{.last_processed_pos = 240});
		r.open();
		r.tick(0);
		const int32_t with_cursor = r.scene().last_tick_square();
		expect(with_cursor).to_equal(5);
		// The cursor really is on screen before the setting is changed -- eight pads, one
		// whole column -- so the assertion after it is a change and not a tautology.
		expect(cursor_pads(r)).to_equal(static_cast<int32_t>(kDisplayHeight));

		// The one setting, which legacy reads directly and the port through the device port's
		// `pads/playCursor` row.
		PadLEDs::flashCursor = FLASH_CURSOR_OFF;
		PadLEDs::clearTickSquares(/*shouldSend=*/false);
		// The next frame, as legacy's next graphics routine: the scene is no longer frame-driven, and still
		// draws the frame that takes the cursor away.
		r.tick(deluge::ui::shell::kGraphicsRoutineMs);
		expect_pads_match(self, r, legacy_pads(), PadExpectation::waveform_without_cursor(16));
		playbackHandler.playbackState = 0;
	});

	it("flashes the cursor through the PIC with the flash cursor FAST, once per square, and paints none", _ {
		// `PadLEDs::setTickSquares`' fast arm: `flashMainPad` for each row whose square moved.
		Rig r;
		PadLEDs::flashCursor = FLASH_CURSOR_FAST;
		playbackHandler.playbackState = PLAYBACK_SWITCHED_ON | PLAYBACK_CLOCK_INTERNAL_ACTIVE;
		r.seed(Sentinel{.last_processed_pos = 240});
		const size_t from = r.fake.flashes_sent.size();
		r.open();
		r.tick(0);
		expect(r.scene().last_tick_square()).to_equal(5);
		size_t at_five = 0;
		for (size_t i = from; i < r.fake.flashes_sent.size(); i++) {
			at_five += r.fake.flashes_sent[i].x == 5 ? 1 : 0;
		}
		expect(at_five).to_equal(static_cast<size_t>(kDisplayHeight));
		expect(cursor_pads(r)).to_equal(0);
		const size_t after = r.fake.flashes_sent.size();
		r.tick(1);
		expect(r.fake.flashes_sent.size()).to_equal(after);
		playbackHandler.playbackState = 0;
	});

	// --- timers ---------------------------------------------------------------------------------

	it("is frame-driven while the cursor is live, and asks for nothing when it is not", _ {
		Rig r;
		r.seed(Sentinel{});
		r.open();
		r.tick(0);
		// Stopped: no playhead, no marker, no timer at all.
		expect(r.scene().frame_driven()).to_equal(false);
		expect(r.scene().next_timer().has_value()).to_be_false();

		// Playing: woken every frame, with no deadline of its own.
		playbackHandler.playbackState = PLAYBACK_SWITCHED_ON | PLAYBACK_CLOCK_INTERNAL_ACTIVE;
		expect(r.scene().frame_driven()).to_equal(true);
		expect(r.scene().next_timer().has_value()).to_be_false();
		playbackHandler.playbackState = 0;

		// A visible marker arms the blink instead, at the phase's own interval.
		r.scene().set_end_marker_visible(true);
		expect(r.scene().next_timer().value()).to_equal(deluge::ui::scenes::audio_clip::kMarkerBlinkMs);
	});

	// --- the scroll and zoom slides -----------------------------------------------------------------

	it("slides a scroll in a column a step, holding the pads until its last step, then settles on the new view", _ {
		Rig r;
		r.seed(Sentinel{});
		r.open();
		r.tick(0);
		const std::string before = grid_text(r);
		// Zoomed out so the clip spans two screens, then a scroll right.
		r.f.song->persistentView.xZoom[NAVIGATION_CLIP] = 24;
		r.open();
		r.tick(100);
		const std::string settled_left = grid_text(r);
		(void)audio_clip_scene().handle(
		    UiEvent{.kind = UiEvent::Kind::Encoder, .encoder = deluge::ui::widgets::core::EncoderRole::Horizontal,
		            .delta = 1},
		    UiContext{{}, {}, false, false, 101, deluge_profile()});
		r.tick(101); // the frame the input asked for: the slide's first step
		expect(audio_clip_scene().sliding()).to_equal(true);
		expect((audio_clip_scene().ui_mode() & deluge::ui::scenes::audio_clip::kUiModeHorizontalScroll) != 0).to_equal(true);
		r.tick(101 + 5 * deluge::ui::widgets::grid::kScrollStepMs);
		const std::string mid = grid_text(r);
		expect(mid).not_().to_equal(settled_left);
		// A pad struck mid-slide does nothing, as legacy's mode word refuses it.
		(void)audio_clip_scene().handle(UiEvent{.kind = UiEvent::Kind::Pad, .x = 15, .y = 0, .velocity = 64, .on = true},
		                                UiContext{{}, {}, false, false, 140, deluge_profile()});
		expect(audio_clip_scene().end_marker_visible()).to_equal(false);
		r.tick(101 + 15 * deluge::ui::widgets::grid::kScrollStepMs);
		expect(audio_clip_scene().sliding()).to_equal(false);
		expect(audio_clip_scene().ui_mode()).to_equal(deluge::ui::scenes::audio_clip::kUiModeNone);
		const std::string settled_right = grid_text(r);
		expect(settled_right).not_().to_equal(mid);
		expect(settled_right).not_().to_equal(settled_left);
		(void)before;
	});

	it("slides a zoom about the pin square for its 110 ms, holding input, then settles", _ {
		Rig r;
		r.seed(Sentinel{});
		r.open();
		r.tick(0);
		const std::string settled_before = grid_text(r);
		const deluge::ui::shell::ButtonSet x = [] {
			deluge::ui::shell::ButtonSet b{};
			b.set(ButtonId::XEnc, true);
			return b;
		}();
		(void)audio_clip_scene().handle(UiEvent{.kind = UiEvent::Kind::Button, .on = true, .button = ButtonId::XEnc},
		                                UiContext{x, {}, false, false, 1, deluge_profile()});
		(void)audio_clip_scene().handle(
		    UiEvent{.kind = UiEvent::Kind::Encoder, .encoder = deluge::ui::widgets::core::EncoderRole::Horizontal,
		            .delta = -1},
		    UiContext{x, {}, false, false, 2, deluge_profile()});
		r.tick(2); // the frame the input asked for: the slide's first step
		expect(audio_clip_scene().sliding()).to_equal(true);
		expect((audio_clip_scene().ui_mode() & 255) == deluge::ui::scenes::audio_clip::kUiModeHorizontalZoom).to_equal(true);
		r.tick(2 + 55);
		const std::string mid = grid_text(r);
		expect(mid).not_().to_equal(settled_before);
		r.tick(2 + deluge::ui::widgets::grid::kZoomSpeed + deluge::ui::widgets::grid::kZoomStepMs);
		expect(audio_clip_scene().sliding()).to_equal(false);
		expect(audio_clip_scene().ui_mode()).to_equal(deluge::ui::scenes::audio_clip::kUiModeHoldingHorizontalEncoder);
		expect(grid_text(r)).not_().to_equal(mid);
		(void)audio_clip_scene().handle(UiEvent{.kind = UiEvent::Kind::Button, .on = false, .button = ButtonId::XEnc},
		                                UiContext{{}, {}, false, false, 300, deluge_profile()});
	});

	it("keeps the cursor tracking across twelve quiet frames, with no input at all", _ {
		// What a long run of frames does with a frame-driven scene: twelve frames, one graphics routine period
		// apart, no board event of any kind, the model's play position advanced between them the way the audio
		// engine would advance it. Each frame must re-render and land on legacy's own image.
		Rig r;
		playbackHandler.playbackState = PLAYBACK_SWITCHED_ON | PLAYBACK_CLOCK_INTERNAL_ACTIVE;
		r.seed(Sentinel{});
		r.open();
		for (int32_t step = 0; step < 12; step++) {
			// 48 ticks is exactly one column at this zoom, so the expected column is the
			// step number -- derived from the sentinel, not from either side.
			r.f.clip->lastProcessedPos = step * 48;
			r.tick(static_cast<uint32_t>(step) * deluge::ui::shell::kGraphicsRoutineMs);
			expect(r.scene().last_tick_square()).to_equal(step);
			expect_pads_match(self, r, legacy_pads(), PadExpectation::waveform_with_cursor(16 + 8));
			// And the next frame stays due, rather than this being a one-shot.
			expect(r.shell.next_wake())
			    .to_equal(std::optional<uint32_t>{(static_cast<uint32_t>(step) + 1)
			                                      * deluge::ui::shell::kGraphicsRoutineMs});
		}
		playbackHandler.playbackState = 0;
	});

	it("claims the grid and NOT the OLED, because nothing time-driven reaches the OLED page", _ {
		// The mask is a claim about surfaces, and an over-claimed OLED is a permanent
		// unconditional blit (`ui/shell/scene.h`'s `timer_repaints`). Both halves asserted, and the
		// second half is checked against what the page actually draws in the OLED group below --
		// the output's name, which no timer here can change.
		expect(audio_clip_scene().timer_repaints().grid).to_be_true();
		expect(audio_clip_scene().timer_repaints().oled).to_be_false();
		expect(audio_clip_scene().timer_repaints().any()).to_be_true();
	});

	it("advances the marker blink on quiet ticks alone, and stops when the marker goes down", _ {
		Rig r;
		r.seed(Sentinel{});
		r.open();
		r.tick(0);
		r.scene().set_end_marker_visible(true);
		expect(r.scene().blink_on()).to_be_true();

		uint32_t t = 0;
		for (const bool expected : {false, true, false, true}) {
			t += deluge::ui::scenes::audio_clip::kMarkerBlinkMs;
			r.tick(t); // no input at all
			expect(r.scene().blink_on()).to_equal(expected);
		}

		// Marker down: `timerCallback` is unset in legacy (`AudioClipView::padAction`'s marker-toggle arm), so
		// the phase must freeze.
		r.scene().set_end_marker_visible(false);
		const bool frozen = r.scene().blink_on();
		t += 10 * deluge::ui::scenes::audio_clip::kMarkerBlinkMs;
		r.tick(t);
		expect(r.scene().blink_on()).to_equal(frozen);
		expect(r.scene().next_timer().has_value()).to_be_false();
	});

	// --- the OLED page --------------------------------------------------------------------------

	it("draws the same OLED page as legacy for a named output", _ {
		Rig r;
		name_track(r, "TAPE");
		name_clip(r, "");
		r.f.clip->setSection(2);
		r.seed(Sentinel{});
		r.open();
		r.tick(0);
		expect(r.oled()).to_equal(legacy_oled());
	});

	it("draws the same OLED page as legacy for a named output and a named clip", _ {
		Rig r;
		name_track(r, "TAPE");
		name_clip(r, "VERSE");
		r.f.clip->setSection(0);
		r.seed(Sentinel{});
		r.open();
		r.tick(0);
		expect(r.oled()).to_equal(legacy_oled());
		name_clip(r, "");
	});

	it("starts a name and a clip line too wide for the panel where legacy does, then scrolls them", _ {
		Rig r;
		name_track(r, "A TRACK NAME FAR TOO WIDE");
		name_clip(r, "AND A CLIP NAME FAR TOO WIDE TO FIT");
		r.f.clip->setSection(0);
		r.seed(Sentinel{});
		r.open();
		r.tick(0);
		const std::string first = r.oled();
		// The type and the name, rows 0 to 31, as legacy draws them: the name left-aligned where it scrolls
		// from. The clip line differs on purpose (Q2): legacy shows it centred and overflowing both edges until
		// its scroller's first step, then jumps to the left edge; the port starts it at the left edge.
		const auto top_rows = [](const std::string& pbm) {
			size_t at = 0;
			for (int line = 0; line < 2 + 32 && at != std::string::npos; ++line) {
				at = pbm.find('\n', at + 1);
			}
			return pbm.substr(0, at);
		};
		expect(top_rows(first)).to_equal(top_rows(legacy_oled()));
		// Legacy's side-scrollers pause 400 ms, then step a pixel each 15 ms; nothing in this process runs
		// legacy's OLED timer, so the scrolled frames are the scene's alone.
		r.tick(400 + 15 * 4);
		expect(r.oled()).not_().to_equal(first);
		name_clip(r, "");
		name_track(r, "TAPE");
	});

	it("draws the same OLED page as legacy for an unnamed output", _ {
		Rig r;
		name_track(r, "");
		r.seed(Sentinel{});
		r.open();
		r.tick(0);
		expect(r.oled()).to_equal(legacy_oled());
	});

	it("draws the same OLED page as legacy in each of the three audio output modes", _ {
		Rig r;
		name_track(r, "TAPE");
		name_clip(r, "");
		r.seed(Sentinel{});
		for (const AudioOutputMode m :
		     {AudioOutputMode::player, AudioOutputMode::sampler, AudioOutputMode::looper}) {
			deluge::edits::audioModeFor(*r.f.output, m);
			r.open();
			r.tick(0);
			expect(r.oled()).to_equal(legacy_oled());
		}
		deluge::edits::audioModeFor(*r.f.output, AudioOutputMode::player);
	});

	// --- goldens --------------------------------------------------------------------------------

	it("draws the output name and section on a 48-row panel", _ {
		Rig r;
		name_track(r, "TAPE");
		name_clip(r, "");
		r.f.clip->setSection(2);
		r.seed(Sentinel{});
		r.open();
		r.tick(0);
		expect_golden(self, "ui/scenes/audio_clip_oled_48.pbm", r.oled());
	});

	it("draws the output name and section on a 64-row panel", _ {
		Rig r{board_64(), profile_64()};
		name_track(r, "TAPE");
		name_clip(r, "");
		r.f.clip->setSection(2);
		r.seed(Sentinel{});
		r.open();
		r.tick(0);
		expect_golden(self, "ui/scenes/audio_clip_oled_64.pbm", r.oled());
	});

	// --- the UI-mode latch, which three arms below branch on -------------------------------------
	it("enters and leaves the horizontal-encoder UI mode exactly when legacy does", _ {
		spec_fixture::QuietAudio quiet;
		Rig r;
		Input in;
		r.reseed(Sentinel{});
		r.open_scene_only();
		currentUIMode = UI_MODE_NONE;

		// Press: both sides reach `TimelineView::buttonAction`'s X_ENC arm
		// (`gui/views/timeline_view.cpp`) and enter the mode.
		(void)in.press(ButtonId::XEnc, true);
		(void)legacy_view.buttonAction(legacy_button(ButtonId::XEnc), true);
		expect(r.scene().ui_mode()).to_equal(currentUIMode);
		expect(r.scene().ui_mode()).to_equal(deluge::ui::scenes::audio_clip::kUiModeHoldingHorizontalEncoder);

		// Release: both leave it.
		(void)in.press(ButtonId::XEnc, false);
		(void)legacy_view.buttonAction(legacy_button(ButtonId::XEnc), false);
		expect(r.scene().ui_mode()).to_equal(currentUIMode);
		expect(r.scene().ui_mode()).to_equal(deluge::ui::scenes::audio_clip::kUiModeNone);
		currentUIMode = UI_MODE_NONE;
	});

	it("does NOT enter that mode when shift is held, which is what frees the length-edit chord", _ {
		spec_fixture::QuietAudio quiet;
		// `AudioClipView::buttonAction` (`audio_clip_view.cpp`): with shift down and Y_ENC up, the X_ENC arm
		// matches neither branch, does nothing, and -- the part that matters -- never delegates to `ClipView`,
		// so the mode is never entered. That is exactly what leaves `AudioClipView::horizontalEncoderAction`'s
		// `isNoUIModeActive()` first arm reachable.
		Rig r;
		Input in;
		r.reseed(Sentinel{});
		r.open_scene_only();
		currentUIMode = UI_MODE_NONE;

		(void)in.press(ButtonId::Shift, true);
		legacy_hold(ButtonId::Shift, true);
		(void)in.press(ButtonId::XEnc, true);
		(void)legacy_view.buttonAction(legacy_button(ButtonId::XEnc), true);

		expect(r.scene().ui_mode()).to_equal(currentUIMode);
		expect(r.scene().ui_mode()).to_equal(deluge::ui::scenes::audio_clip::kUiModeNone);

		(void)in.press(ButtonId::XEnc, false);
		(void)in.press(ButtonId::Shift, false);
		legacy_hold(ButtonId::Shift, false);
		currentUIMode = UI_MODE_NONE;
	});

	// --- BACK while the horizontal encoder is held ------------------------------------------------------

	it("claims BACK while the horizontal encoder is held, so GlobalControls never undoes", _ {
		spec_fixture::QuietAudio quiet;
		// The routing half of the defect. `services::GlobalControls` gates `View::buttonAction`'s
		// undo/redo arm on `stack_.input_top().is_root()` (`GlobalControls::handle`),
		// which this scene answers true; the ONLY thing that keeps the undo from happening is
		// this scene claiming the event first, which is what `handled` says.
		Rig r;
		Input in;
		r.reseed(Sentinel{});
		r.open_scene_only();

		// With no mode set, BACK is not this scene's: it must fall through, which is how
		// undo/redo keeps working everywhere else.
		expect(in.press(ButtonId::Back, true, 1).handled).to_be_false();
		// Released before the next press: `InputLowering` reports EDGES, so a second press of
		// a button already down produces no event at all and the assertion below would be
		// reading the previous result. Found by that assertion failing.
		(void)in.press(ButtonId::Back, false, 2);

		// With the horizontal encoder held it IS this scene's, and the claim is what closes
		// the defect.
		(void)in.press(ButtonId::XEnc, true, 3);
		const Result claimed = in.press(ButtonId::Back, true, 4);
		expect(claimed.handled).to_be_true();
		// The release is claimed too -- legacy's `if (on)` in `AudioClipView::buttonAction`'s BACK arm guards
		// the BODY, not the arm, so a release with the mode set also returns DEALT_WITH rather than reaching
		// `View::buttonAction`.
		expect(in.press(ButtonId::Back, false, 5).handled).to_be_true();
		(void)in.press(ButtonId::XEnc, false, 6);
	});

	it("undoing a clear gives the clip its name back with its sample, and redoing it takes the name again", _ {
		spec_fixture::QuietAudio quiet;
		Rig r;
		r.reseed(Sentinel{});
		r.f.clip->setName("TAKE");
		ConsequenceAudioClipSetSample undo{r.f.clip};
		// What a clear does after recording that consequence (`AudioClip::clearSample`).
		r.f.clip->setSampleFilePath("");
		r.f.clip->setName("");
		alignas(ModelStack) char memory[MODEL_STACK_MAX_SIZE];
		ModelStack* stack = setupModelStackWithSong(memory, r.f.song);
		(void)undo.revert(BEFORE, stack);
		expect(r.f.clip->name).to_equal(std::string{"TAKE"});
		(void)undo.revert(AFTER, stack);
		expect(r.f.clip->name).to_equal(std::string{});
		r.f.clip->sampleHolder.setAudioFile(r.sample);
		r.reseed(Sentinel{});
	});

	it("clears the clip on BACK + horizontal encoder, exactly as legacy does", _ {
		// The behavioural half. `AudioClip::clear`
		// drops the sample and the clip's name, which is what "cleared" means in the model --
		// and is emphatically not what an undo would have done.
		//
		// **Every example in this group holds `QuietAudio`, and none of the render examples
		// above does.** The latch keeps the audio routine still while legacy's own gestures run
		// (`support/card_access.h`). It must NOT spread to a pad comparison: the overview scan
		// waits while the latch is held, and a failed scan is the vacuity the `Sentinel`
		// docstring is about.
		spec_fixture::QuietAudio quiet;
		Rig r;
		Input in;

		// --- legacy, from its own sentinel ---
		r.reseed(Sentinel{});
		r.f.clip->name = "TAKE";
		currentUIMode = UI_MODE_HOLDING_HORIZONTAL_ENCODER_BUTTON;
		(void)legacy_view.buttonAction(legacy_button(ButtonId::Back), true);
		currentUIMode = UI_MODE_NONE;
		const bool legacy_sample_gone = r.f.clip->sampleHolder.audioFile == nullptr;
		const bool legacy_name_gone = r.f.clip->name.empty();
		expect(legacy_sample_gone).to_be_true();
		expect(legacy_name_gone).to_be_true();

		// --- the port, from the same sentinel, re-seeded ---
		r.f.clip->sampleHolder.setAudioFile(r.sample);
		r.reseed(Sentinel{});
		r.f.clip->name = "TAKE";
		r.open_scene_only();
		expect(r.f.clip->sampleHolder.audioFile).not_().to_be_null(); // the re-seed took
		(void)in.press(ButtonId::XEnc, true);
		const Result cleared = in.press(ButtonId::Back, true);
		expect(r.f.clip->sampleHolder.audioFile == nullptr).to_equal(legacy_sample_gone);
		expect(r.f.clip->name.empty()).to_equal(legacy_name_gone);
		// And the popup legacy shows through `display->displayPopup` (`AudioClipView::buttonAction`'s BACK
		// arm), which this layer raises as an `Action::Kind::Popup`.
		expect(Input::has_popup(cleared,
		                        static_cast<uint16_t>(FlashStorage::automationClear
		                                                  ? deluge::l10n::String::STRING_FOR_SAMPLE_CLEARED
		                                                  : deluge::l10n::String::STRING_FOR_CLIP_CLEARED)))
		    .to_be_true();
		(void)in.press(ButtonId::XEnc, false);

		r.f.clip->sampleHolder.setAudioFile(r.sample);
		r.f.clip->name.clear();
	});

	it("gives BACK back to the global handler when the song button is ALSO held, as legacy does", _ {
		spec_fixture::QuietAudio quiet;
		// the BACK arm's condition in `AudioClipView::buttonAction` is
		// `currentUIMode == UI_MODE_HOLDING_HORIZONTAL_ENCODER_BUTTON`, an EXACT comparison against the whole
		// word -- so the song button's exclusive value ORed in takes the arm away. Asserted against legacy's
		// own answer, by driving legacy's `buttonAction` in the same state and watching the clip survive.
		Rig r;
		Input in;

		r.reseed(Sentinel{});
		currentUIMode = UI_MODE_HOLDING_HORIZONTAL_ENCODER_BUTTON | UI_MODE_HOLDING_SONG_BUTTON;
		(void)legacy_view.buttonAction(legacy_button(ButtonId::Back), true);
		currentUIMode = UI_MODE_NONE;
		expect(r.f.clip->sampleHolder.audioFile).not_().to_be_null(); // legacy did NOT clear

		r.reseed(Sentinel{});
		r.open_scene_only();
		(void)in.press(ButtonId::Session, true);
		(void)in.press(ButtonId::XEnc, true);
		expect(r.scene().ui_mode())
		    .to_equal(deluge::ui::scenes::audio_clip::kUiModeHoldingSongButton
		              | deluge::ui::scenes::audio_clip::kUiModeHoldingHorizontalEncoder);
		expect(in.press(ButtonId::Back, true).handled).to_be_false();
		expect(r.f.clip->sampleHolder.audioFile).not_().to_be_null();
		(void)in.press(ButtonId::XEnc, false);
		(void)in.press(ButtonId::Session, false);
	});

	// --- stop_following()'s refused turn ----------------------------------------------------------

	it("forgets the auto-extend restore point on a turn the UI-mode gate then refuses", _ {
		spec_fixture::QuietAudio quiet;
		// Legacy clears
		// `xScrollBeforeFollowingAutoExtendingLinearRecording` as the FIRST statement of
		// `ClipNavigationTimelineView::horizontalEncoderAction`
		// (`clip_navigation_timeline_view.cpp`), above `TimelineView`'s gates -- so a turn
		// those gates refuse still forgets it. With `UI_MODE_HOLDING_SONG_BUTTON` set, neither
		// `TimelineView::horizontalEncoderAction`'s arm that wants the
		// horizontal-encoder flag nor its arm that wants no mode at all matches, so nothing moves.
		Rig r;
		Input in;

		// --- legacy ---
		r.reseed(Sentinel{});
		NavProbe::set_remembered_scroll(480);
		currentUIMode = UI_MODE_HOLDING_SONG_BUTTON;
		(void)legacy_view.horizontalEncoderAction(1);
		currentUIMode = UI_MODE_NONE;
		const int32_t legacy_follow = NavProbe::remembered_scroll();
		const int32_t legacy_scroll = r.f.song->persistentView.xScroll[NAVIGATION_CLIP];
		const uint32_t legacy_zoom = r.f.song->persistentView.xZoom[NAVIGATION_CLIP];

		// --- the port ---
		r.reseed(Sentinel{});
		r.open_scene_only();
		r.scene().timeline().scroll_before_following = 480;
		(void)in.press(ButtonId::Session, true);
		(void)in.turn(deluge::ui::widgets::core::EncoderRole::Horizontal, 1);
		(void)in.press(ButtonId::Session, false);

		expect(r.scene().timeline().scroll_before_following).to_equal(legacy_follow);
		expect(r.scene().timeline().scroll_before_following)
		    .to_equal(deluge::ui::scenes::timeline::kNotFollowing);
		// ...and the turn really was REFUSED on both sides, which is what makes this example
		// about the clear rather than about the gesture.
		expect(r.scene().timeline().x_scroll).to_equal(legacy_scroll);
		expect(r.scene().timeline().x_scroll).to_equal(0);
		expect(r.scene().timeline().x_zoom).to_equal(legacy_zoom);
		expect(r.scene().timeline().x_zoom).to_equal(48u);
	});

	it("KEEPS the restore point on a shift length edit, which is one arm above the clear", _ {
		spec_fixture::QuietAudio quiet;
		// The other half of the placement, and the reason the clear is not the first statement of
		// `AudioClipView::horizontalEncoderAction`: legacy's clear lives in `ClipNavigationTimelineView`,
		// reached only through `ClipView`'s THIRD arm (`ClipView::horizontalEncoderAction` in
		// `gui/views/clip_view.cpp`). A shift length edit takes the FIRST (`clip_view.cpp`) and never reaches
		// it.
		Rig r;
		Input in;

		// --- legacy ---
		r.reseed(Sentinel{});
		NavProbe::set_remembered_scroll(480);
		currentUIMode = UI_MODE_NONE;
		legacy_hold(ButtonId::Shift, true);
		(void)legacy_view.horizontalEncoderAction(1);
		legacy_hold(ButtonId::Shift, false);
		const int32_t legacy_follow = NavProbe::remembered_scroll();
		const int32_t legacy_length = r.f.clip->loopLength();
		expect(legacy_follow).to_equal(480); // legacy really does keep it

		// --- the port ---
		r.reseed(Sentinel{});
		r.open_scene_only();
		r.scene().timeline().scroll_before_following = 480;
		(void)in.press(ButtonId::Shift, true);
		(void)in.turn(deluge::ui::widgets::core::EncoderRole::Horizontal, 1);
		(void)in.press(ButtonId::Shift, false);

		expect(r.scene().timeline().scroll_before_following).to_equal(legacy_follow);
		// ...and the edit itself happened, so this is not passing because the turn did nothing.
		expect(r.f.clip->loopLength()).to_equal(legacy_length);
		expect(r.f.clip->loopLength()).to_be_greater_than(768);
	});

	// --- the edits ---------------------------------------------------------------------------------

	it("undoes a sample-end drag as legacy does", _ {
		spec_fixture::QuietAudio quiet;
		Rig r;
		changeRootUI(&sessionView); // an undo step opens only with a view as the current UI
		Input in;
		const Sentinel s{};
		constexpr int32_t kColumn = 9;

		r.reseed(s);
		expect(reset_legacy_markers(r)).to_be_true();
		const ModelSnapshot seeded = model_snapshot(r);
		expect(raise_legacy_end_marker(r)).to_be_true();
		(void)legacy_view.padAction(kColumn, 0, 1);
		const ModelSnapshot legacy = model_snapshot(r);
		(void)actionLogger.revert(BEFORE, false, false);
		const ModelSnapshot legacy_undone = model_snapshot(r);
		lower_legacy_end_marker(r);

		r.reseed(s);
		r.open_scene_only();
		(void)in.pad(end_square_of(r), 0, true);
		(void)in.pad(kColumn, 0, true);
		const ModelSnapshot port = model_snapshot(r);
		(void)actionLogger.revert(BEFORE, false, false);
		const ModelSnapshot port_undone = model_snapshot(r);

		expect_edit_matches(self, legacy, port, seeded);
		expect(port_undone).to_equal(legacy_undone);
		expect(port_undone).to_equal(seeded);
		nullifyUIs();
	});

	it("undoes a clear as legacy does", _ {
		spec_fixture::QuietAudio quiet;
		Rig r;
		changeRootUI(&sessionView); // an undo step opens only with a view as the current UI
		Input in;

		r.reseed(Sentinel{});
		r.f.clip->name = "TAKE";
		currentUIMode = UI_MODE_HOLDING_HORIZONTAL_ENCODER_BUTTON;
		(void)legacy_view.buttonAction(legacy_button(ButtonId::Back), true);
		currentUIMode = UI_MODE_NONE;
		(void)actionLogger.revert(BEFORE, false, false);
		const bool legacy_sample_back = r.f.clip->sampleHolder.audioFile != nullptr;
		const std::string legacy_name = r.f.clip->name;

		r.f.clip->sampleHolder.setAudioFile(r.sample);
		r.reseed(Sentinel{});
		r.f.clip->name = "TAKE";
		r.open_scene_only();
		(void)in.press(ButtonId::XEnc, true);
		(void)in.press(ButtonId::Back, true);
		(void)in.press(ButtonId::XEnc, false);
		expect(r.f.clip->sampleHolder.audioFile).to_be_null();
		(void)actionLogger.revert(BEFORE, false, false);
		expect(r.f.clip->sampleHolder.audioFile != nullptr).to_equal(legacy_sample_back);
		expect(r.f.clip->name).to_equal(legacy_name);

		r.f.clip->sampleHolder.setAudioFile(r.sample);
		r.f.clip->name.clear();
		nullifyUIs();
	});

	it("a drag while the clock runs coalesces its undo as legacy's", _ {
		// Shorten by one square, then lengthen back: the lengthen undoes the shorten, and with the
		// clock running the clip is re-synced by hand.
		spec_fixture::QuietAudio quiet;
		Rig r;
		changeRootUI(&sessionView); // an undo step opens only with a view as the current UI
		Input in;
		const Sentinel s{};

		r.reseed(s);
		const ModelSnapshot seeded = model_snapshot(r);
		playbackHandler.playbackState = PLAYBACK_SWITCHED_ON | PLAYBACK_CLOCK_INTERNAL_ACTIVE;
		currentUIMode = UI_MODE_NONE;
		legacy_hold(ButtonId::Shift, true);
		(void)legacy_view.horizontalEncoderAction(-1);
		const int32_t legacy_shortened = r.f.clip->loopLength();
		(void)legacy_view.horizontalEncoderAction(1);
		legacy_hold(ButtonId::Shift, false);
		currentUIMode = UI_MODE_NONE;
		const ModelSnapshot legacy = model_snapshot(r);
		const int32_t legacy_steps = undo_steps();
		playbackHandler.playbackState = 0;

		r.reseed(s);
		playbackHandler.playbackState = PLAYBACK_SWITCHED_ON | PLAYBACK_CLOCK_INTERNAL_ACTIVE;
		r.open_scene_only();
		(void)in.press(ButtonId::Shift, true);
		(void)in.turn(deluge::ui::widgets::core::EncoderRole::Horizontal, -1);
		const int32_t port_shortened = r.f.clip->loopLength();
		(void)in.turn(deluge::ui::widgets::core::EncoderRole::Horizontal, 1);
		(void)in.press(ButtonId::Shift, false);
		const ModelSnapshot port = model_snapshot(r);
		const int32_t port_steps = undo_steps();
		playbackHandler.playbackState = 0;

		expect(port_shortened).to_equal(legacy_shortened);
		expect(port_shortened).to_be_less_than(seeded.loop_length);
		expect(port).to_equal(legacy);
		expect(port).to_equal(seeded); // the lengthen undid the shorten
		expect(port_steps).to_equal(legacy_steps);
		nullifyUIs();
	});

	it("refuses a length change without timestretching to no length or past the longest a clip may be", _ {
		spec_fixture::QuietAudio quiet;
		Rig r;
		r.reseed(Sentinel{});
		const ModelSnapshot seeded = model_snapshot(r);
		bind::ClipWrites& clip_writes = *deluge::gui::shell_host::host_ports().clip_writes;
		expect(clip_writes.resize_audio_clip(r.address(), bind::ResizeKind::NO_STRETCH, 0, 768)).to_be_false();
		expect(model_snapshot(r)).to_equal(seeded);
		expect(clip_writes.resize_audio_clip(r.address(), bind::ResizeKind::NO_STRETCH,
		                                     static_cast<int32_t>(kMaxSequenceLength) + 1, 768))
		    .to_be_false();
		expect(model_snapshot(r)).to_equal(seeded);
	});

	it("records the view a redo returns to, as legacy does", _ {
		spec_fixture::QuietAudio quiet;
		Rig r;
		changeRootUI(&sessionView); // an undo step opens only with a view as the current UI
		Input in;
		const Sentinel s{};

		r.reseed(s);
		currentUIMode = UI_MODE_NONE;
		legacy_hold(ButtonId::Shift, true);
		(void)legacy_view.horizontalEncoderAction(1);
		legacy_hold(ButtonId::Shift, false);
		currentUIMode = UI_MODE_NONE;
		const int32_t legacy_after = actionLogger.firstAction[BEFORE]->xScrollClip[AFTER];

		r.reseed(s);
		r.open_scene_only();
		(void)in.press(ButtonId::Shift, true);
		(void)in.turn(deluge::ui::widgets::core::EncoderRole::Horizontal, 1);
		(void)in.press(ButtonId::Shift, false);
		const int32_t port_after = actionLogger.firstAction[BEFORE]->xScrollClip[AFTER];

		expect(port_after).to_equal(legacy_after);
		expect(port_after).to_equal(r.scene().timeline().x_scroll);
		expect(port_after).not_().to_equal(s.x_scroll); // the lengthen really scrolled
		nullifyUIs();
	});

	it("hands each clip edit to the clip port, and the rest to the menu's sink, by the clip's address", _ {
		spec_fixture::QuietAudio quiet;
		Rig r;
		Input in;
		r.reseed(Sentinel{});
		const ModelSnapshot seeded = model_snapshot(r);
		bind::test::RecordingWrites recording;
		bind::test::RecordingClipWrites clip_writes;
		// The app's ports with the two recorders in place of its sinks, kept alive for the case.
		bind::Ports held = deluge::gui::shell_host::host_ports();
		held.writes = &recording;
		held.clip_writes = &clip_writes;
		audio_clip_scene().set_ports(&held);
		r.open_scene_only();
		using Kind = bind::test::RecordingClipWrites::Call::Kind;
		using Resize = bind::ResizeKind;
		// The last call of a kind: a length change also records the view a redo returns to, after its resize.
		const auto last = [&clip_writes, &self](Kind kind) {
			const auto found = std::ranges::find_if(clip_writes.calls.rbegin(), clip_writes.calls.rend(),
			                                        [kind](const auto& call) { return call.kind == kind; });
			const bool present = found != clip_writes.calls.rend();
			expect(present).to_be_true();
			return present ? *found : bind::test::RecordingClipWrites::Call{};
		};

		(void)in.pad(end_square_of(r), 0, true);
		(void)in.pad(9, 0, true);
		expect(clip_writes.calls.size()).to_equal(size_t{1});
		if (clip_writes.calls.empty()) {
			return;
		}
		expect(clip_writes.calls.back().kind).to_equal(Kind::RESIZE);
		expect(last(Kind::RESIZE).clip).to_equal(r.address());
		expect(last(Kind::RESIZE).resize).to_equal(Resize::END);
		expect(last(Kind::RESIZE).value).to_equal(int64_t{480});
		expect(last(Kind::RESIZE).old_length).to_equal(768);
		(void)in.pad(end_square_of(r), 0, true); // the marker back down

		(void)in.press(ButtonId::Shift, true);
		(void)in.turn(deluge::ui::widgets::core::EncoderRole::Horizontal, -1);
		expect(last(Kind::RESIZE).resize).to_equal(Resize::STRETCH);
		expect(last(Kind::RESIZE).value).to_equal(int64_t{720});
		expect(last(Kind::RESIZE).old_length).to_equal(768);
		expect(clip_writes.calls.back().kind).to_equal(Kind::UNDO_SCROLL);
		(void)in.press(ButtonId::XEnc, true);
		(void)in.turn(deluge::ui::widgets::core::EncoderRole::Horizontal, 1);
		expect(last(Kind::RESIZE).resize).to_equal(Resize::NO_STRETCH);
		expect(last(Kind::RESIZE).value).to_equal(int64_t{816});
		(void)in.press(ButtonId::XEnc, false);
		(void)in.turn(deluge::ui::widgets::core::EncoderRole::Vertical, 3);
		expect(clip_writes.calls.back().kind).to_equal(Kind::RECOLOUR);
		expect(last(Kind::RECOLOUR).clip).to_equal(r.address());
		expect(last(Kind::RECOLOUR).value).to_equal(int64_t{3});
		(void)in.press(ButtonId::Shift, false);

		(void)in.press(ButtonId::YEnc, true);
		(void)in.press(ButtonId::XEnc, true);
		expect(last(Kind::RESIZE).resize).to_equal(Resize::TO_SAMPLE_LENGTH);
		expect(clip_writes.calls.back().kind).to_equal(Kind::UNDO_SCROLL);
		(void)in.press(ButtonId::XEnc, false);
		(void)in.press(ButtonId::YEnc, false);

		(void)in.press(ButtonId::XEnc, true);
		(void)in.press(ButtonId::Back, true);
		(void)in.press(ButtonId::XEnc, false);
		expect(clip_writes.calls.back().kind).to_equal(Kind::CLEAR);
		expect(last(Kind::CLEAR).clip).to_equal(r.address());
		expect(last(Kind::CLEAR).clear_automation).to_equal(!FlashStorage::automationClear);
		expect(last(Kind::CLEAR).clear_sequence_and_mpe).to_be_true();

		(void)in.turn(deluge::ui::widgets::core::EncoderRole::Select, 1);
		expect(recording.requests.size()).to_equal(size_t{1});
		if (!recording.requests.empty()) {
			const bind::WriteRequest& mode = recording.requests.back();
			expect(mode.target.path).to_equal("mode");
			expect(mode.target.group).to_equal(deluge::project::SettingGroupId::AUDIO_INPUT);
			expect(mode.where.track.has_value()).to_be_true();
			if (mode.where.track.has_value()) {
				expect(mode.where.track->output_id).to_equal(*r.f.output->id());
			}
			expect(mode.menu_value).to_equal(1);
		}
		// Only requests: nothing else wrote.
		expect(model_snapshot(r)).to_equal(seeded);
		audio_clip_scene().set_ports(&deluge::gui::shell_host::host_ports()); // before `held` goes
	});

	it("writes the same loop length and end marker as legacy when the end marker is dragged", _ {
		spec_fixture::QuietAudio quiet;
		Rig r;
		Input in;
		const Sentinel s{};
		// Column 9 at zoom 48 means a new length of `(9 + 1) * 48 = 480` ticks -- derived from
		// the sentinel alone, and shorter than the seeded 768 so the edit is a real change.
		constexpr int32_t kColumn = 9;

		// --- legacy: raise its marker through its own gesture, then drag through it too ---
		r.reseed(s);
		expect(reset_legacy_markers(r)).to_be_true();
		const ModelSnapshot seeded = model_snapshot(r);
		expect(raise_legacy_end_marker(r)).to_be_true();
		(void)legacy_view.padAction(kColumn, 0, 1);
		const ModelSnapshot legacy = model_snapshot(r);
		lower_legacy_end_marker(r);

		// --- the port, from the same re-seeded start ---
		r.reseed(s);
		r.open_scene_only();
		expect(model_snapshot(r)).to_equal(seeded); // the re-seed really put it back
		(void)in.pad(end_square_of(r), 0, true);    // raise, through the port's own gesture
		expect(r.scene().end_marker_visible()).to_be_true();
		(void)in.pad(kColumn, 0, true);
		const ModelSnapshot port = model_snapshot(r);

		expect_edit_matches(self, legacy, port, seeded);
		expect(port.loop_length).to_equal(480);
	});

	it("writes the same start marker as legacy when the start marker is dragged", _ {
		spec_fixture::QuietAudio quiet;
		// The start-marker gesture is behind `RuntimeFeatureSettingType::TrimFromStartOfAudioClip`
		// (`AudioClipView::padAction`), which is off by default; both sides read the same global,
		// so it is turned on once, for both, and put back.
		const uint32_t saved = runtimeFeatureSettings.get(RuntimeFeatureSettingType::TrimFromStartOfAudioClip);
		runtimeFeatureSettings.set(RuntimeFeatureSettingType::TrimFromStartOfAudioClip,
		                           RuntimeFeatureStateToggle::On);

		Rig r;
		Input in;
		const Sentinel s{};
		constexpr int32_t kColumn = 4; // a new start of `4 * 48 = 192` ticks

		// --- legacy ---
		r.reseed(s);
		expect(reset_legacy_markers(r)).to_be_true();
		const ModelSnapshot seeded = model_snapshot(r);
		(void)legacy_view.padAction(0, 0, 1); // start square at scroll 0 is column 0
		(void)legacy_view.padAction(kColumn, 0, 1);
		const ModelSnapshot legacy = model_snapshot(r);

		// --- the port ---
		r.reseed(s);
		r.open_scene_only();
		(void)in.pad(0, 0, true);
		expect(r.scene().start_marker_visible()).to_be_true();
		(void)in.pad(kColumn, 0, true);
		const ModelSnapshot port = model_snapshot(r);

		expect_edit_matches(self, legacy, port, seeded);
		// `oldEndTick - newStartTicks` = `768 - 192` (`AudioClipView::changeUnderlyingSampleStart` in
		// `audio_clip_view.cpp`).
		expect(port.loop_length).to_equal(576);

		// Legacy's start latch is private and nothing else puts it down; every example that
		// drives legacy's `padAction` opens with `reset_legacy_markers`, but put it down here
		// too rather than leaving the next example to clean up after this one.
		expect(reset_legacy_markers(r)).to_be_true();

		runtimeFeatureSettings.set(RuntimeFeatureSettingType::TrimFromStartOfAudioClip, saved);
	});

	it("writes the same length and end marker as legacy for a REVERSED sample", _ {
		spec_fixture::QuietAudio quiet;
		// The other arm of `changeUnderlyingSampleLength` (`audio_clip_view.cpp`),
		// which moves `startPos` rather than `endPos` and can clamp it at zero.
		Rig r;
		Input in;
		const Sentinel s{};
		constexpr int32_t kColumn = 9;

		r.reseed(s);
		r.f.clip->sampleControls.reversed = true;
		expect(reset_legacy_markers(r)).to_be_true();
		const ModelSnapshot seeded = model_snapshot(r);
		expect(raise_legacy_end_marker(r)).to_be_true();
		(void)legacy_view.padAction(kColumn, 0, 1);
		const ModelSnapshot legacy = model_snapshot(r);
		lower_legacy_end_marker(r);

		r.reseed(s);
		r.f.clip->sampleControls.reversed = true;
		r.open_scene_only();
		(void)in.pad(end_square_of(r), 0, true);
		(void)in.pad(kColumn, 0, true);
		const ModelSnapshot port = model_snapshot(r);

		expect_edit_matches(self, legacy, port, seeded);
		// Reversed, it is the START pos that moved and the END pos that did not.
		expect(port.end_pos).to_equal(seeded.end_pos);
		expect(port.start_pos).not_().to_equal(seeded.start_pos);

		r.f.clip->sampleControls.reversed = false;
	});

	it("edits the clip length without timestretching exactly as legacy does, both directions", _ {
		spec_fixture::QuietAudio quiet;
		// `AudioClipView::editClipLengthWithoutTimestretching`,
		// a DISTINCT arm from `adjustLoopLength`: it moves the sample's marker with the clip.
		// Reached by shift + the horizontal encoder's own button, which -- per the UI-mode
		// example above -- deliberately enters no mode.
		for (const int32_t offset : {1, -1}) {
			Rig r;
			Input in;
			const Sentinel s{};

			r.reseed(s);
			const ModelSnapshot seeded = model_snapshot(r);
			currentUIMode = UI_MODE_NONE;
			legacy_hold(ButtonId::Shift, true);
			legacy_hold(ButtonId::XEnc, true);
			(void)legacy_view.horizontalEncoderAction(offset);
			legacy_hold(ButtonId::XEnc, false);
			legacy_hold(ButtonId::Shift, false);
			currentUIMode = UI_MODE_NONE;
			const ModelSnapshot legacy = model_snapshot(r);

			r.reseed(s);
			r.open_scene_only();
			(void)in.press(ButtonId::Shift, true);
			(void)in.press(ButtonId::XEnc, true);
			expect(r.scene().ui_mode()).to_equal(deluge::ui::scenes::audio_clip::kUiModeNone);
			(void)in.turn(deluge::ui::widgets::core::EncoderRole::Horizontal, static_cast<int8_t>(offset));
			(void)in.press(ButtonId::XEnc, false);
			(void)in.press(ButtonId::Shift, false);
			const ModelSnapshot port = model_snapshot(r);

			expect_edit_matches(self, legacy, port, seeded);
			// `ClipView::changeClipLength` moves by ONE SQUARE, not one screen,
			// and a square at zoom 48 is 48 ticks -- so the two directions land on 816 and
			// 720. Derived from the sentinel, not from either side.
			expect(port.loop_length).to_equal(offset > 0 ? 816 : 720);
		}
	});

	// --- the first parked MenuAction gets a real callee -------------------------------------------

	it("sets the clip length equal to the sample length exactly as legacy does", _ {
		spec_fixture::QuietAudio quiet;
		// `MenuAction::SetClipLengthToSampleLength`'s callee (`ui/scenes/menu/action.h`'s table).
		// **Driven directly**, because building half a dispatcher here would be a second seam
		// nobody owns. This example is what says the callee is real; that the dispatcher's arm
		// reaches the LEGACY method of the same name is pinned in
		// `tests/integration/ui/scenes/menu_actions_spec.cpp`'s `menu_action_three_verbs`.
		Rig r;
		const Sentinel s{};

		r.reseed(s);
		const ModelSnapshot seeded = model_snapshot(r);
		legacy_view.setClipLengthEqualToSampleLength();
		const ModelSnapshot legacy = model_snapshot(r);

		r.reseed(s);
		r.open_scene_only();
		expect(model_snapshot(r)).to_equal(seeded);
		r.scene().set_clip_length_equal_to_sample_length();
		const ModelSnapshot port = model_snapshot(r);

		expect_edit_matches(self, legacy, port, seeded);
	});

	it("reaches setClipLengthEqualToSampleLength from the X_ENC + Y_ENC chord, as legacy does", _ {
		spec_fixture::QuietAudio quiet;
		// The other caller (`AudioClipView::buttonAction` in `audio_clip_view.cpp`), and the one this library
		// can actually reach today. Same model comparison, driven through the buttons.
		Rig r;
		Input in;
		const Sentinel s{};

		r.reseed(s);
		const ModelSnapshot seeded = model_snapshot(r);
		currentUIMode = UI_MODE_NONE;
		legacy_hold(ButtonId::YEnc, true);
		(void)legacy_view.buttonAction(legacy_button(ButtonId::XEnc), true);
		legacy_hold(ButtonId::YEnc, false);
		currentUIMode = UI_MODE_NONE;
		const ModelSnapshot legacy = model_snapshot(r);

		r.reseed(s);
		r.open_scene_only();
		(void)in.press(ButtonId::YEnc, true);
		(void)in.press(ButtonId::XEnc, true);
		(void)in.press(ButtonId::XEnc, false);
		(void)in.press(ButtonId::YEnc, false);
		const ModelSnapshot port = model_snapshot(r);

		expect_edit_matches(self, legacy, port, seeded);
	});

	// --- the remaining encoder arms ---------------------------------------------------------------

	it("scrolls the audio output's mode on a select turn, exactly as legacy does", _ {
		spec_fixture::QuietAudio quiet;
		// `AudioClipView::selectEncoderAction`'s non-shift arm (`audio_clip_view.cpp`).
		Rig r;
		Input in;
		r.reseed(Sentinel{});
		currentUIMode = UI_MODE_NONE;

		const AudioOutputMode start = r.f.output->mode;
		legacy_view.selectEncoderAction(1);
		const AudioOutputMode after_legacy = r.f.output->mode;
		expect(after_legacy).not_().to_equal(start); // legacy really moved it

		deluge::edits::audioModeFor(*r.f.output, start);
		r.open_scene_only();
		(void)in.turn(deluge::ui::widgets::core::EncoderRole::Select, 1);
		expect(r.f.output->mode).to_equal(after_legacy);

		deluge::edits::audioModeFor(*r.f.output, AudioOutputMode::player);
	});

	it("shifts the clip's colour on shift + a vertical turn, exactly as legacy does", _ {
		spec_fixture::QuietAudio quiet;
		// `AudioClipView::verticalEncoderAction`.
		Rig r;
		Input in;
		r.reseed(Sentinel{});
		currentUIMode = UI_MODE_NONE;

		const int16_t start = r.f.clip->colourOffset();
		legacy_hold(ButtonId::Shift, true);
		(void)legacy_view.verticalEncoderAction(3);
		legacy_hold(ButtonId::Shift, false);
		const int16_t after_legacy = r.f.clip->colourOffset();
		expect(after_legacy).not_().to_equal(start);

		r.f.clip->setColourOffset(start);
		r.open_scene_only();
		(void)in.press(ButtonId::Shift, true);
		(void)in.turn(deluge::ui::widgets::core::EncoderRole::Vertical, 3);
		(void)in.press(ButtonId::Shift, false);
		expect(r.f.clip->colourOffset()).to_equal(after_legacy);

		r.f.clip->setColourOffset(start);
	});

	it("scrolls and zooms on a plain horizontal turn, landing where legacy lands", _ {
		spec_fixture::QuietAudio quiet;
		Rig r;
		Input in;

		// --- scroll: no mode, no shift (`TimelineView::horizontalEncoderAction` in `timeline_view.cpp`) ---
		// A 4096-tick clip, so one screen right (48 * 16 = 768 ticks) is genuinely inside
		// `getMaxLength()` and the turn is ALLOWED. At the sentinel's own 768 it would be
		// refused, which is the other example's subject rather than this one's.
		r.reseed(Sentinel{.loop_length = 4096});
		currentUIMode = UI_MODE_NONE;
		(void)legacy_view.horizontalEncoderAction(1);
		const int32_t legacy_scroll_long = r.f.song->persistentView.xScroll[NAVIGATION_CLIP];

		r.reseed(Sentinel{.loop_length = 4096});
		r.open_scene_only();
		(void)in.turn(deluge::ui::widgets::core::EncoderRole::Horizontal, 1);
		expect(r.scene().timeline().x_scroll).to_equal(legacy_scroll_long);
		expect(r.scene().timeline().x_scroll).to_equal(48 * kDisplayWidth); // one screen

		// --- zoom: the horizontal encoder's button held (`TimelineView::horizontalEncoderAction` in
		// `timeline_view.cpp`) ---
		r.reseed(Sentinel{.loop_length = 4096});
		currentUIMode = UI_MODE_HOLDING_HORIZONTAL_ENCODER_BUTTON;
		(void)legacy_view.horizontalEncoderAction(-1);
		currentUIMode = UI_MODE_NONE;
		const uint32_t legacy_zoom = r.f.song->persistentView.xZoom[NAVIGATION_CLIP];
		expect(legacy_zoom).to_equal(96u); // zoomed OUT one step

		r.reseed(Sentinel{.loop_length = 4096});
		r.open_scene_only();
		(void)in.press(ButtonId::XEnc, true);
		(void)in.turn(deluge::ui::widgets::core::EncoderRole::Horizontal, -1);
		expect(r.scene().timeline().x_zoom).to_equal(legacy_zoom);
		(void)in.press(ButtonId::XEnc, false);
	});

	// --- notifications -----------------------------------------------------------------------------

	it("scrolls back to the left when a clip-content change leaves the view past the end", _ {
		spec_fixture::QuietAudio quiet;
		// `AudioClipView::clipNeedsReRendering`'s own half, which legacy's
		// comment calls "a bit hacky that I put this in this function". The redraw half is the
		// shell's (`Shell::apply_dirty`), so this is all that is left for the scene.
		Rig r;
		r.reseed(Sentinel{.x_scroll = 768});
		r.open_scene_only();
		expect(r.scene().timeline().x_scroll).to_equal(768);

		// Legacy's own answer, from legacy's own function, over the same model.
		r.f.song->persistentView.xScroll[NAVIGATION_CLIP] = 768;
		legacy_view.clipNeedsReRendering(r.f.clip);
		const int32_t legacy_scroll = r.f.song->persistentView.xScroll[NAVIGATION_CLIP];
		expect(legacy_scroll).to_equal(0);

		deluge::base::notify::DirtySet dirty;
		dirty.raise(deluge::base::notify::Dirty::ClipContent);
		audio_clip_scene().on_model_changed(dirty);
		expect(r.scene().timeline().x_scroll).to_equal(legacy_scroll);
	});

	it("leaves the scroll alone when the view is still inside the clip", _ {
		spec_fixture::QuietAudio quiet;
		// The `else` of the same test (`AudioClipView::clipNeedsReRendering` in
		// `gui/views/audio_clip_view.cpp`), so the example above is a branch and not a function that always
		// scrolls to zero.
		Rig r;
		r.reseed(Sentinel{.x_scroll = 384, .loop_length = 4096});
		r.open_scene_only();

		r.f.song->persistentView.xScroll[NAVIGATION_CLIP] = 384;
		legacy_view.clipNeedsReRendering(r.f.clip);
		expect(r.f.song->persistentView.xScroll[NAVIGATION_CLIP]).to_equal(384);

		deluge::base::notify::DirtySet dirty;
		dirty.raise(deluge::base::notify::Dirty::ClipContent);
		audio_clip_scene().on_model_changed(dirty);
		expect(r.scene().timeline().x_scroll).to_equal(384);
	});

	it("draws the waveform, the grey region and the play cursor on the pads", _ {
		Rig r;
		playbackHandler.playbackState = PLAYBACK_SWITCHED_ON | PLAYBACK_CLOCK_INTERNAL_ACTIVE;
		// Zoomed out to 96 ticks per column: the clip fills columns 0..7, 8..15 are grey, and the
		// cursor sits in column 2 (`240 / 96`).
		r.seed(Sentinel{.x_zoom = 96, .last_processed_pos = 240});
		r.open();
		r.tick(0);
		// The one place a bare content assertion is still right: this compares against a
		// recorded golden rather than against legacy, so there is no `PadExpectation` to
		// carry -- but a golden of a black grid would have recorded nothing and passed
		// forever, which is the same hazard, so the floor is asserted here too.
		expect_grid_has_content(self, r, 64 + 8);
		expect_golden(self, "ui/scenes/audio_clip_pads.state", r.grid());
		playbackHandler.playbackState = 0;
	});

	it("offers the Waveform item on a clip whose file is loaded, and not while the clip is still recording", _ {
		// Legacy tests the clip's own file, which a linear recording has not attached yet.
		Rig r;
		const deluge::ui::scenes::menu::MenuContext ctx = deluge::ui::scenes::spec::ctx_of(
		    *r.f.song, deluge::gui::menuViewOf(r.f.clip), 0, false, false, false, false);
		expect(deluge::ui::scenes::menu::relevance::audio_clip_has_sample(ctx)).to_be_true();
		auto* recorder = new SampleRecorder(); // left to the heap: its destructor would free the take
		recorder->sample = r.sample;
		r.f.clip->recorder = recorder;
		expect(deluge::ui::scenes::menu::relevance::audio_clip_has_sample(ctx)).to_be_false();
		r.f.clip->recorder = nullptr;
	});
});
// NOLINTEND(readability-function-cognitive-complexity)
// clang-format on
DELUGE_SPEC(audio_clip_scene_spec, audio_clip_scene_spec_body)
