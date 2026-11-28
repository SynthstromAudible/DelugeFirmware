/// @file
/// @brief `AudioClipScene` against legacy's `AudioClipView` on the same input: the differential driver's cases for the
///        audio clip view, and the driver's own examples.
///
/// Each case plays its steps into legacy, puts the song back, plays them into the scene, and compares the OLED, the
/// pads and the clip's model after every step (`support/differential/differential.h`). The input alphabet holds the
/// arms the scene has ported; an arm joins it when its port lands, so a case that differs is a bug in the scene or a
/// quirk the scene fixes on purpose, masked with its D-row.

#include "support/audio_clip_rig.h"
#include "support/differential/differential.h"

#include "base/notify/notice_queue.h"
#include "hid/matrix/matrix_driver.h"
#include "processing/engines/audio_engine.h"

#include <algorithm>
#include <array>
#include <memory>
#include <utility>

#include "support/spec_main.h"

namespace diff = deluge::ui::test::differential;
using deluge::ui::widgets::core::EncoderRole;
using diff::Gesture;
using diff::Step;

namespace {

// --- the driver's own examples ----------------------------------------------------------------

/// @brief A side whose frames are given, for the driver's own examples.
class ScriptedSide final : public diff::Side {
public:
	explicit ScriptedSide(std::vector<diff::Frame> frames) : frames_(std::move(frames)) {}
	void enter() override { at_ = 0; }
	void feed(const Step& /*step*/, uint32_t /*at_ms*/) override { ++at_; }
	[[nodiscard]] diff::Frame frame(uint32_t /*at_ms*/) override { return frames_.at(at_); }

private:
	std::vector<diff::Frame> frames_;
	size_t at_ = 0;
};

/// @brief A frame whose only lit pad is (@p x, @p y), in @p c.
diff::Frame frame_with_pad(size_t x, size_t y, diff::Frame::Rgb c) {
	diff::Frame f;
	f.pads[y][x] = c;
	return f;
}

constexpr diff::Frame::Rgb kRed{255, 0, 0};
constexpr diff::Frame::Rgb kBlue{0, 0, 255};

/// @brief @p n button taps, enough steps for the scripted sides.
diff::Case taps(size_t n) {
	diff::Case c{.name = "taps"};
	for (size_t i = 0; i < n; ++i) {
		c.steps.push_back(Step::press(ButtonId::Shift, (i % 2) == 0));
	}
	return c;
}

/// @brief A case's steps as text, one a line.
std::string steps_text(const diff::Case& c) {
	std::string out;
	for (const Step& s : c.steps) {
		out += diff::describe(s) + '\n';
	}
	return out;
}

// --- the audio clip's two sides ---------------------------------------------------------------

/// @brief How long passes between two cases on each side's clock: longer than any pause a case can leave armed.
constexpr uint32_t kGapBetweenCasesMs = 10'000;

/// @brief The clip's model, as both sides leave it: length, sample bounds, scroll, zoom and colour.
std::string model_digest(const Rig& r) {
	return std::format("loop_length {} start {} end {} x_scroll {} x_zoom {} colour {}", r.f.clip->loopLength(),
	                   r.f.clip->sampleHolder.startPos, r.f.clip->sampleHolder.endPos,
	                   r.f.song->persistentView.xScroll[NAVIGATION_CLIP],
	                   r.f.song->persistentView.xZoom[NAVIGATION_CLIP], r.f.clip->colourOffset());
}

/// @brief Put the rig's sample and colour back on its clip: clearing the clip takes the sample off and SHIFT with the
///        vertical encoder moves the colour, and `Rig::reseed` puts back the clip's numbers only.
void restore_sample(Rig& r) {
	if (r.f.clip->sampleHolder.audioFile != r.sample) {
		r.f.clip->sampleHolder.audioFile = r.sample;
		r.sample->addReason();
	}
	r.f.clip->setColourOffset(0);
}

/// @brief An l10n string id's English text, as both sides' popups name their strings.
std::string l10n_text(uint16_t id) {
	return deluge::l10n::get(static_cast<deluge::l10n::String>(id));
}

/// @brief Legacy's display, keeping the text of every popup raised and noting every cancel, so a frame can show
///        what legacy put over its OLED.
struct RecordingDisplay final : deluge::hid::Display {
	std::string popups;
	constexpr size_t getNumBrowserAndMenuLines() override { return 3; }
	void displayPopup(char const* text, int8_t, bool, uint8_t, int32_t, PopupType) override { note(text); }
	void popupText(char const* text, PopupType) override { note(text); }
	void popupTextTemporary(char const* text, PopupType) override { note(text); }
	void cancelPopup() override { popups += "cancel\n"; }
	void freezeWithError(char const*) override {}
	void displayError(Error) override {}
	void removeWorkingAnimation() override {}
	void displayLoadingAnimationText(char const*, bool, bool) override {}
	void removeLoadingAnimation() override {}
	bool hasPopup() override { return false; }
	bool hasPopupOfType(PopupType) override { return false; }
	void consoleText(char const*) override {}
	void timerRoutine() override {}

private:
	void note(char const* text) {
		popups += "popup ";
		popups += text != nullptr ? text : "";
		popups += '\n';
	}
};

/// @brief Legacy's `AudioClipView`, driven through its own hooks with legacy's held-button state set first.
class LegacySide final : public diff::Side {
public:
	/// @param r The rig. @param start The clip's numbers and the play position every case starts from.
	explicit LegacySide(Rig& r, Sentinel start = {}) : r_(r), start_(start), saved_display_(display) {
		display = &recorder_;
	}
	~LegacySide() override { display = saved_display_; }
	LegacySide(const LegacySide&) = delete;
	LegacySide& operator=(const LegacySide&) = delete;

	void enter() override {
		release_all();
		restore_sample(r_);
		r_.reseed(start_);
		(void)reset_legacy_markers(r_);
		r_.reseed(start_);
		currentUIMode = UI_MODE_NONE;
		recorder_.popups.clear();
		for (auto& column : matrixDriver.padStates) {
			std::ranges::fill(column, false);
		}
		// Seconds pass between visits on the device, so a zoom pause armed in an earlier case
		// (`delayHorizontalZoomUntil`, on the one legacy view) has expired.
		base_samples_ = AudioEngine::audioSampleTimer + kGapBetweenCasesMs * kSamplesPerMs;
		AudioEngine::audioSampleTimer = base_samples_;
	}

	void feed(const Step& s, uint32_t at_ms) override {
		AudioEngine::audioSampleTimer = base_samples_ + at_ms * kSamplesPerMs;
		switch (s.kind) {
		case Step::Kind::Button: {
			legacy_hold(s.button, s.down);
			(void)legacy_view.buttonAction(legacy_button(s.button), s.down);
			break;
		}
		case Step::Kind::Pad:
			// What `isPadPressed` reads, which the macros column brightens a held pad from.
			matrixDriver.padStates[s.x][s.y] = s.down;
			(void)legacy_view.padAction(s.x, s.y, s.down ? 64 : 0);
			break;
		case Step::Kind::Encoder:
			if (s.encoder == EncoderRole::Horizontal) {
				(void)legacy_view.horizontalEncoderAction(s.detents);
			}
			else if (s.encoder == EncoderRole::Vertical) {
				(void)legacy_view.verticalEncoderAction(s.detents);
			}
			else if (s.encoder == EncoderRole::Select) {
				legacy_view.selectEncoderAction(s.detents);
			}
			break;
		}
		// A scroll or zoom legacy animates ends a few frames later on the device, before the next deliberate input;
		// nothing runs those frames here, so the animation's end runs now. The scene shows both at once.
		if (isUIModeActive(UI_MODE_HORIZONTAL_SCROLL)) {
			legacy_view.scrollFinished();
		}
		if (isUIModeActive(UI_MODE_HORIZONTAL_ZOOM)) {
			exitUIMode(UI_MODE_HORIZONTAL_ZOOM);
		}
	}

	[[nodiscard]] diff::Frame frame(uint32_t /*at_ms*/) override {
		diff::Frame f;
		f.oled = legacy_oled();
		const LegacyPads pads = legacy_pads();
		for (size_t y = 0; y < diff::Mask::kRows; ++y) {
			for (size_t x = 0; x < diff::Mask::kColumns; ++x) {
				const RGB& c = pads.image[y][x];
				f.pads[y][x] = {c.r, c.g, c.b};
			}
		}
		f.model = model_digest(r_);
		f.popups = std::exchange(recorder_.popups, {});
		return f;
	}

	/// @brief Let every button go, so no hold leaks into the next case.
	static void release_all() {
		for (uint8_t b = 1; b < static_cast<uint8_t>(ButtonId::Count); ++b) {
			legacy_hold(static_cast<ButtonId>(b), false);
		}
	}

private:
	static constexpr uint32_t kSamplesPerMs = 44;
	Rig& r_;
	Sentinel start_;
	RecordingDisplay recorder_;
	uint32_t base_samples_ = 0;
	deluge::hid::Display* saved_display_;
};

/// @brief `AudioClipScene`, fed through a real input lowering and rendered by the rig's shell, with its popups taken
///        as text before the shell would draw them.
class SceneSide final : public diff::Side {
public:
	/// @param r The rig. @param start The clip's numbers and the play position every case starts from.
	explicit SceneSide(Rig& r, Sentinel start = {}) : r_(r), start_(start) {}

	void enter() override {
		while (r_.shell.stack().depth() > 0) {
			r_.shell.stack().pop(SceneKind::Null);
		}
		lowering_ = std::make_unique<InputLowering>(deluge_profile(), deluge::ui::shell::LoweringConfig{});
		r_.shell.lowering().forget_held_pads();
		restore_sample(r_);
		r_.reseed(start_);
		currentUIMode = UI_MODE_NONE;
		// Seconds pass between visits, as on legacy's side: the scene is one object, and its zoom pause from an earlier
		// case has expired.
		epoch_ms_ += kGapBetweenCasesMs;
		r_.open();
		// The waveform's first reads may be pending: tick until the first frame's time, as the scene's own retry does.
		for (uint32_t t = 0; t < diff::kFirstFrameMs; t += 10) {
			drain_notices();
			r_.tick(epoch_ms_ + t);
		}
		popups_.clear();
	}

	void feed(const Step& s, uint32_t case_ms) override {
		const uint32_t at_ms = epoch_ms_ + case_ms;
		lowering_->begin_tick(at_ms);
		switch (s.kind) {
		case Step::Kind::Button: {
			const auto [x, y] = matrix_xy(s.button);
			lowering_->on_board_event(
			    DelugeInputEvent{DELUGE_EVENT_BUTTON, x, y, static_cast<int16_t>(s.down ? 255 : 0)}, at_ms);
			UiEvent e{UiEvent::Kind::AllReleased};
			while (lowering_->next(e)) {
				note(audio_clip_scene().handle(e, ui_context(at_ms)));
			}
			break;
		}
		case Step::Kind::Pad:
			// The shell's own lowering holds the pad too, as the frame reads it (the macros column brightens a held
			// pad); its queue is dropped, so the shell does not route the pad a second time.
			r_.shell.lowering().on_host_pad(s.x, s.y, s.down ? 64 : 0, false, at_ms);
			r_.shell.lowering().discard_pending();
			note(audio_clip_scene().handle(UiEvent{.kind = UiEvent::Kind::Pad,
			                                       .x = s.x,
			                                       .y = s.y,
			                                       .velocity = static_cast<uint8_t>(s.down ? 64 : 0),
			                                       .on = s.down},
			                               ui_context(at_ms)));
			break;
		case Step::Kind::Encoder:
			note(audio_clip_scene().handle(
			    UiEvent{.kind = UiEvent::Kind::Encoder, .encoder = s.encoder, .delta = s.detents}, ui_context(at_ms)));
			break;
		}
		lowering_->end_tick(at_ms);
		// The slide a step starts ends before the next deliberate input, as legacy's side finishes its own: the
		// cases compare settled views.
		audio_clip_scene().finish_slides();
	}

	[[nodiscard]] diff::Frame frame(uint32_t case_ms) override {
		const uint32_t at_ms = epoch_ms_ + case_ms;
		drain_notices();
		// The repaint the scene asked for, as the shell's router would apply it; a scene that forgets to ask keeps
		// its last frame, as the device would.
		if (std::exchange(repaint_, false)) {
			r_.shell.boot();
		}
		r_.tick(at_ms);
		diff::Frame f;
		f.oled = r_.oled();
		const deluge::ui::gfx::grid::PadGrid& g = r_.fake.grid().grid();
		for (size_t y = 0; y < diff::Mask::kRows; ++y) {
			for (size_t x = 0; x < diff::Mask::kColumns; ++x) {
				const auto c = g.at(static_cast<uint8_t>(x), static_cast<uint8_t>(y));
				f.pads[y][x] = {c.r, c.g, c.b};
			}
		}
		f.model = model_digest(r_);
		f.popups = std::exchange(popups_, {});
		return f;
	}

private:
	[[nodiscard]] UiContext ui_context(uint32_t at_ms) const {
		return UiContext{lowering_->held(),    lowering_->held_pads(), lowering_->shift(), lowering_->sticky(), at_ms,
		                 lowering_->profile(), lowering_.get()};
	}

	/// @brief Keep the text of every popup @p r asks for, and whether it asks for a repaint.
	void note(const Result& r) {
		for (const ShellAction& a : r.actions) {
			if (a.kind == ShellAction::Kind::Popup) {
				popups_ += "popup " + l10n_text(a.popup_string) + '\n';
			}
			else if (a.kind == ShellAction::Kind::RepaintAll || a.kind == ShellAction::Kind::Repaint) {
				repaint_ = true;
			}
		}
	}

	/// @brief Take every queued notice the shell would draw as a popup, keeping its text, so the frame's OLED is the
	///        scene's own page as legacy's is.
	void drain_notices() {
		deluge::base::notify::Notice n;
		while (deluge::base::notify::notices().pop(n)) {
			if (n.kind == deluge::base::notify::Notice::Kind::Text) {
				popups_ += "popup " + (n.string_id != 0 ? l10n_text(n.string_id) : std::string{n.text}) + '\n';
			}
			else if (n.kind == deluge::base::notify::Notice::Kind::CancelPopup) {
				popups_ += "cancel\n";
			}
		}
	}

	/// @brief Where this case's clock starts: every case's own times start again at zero.
	static inline uint32_t epoch_ms_ = 0;
	Rig& r_;
	Sentinel start_;
	std::unique_ptr<InputLowering> lowering_;
	std::string popups_;
	bool repaint_ = false;
};

// --- the audio clip's input alphabet ----------------------------------------------------------

/// @brief A tap of @p b.
Gesture tap(ButtonId b) {
	return {Step::press(b, true), Step::press(b, false)};
}

/// @brief @p held down, then @p inner, then @p held up.
Gesture holding(ButtonId held, const Gesture& inner) {
	Gesture g{Step::press(held, true)};
	g.insert(g.end(), inner.begin(), inner.end());
	g.push_back(Step::press(held, false));
	return g;
}

/// @brief A pad tap at (@p x, @p y).
Gesture pad_tap(uint8_t x, uint8_t y) {
	return {Step::pad(x, y, true), Step::pad(x, y, false)};
}

/// @brief One turn of @p role by @p delta.
Gesture turn(EncoderRole role, int8_t delta) {
	return {Step::turn(role, delta)};
}

/// @brief The gestures whose arms `AudioClipScene` has ported. The ledger lists the arms still outside it.
std::vector<Gesture> ported_alphabet() {
	return {
	    turn(EncoderRole::Horizontal, +1),
	    turn(EncoderRole::Horizontal, -1),
	    holding(ButtonId::XEnc, turn(EncoderRole::Horizontal, +1)),
	    holding(ButtonId::XEnc, turn(EncoderRole::Horizontal, -1)),
	    holding(ButtonId::Shift, turn(EncoderRole::Horizontal, +1)),
	    holding(ButtonId::Shift, turn(EncoderRole::Horizontal, -1)),
	    holding(ButtonId::Shift, holding(ButtonId::XEnc, turn(EncoderRole::Horizontal, +1))),
	    holding(ButtonId::Shift, holding(ButtonId::XEnc, turn(EncoderRole::Horizontal, -1))),
	    holding(ButtonId::XEnc, tap(ButtonId::YEnc)),
	    holding(ButtonId::XEnc, tap(ButtonId::Back)),
	    holding(ButtonId::Shift, turn(EncoderRole::Vertical, +2)),
	    pad_tap(0, 4),
	    pad_tap(6, 4),
	    pad_tap(12, 4),
	    pad_tap(15, 4),
	};
}

/// @brief A case from one gesture list.
diff::Case scripted(std::string name, std::initializer_list<Gesture> gestures) {
	diff::Case c{.name = std::move(name)};
	for (const Gesture& g : gestures) {
		c.steps.insert(c.steps.end(), g.begin(), g.end());
	}
	return c;
}

} // namespace

// clang-format off
// NOLINTBEGIN(readability-function-cognitive-complexity)
describe audio_clip_differential_spec_body(
    "scenes/audio_clip AudioClipScene against AudioClipView", $ {
	    context("the differential driver", _ {
		    it("reports nothing when every frame agrees", _ {
			    ScriptedSide a{{frame_with_pad(0, 0, kRed), frame_with_pad(0, 0, kRed), frame_with_pad(0, 0, kRed)}};
			    ScriptedSide b{{frame_with_pad(0, 0, kRed), frame_with_pad(0, 0, kRed), frame_with_pad(0, 0, kRed)}};
			    const diff::Case c = taps(2);
			    expect(diff::report(c, diff::run(c, a, b))).to_equal(std::string{});
		    });

		    it("reports the first step whose frames differ, and which field", _ {
			    ScriptedSide a{{frame_with_pad(0, 0, kRed), frame_with_pad(0, 0, kRed), frame_with_pad(0, 0, kRed)}};
			    ScriptedSide b{{frame_with_pad(0, 0, kRed), frame_with_pad(0, 0, kRed), frame_with_pad(0, 0, kBlue)}};
			    const auto d = diff::run(taps(2), a, b);
			    expect(d.value_or(diff::Divergence{.step = 99}).step).to_equal(size_t{2});
			    expect(d.value_or(diff::Divergence{}).field).to_equal(std::string{"pads"});
		    });

		    it("does not report a pad a D-row masks", _ {
			    ScriptedSide a{{frame_with_pad(3, 5, kRed), frame_with_pad(3, 5, kRed)}};
			    ScriptedSide b{{frame_with_pad(3, 5, kRed), frame_with_pad(3, 5, kBlue)}};
			    diff::Case c = taps(1);
			    c.mask.mask_pad(3, 5, 201);
			    expect(diff::report(c, diff::run(c, a, b))).to_equal(std::string{});
		    });

		    it("walks the same steps for the same seed, and other steps for another", _ {
			    const std::vector<Gesture> alphabet = ported_alphabet();
			    const diff::Case first = diff::random_walk("w", 7, alphabet, 50);
			    const diff::Case again = diff::random_walk("w", 7, alphabet, 50);
			    const diff::Case other = diff::random_walk("w", 8, alphabet, 50);
			    expect(steps_text(again)).to_equal(steps_text(first));
			    expect(steps_text(other)).not_().to_equal(steps_text(first));
		    });
	    });

	    context("the audio clip view", _ {
		    it("agrees with legacy on scrolling and zooming", _ {
			    Rig r;
			    LegacySide legacy{r};
			    SceneSide scene{r};
			    const diff::Case c = scripted(
			        "scroll and zoom", {turn(EncoderRole::Horizontal, +1), turn(EncoderRole::Horizontal, -1),
			                            holding(ButtonId::XEnc, turn(EncoderRole::Horizontal, -1)),
			                            turn(EncoderRole::Horizontal, +2),
			                            holding(ButtonId::XEnc, turn(EncoderRole::Horizontal, +1))});
			    expect(diff::report(c, diff::run(c, legacy, scene))).to_equal(std::string{});
		    });

		    it("agrees with legacy on the end marker and the length edits", _ {
			    Rig r;
			    LegacySide legacy{r};
			    SceneSide scene{r};
			    const diff::Case c = scripted(
			        "markers and length",
			        {pad_tap(15, 4), pad_tap(6, 4), pad_tap(6, 4),
			         holding(ButtonId::Shift, turn(EncoderRole::Horizontal, +1)),
			         holding(ButtonId::Shift, holding(ButtonId::XEnc, turn(EncoderRole::Horizontal, -1))),
			         holding(ButtonId::XEnc, tap(ButtonId::YEnc))});
			    expect(diff::report(c, diff::run(c, legacy, scene))).to_equal(std::string{});
		    });

		    it("agrees with legacy on BACK with X held, and the colour", _ {
			    Rig r;
			    LegacySide legacy{r};
			    SceneSide scene{r};
			    const diff::Case c =
			        scripted("clear and colour", {holding(ButtonId::Shift, turn(EncoderRole::Vertical, +3)),
			                                      holding(ButtonId::XEnc, tap(ButtonId::Back))});
			    expect(diff::report(c, diff::run(c, legacy, scene))).to_equal(std::string{});
		    });

		    it("agrees with legacy on the session macros while SESSION is held, each kind and its modifiers", _ {
			    Rig r;
			    std::array<deluge::project::SessionMacro, 8>& macros = r.f.song->project.arrangement.session_macros;
			    const std::array<deluge::project::SessionMacro, 8> saved = macros;
			    using Kind = deluge::project::SessionMacroKind;
			    macros[0] = {.kind = Kind::CLIP_LAUNCH, .clip_id = r.f.clip->id()};
			    macros[1] = {.kind = Kind::OUTPUT_CYCLE, .output_id = r.f.output->id()};
			    macros[2] = {.kind = Kind::SECTION, .section = 0};
			    macros[3] = {.kind = Kind::CLIP_LAUNCH, .clip_id = 0xFFFF'FFFFu}; // a clip the song does not hold
			    LegacySide legacy{r};
			    SceneSide scene{r};
			    // SESSION stays held to the end: a release a millisecond after the press is a short press, which leaves
			    // the view.
			    const diff::Case c = scripted(
			        "session macros",
			        {{Step::press(ButtonId::Session, true)}, pad_tap(16, 0), pad_tap(16, 3), pad_tap(16, 2),
			         holding(ButtonId::Shift, pad_tap(16, 0)), holding(ButtonId::AffectEntire, pad_tap(16, 0)),
			         {Step::pad(16, 1, true)}});
			    expect(diff::report(c, diff::run(c, legacy, scene))).to_equal(std::string{});
			    macros = saved;
		    });

		    it("agrees with legacy on seeded walks over every ported arm", _ {
			    const std::vector<Gesture> alphabet = ported_alphabet();
			    for (uint64_t seed = 1; seed <= 8; ++seed) {
				    Rig r;
				    LegacySide legacy{r};
				    SceneSide scene{r};
				    const diff::Case c = diff::random_walk("audio clip walk", seed, alphabet, 80);
				    expect(diff::report(c, diff::run(c, legacy, scene))).to_equal(std::string{});
			    }
		    });

		    it("agrees with legacy while playing, under each flash cursor setting", _ {
			    const std::vector<Gesture> alphabet = ported_alphabet();
			    for (const uint8_t setting : {FLASH_CURSOR_SLOW, FLASH_CURSOR_FAST, FLASH_CURSOR_OFF}) {
				    Rig r;
				    PadLEDs::flashCursor = setting;
				    playbackHandler.playbackState = PLAYBACK_SWITCHED_ON | PLAYBACK_CLOCK_INTERNAL_ACTIVE;
				    playbackHandler.ticksLeftInCountIn = 0;
				    const Sentinel playing{.last_processed_pos = 240};
				    LegacySide legacy{r, playing};
				    SceneSide scene{r, playing};
				    diff::Case c = diff::random_walk("playing walk", 9, alphabet, 40);
				    c.name += std::format(" flash cursor {}", setting);
				    expect(diff::report(c, diff::run(c, legacy, scene))).to_equal(std::string{});
				    playbackHandler.playbackState = 0;
			    }
		    });
	    });
    });
// NOLINTEND(readability-function-cognitive-complexity)
// clang-format on
DELUGE_SPEC(audio_clip_differential_spec, audio_clip_differential_spec_body)
