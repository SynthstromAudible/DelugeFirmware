/// @file
/// @brief `AudioClipScene` against legacy `AudioClipView`'s frames on the same input: the differential driver's cases
///        for the audio clip view, and the driver's own examples.
///
/// Legacy's side of every case is its frames, frozen under `tests/golden/ui/audio_clip_oracle/` from the live view
/// before it was deleted, one file per case (`support/differential/frozen.h`), and replayed by `FrozenSide`. Each case
/// plays its steps into the scene and compares the OLED, the pads and the clip's model after every step with legacy's
/// (`support/differential/differential.h`). The input alphabet holds the arms the scene has ported, so a case that
/// differs is a bug in the scene or a quirk the scene fixes on purpose, masked with its D-row.

#include "support/audio_clip_rig.h"
#include "support/differential/differential.h"
#include "support/differential/frozen.h"

#include "base/notify/notice_queue.h"
#include "hid/matrix/matrix_driver.h"
#include "processing/engines/audio_engine.h"

#include <algorithm>
#include <array>
#include <filesystem>
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

/// @brief `AudioClipScene`, fed through a real input lowering and rendered by the rig's shell, its popups captured
///        (`spec::PopupCapture`) before the shell would draw them.
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
		f.popups = diff::popups_text(popups_.events());
		popups_.clear();
		return f;
	}

private:
	[[nodiscard]] UiContext ui_context(uint32_t at_ms) const {
		return UiContext{lowering_->held(),    lowering_->held_pads(), lowering_->shift(), lowering_->sticky(), at_ms,
		                 lowering_->profile(), lowering_.get()};
	}

	/// @brief Keep every popup @p r asks for, and whether it asks for a repaint.
	void note(const Result& r) {
		popups_.take(r);
		for (const ShellAction& a : r.actions) {
			if (a.kind == ShellAction::Kind::RepaintAll || a.kind == ShellAction::Kind::Repaint) {
				repaint_ = true;
			}
		}
	}

	/// @brief Take every queued notice before the shell would draw it, so the frame's OLED is the scene's own page as
	///        legacy's is; the capture has kept each as it was raised.
	static void drain_notices() {
		deluge::base::notify::Notice n;
		while (deluge::base::notify::notices().pop(n)) {}
	}

	/// @brief Where this case's clock starts: every case's own times start again at zero.
	static inline uint32_t epoch_ms_ = 0;
	Rig& r_;
	Sentinel start_;
	std::unique_ptr<InputLowering> lowering_;
	deluge::ui::scenes::spec::PopupCapture popups_;
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

/// @brief One case and the rig state it starts from.
struct CaseRun {
	diff::Case c;
	Sentinel start{};
	void (*setup)(Rig&) = [](Rig&) {};
	void (*teardown)(Rig&) = [](Rig&) {};
};

/// The song's macros before the macros case set its own.
std::array<deluge::project::SessionMacro, 8> g_saved_macros{};

/// @brief Playing, with the flash cursor at @p Setting.
template <uint8_t Setting>
void start_playing(Rig&) {
	PadLEDs::flashCursor = Setting;
	playbackHandler.playbackState = PLAYBACK_SWITCHED_ON | PLAYBACK_CLOCK_INTERNAL_ACTIVE;
	playbackHandler.ticksLeftInCountIn = 0;
}

void stop_playing(Rig&) {
	playbackHandler.playbackState = 0;
}

/// @brief Every case the view is compared on: the scripted gestures, the seeded walks over the ported arms, the walk
///        while playing under each flash cursor setting, and the session macros.
std::vector<CaseRun> audio_clip_cases() {
	std::vector<CaseRun> runs;
	runs.push_back(
	    {.c = scripted("scroll and zoom",
	                   {turn(EncoderRole::Horizontal, +1), turn(EncoderRole::Horizontal, -1),
	                    holding(ButtonId::XEnc, turn(EncoderRole::Horizontal, -1)), turn(EncoderRole::Horizontal, +2),
	                    holding(ButtonId::XEnc, turn(EncoderRole::Horizontal, +1))})});
	runs.push_back({.c = scripted("markers and length",
	                              {pad_tap(15, 4), pad_tap(6, 4), pad_tap(6, 4),
	                               holding(ButtonId::Shift, turn(EncoderRole::Horizontal, +1)),
	                               holding(ButtonId::Shift, holding(ButtonId::XEnc, turn(EncoderRole::Horizontal, -1))),
	                               holding(ButtonId::XEnc, tap(ButtonId::YEnc))})});
	runs.push_back({.c = scripted("clear and colour", {holding(ButtonId::Shift, turn(EncoderRole::Vertical, +3)),
	                                                   holding(ButtonId::XEnc, tap(ButtonId::Back))})});
	// SESSION stays held to the end: a release a millisecond after the press is a short press, which leaves the view.
	runs.push_back({.c = scripted("session macros", {{Step::press(ButtonId::Session, true)},
	                                                 pad_tap(16, 0),
	                                                 pad_tap(16, 3),
	                                                 pad_tap(16, 2),
	                                                 holding(ButtonId::Shift, pad_tap(16, 0)),
	                                                 holding(ButtonId::AffectEntire, pad_tap(16, 0)),
	                                                 {Step::pad(16, 1, true)}}),
	                .setup =
	                    [](Rig& r) {
		                    std::array<deluge::project::SessionMacro, 8>& macros =
		                        r.f.song->project.arrangement.session_macros;
		                    g_saved_macros = macros;
		                    using Kind = deluge::project::SessionMacroKind;
		                    macros[0] = {.kind = Kind::CLIP_LAUNCH, .clip_id = r.f.clip->id()};
		                    macros[1] = {.kind = Kind::OUTPUT_CYCLE, .output_id = r.f.output->id()};
		                    macros[2] = {.kind = Kind::SECTION, .section = 0};
		                    macros[3] = {.kind = Kind::CLIP_LAUNCH, .clip_id = 0xFFFF'FFFFu}; // not in the song
	                    },
	                .teardown = [](Rig& r) { r.f.song->project.arrangement.session_macros = g_saved_macros; }});
	const std::vector<Gesture> alphabet = ported_alphabet();
	for (uint64_t seed = 1; seed <= 8; ++seed) {
		runs.push_back({.c = diff::random_walk("audio clip walk", seed, alphabet, 80)});
	}
	const Sentinel playing{.last_processed_pos = 240};
	const auto walk_playing = [&alphabet](uint8_t setting) {
		diff::Case c = diff::random_walk("playing walk", 9, alphabet, 40);
		c.name += std::format(" flash cursor {}", setting);
		return c;
	};
	runs.push_back({.c = walk_playing(FLASH_CURSOR_SLOW),
	                .start = playing,
	                .setup = start_playing<FLASH_CURSOR_SLOW>,
	                .teardown = stop_playing});
	runs.push_back({.c = walk_playing(FLASH_CURSOR_FAST),
	                .start = playing,
	                .setup = start_playing<FLASH_CURSOR_FAST>,
	                .teardown = stop_playing});
	runs.push_back({.c = walk_playing(FLASH_CURSOR_OFF),
	                .start = playing,
	                .setup = start_playing<FLASH_CURSOR_OFF>,
	                .teardown = stop_playing});
	return runs;
}

} // namespace

// clang-format off
// NOLINTBEGIN(readability-function-cognitive-complexity)
describe audio_clip_differential_spec_body("scenes/audio_clip AudioClipScene against AudioClipView's frames", $ {
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
		it("agrees with legacy's frozen frames on every case: the scripted gestures, the walks and the macros", _ {
			for (const CaseRun& run : audio_clip_cases()) {
				const std::optional<std::string> text = diff::read_file(deluge::ui::scenes::spec::audio_clip_oracle_dir() / diff::frozen_file_name(run.c.name));
				expect(text.has_value()).to_equal(true);
				const std::optional<std::vector<diff::Frame>> frames =
				    text ? diff::frozen_decode(*text) : std::nullopt;
				expect(frames.has_value()).to_equal(true);
				if (!frames) {
					continue;
				}
				expect(frames->size()).to_equal(run.c.steps.size() + 1);
				Rig r;
				run.setup(r);
				diff::FrozenSide legacy{*frames};
				SceneSide scene{r, run.start};
				expect(diff::report(run.c, diff::run(run.c, legacy, scene))).to_equal(std::string{});
				run.teardown(r);
			}
		});
	});
});
// NOLINTEND(readability-function-cognitive-complexity)
// clang-format on
DELUGE_SPEC(audio_clip_differential_spec, audio_clip_differential_spec_body)
