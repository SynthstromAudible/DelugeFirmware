#pragma once

/// @file
/// @brief The clips the clip view's SHIFT shortcut overview is checked on: one per kind of clip and kit row, a kit's
///        own effects with the kit-global latch set and clear, and a kit row under a stale set latch, each drawn under
///        its own clip view.

#include "gui/ui/ui.h"
#include "gui/views/audio_clip_root.h"
#include "gui/views/clip_view.h"
#include "gui/views/instrument_clip_view.h"
#include "model/clip/instrument_clip.h"
#include "model/drum/gate_drum.h"
#include "model/drum/midi_drum.h"
#include "processing/sound/sound_drum.h"
#include "ui/gfx/grid/rgb.h"
#include "util/menu_entry.h"

#include "support/song_fixture.h"

#include <cstdint>
#include <string>
#include <string_view>

namespace deluge::ui::scenes::spec::clip_overview {

/// @brief One kind of clip or kit row.
enum class Case : uint8_t {
	Synth,
	KitAffectEntire,
	KitAffectEntireUnlatched,
	KitAffectEntireNoRowUnlatched,
	KitAffectEntireGateDrumUnlatched,
	KitAffectEntireGateDrum,
	KitAffectEntireMidiDrumUnlatched,
	KitAffectEntireMidiDrum,
	KitSoundDrum,
	KitSoundDrumLatched,
	KitMidiDrum,
	KitGateDrum,
	KitNothing,
	Midi,
	Cv,
	Audio,
};

/// @brief A case and the name the reference table keys it by (`overview/<name>`).
struct CaseInfo {
	Case id;
	std::string_view name;
};

inline constexpr CaseInfo kCases[] = {
    {Case::Synth, "synth"},
    {Case::KitAffectEntire, "kit_affect_entire"},
    {Case::KitAffectEntireUnlatched, "kit_affect_entire_unlatched"},
    {Case::KitAffectEntireNoRowUnlatched, "kit_affect_entire_no_row_unlatched"},
    {Case::KitAffectEntireGateDrumUnlatched, "kit_affect_entire_gate_drum_unlatched"},
    {Case::KitAffectEntireGateDrum, "kit_affect_entire_gate_drum"},
    {Case::KitAffectEntireMidiDrumUnlatched, "kit_affect_entire_midi_drum_unlatched"},
    {Case::KitAffectEntireMidiDrum, "kit_affect_entire_midi_drum"},
    {Case::KitSoundDrum, "kit_sound_drum"},
    {Case::KitSoundDrumLatched, "kit_sound_drum_latched"},
    {Case::KitMidiDrum, "kit_midi_drum"},
    {Case::KitGateDrum, "kit_gate_drum"},
    {Case::KitNothing, "kit_nothing"},
    {Case::Midi, "midi"},
    {Case::Cv, "cv"},
    {Case::Audio, "audio"},
};

/// @brief The table's name for @p c.
inline std::string_view name_of(Case c) {
	for (const CaseInfo& info : kCases) {
		if (info.id == c) {
			return info.name;
		}
	}
	return "unknown";
}

/// @brief The root UI and the kit-global latch, put back however the caller ends.
struct RootGuard {
	RootUI* saved = getRootUI();
	bool savedLatch = deluge::gui::ui::menuKitGlobalFx();
	RootGuard() = default;
	RootGuard(const RootGuard&) = delete;
	RootGuard& operator=(const RootGuard&) = delete;
	~RootGuard() {
		deluge::gui::ui::setMenuKitGlobalFx(savedLatch);
		if (saved != nullptr) {
			setRootUILowLevel(saved);
		}
	}
};

/// @brief Build @p c's song (its clip is the current clip), set the kit-global latch, and make its clip view the
///        root UI.
///
/// The latch is set for the kit affect-entire cases without `Unlatched`, as a pad press on that clip leaves it; the
/// `Unlatched` cases are the same clip with it clear, as a menu close leaves it. `KitSoundDrumLatched` is a sound
/// row with affect-entire off under a latch a press set while it was on, which nothing has cleared since.
/// @return The clip view the overview is drawn under.
inline RootUI& set_up(Case c) {
	deluge::gui::ui::setMenuKitGlobalFx(c == Case::KitAffectEntire || c == Case::KitAffectEntireGateDrum
	                                    || c == Case::KitAffectEntireMidiDrum || c == Case::KitSoundDrumLatched);
	switch (c) {
	case Case::Synth:
		(void)make_synth_song();
		break;
	case Case::KitAffectEntire:
	case Case::KitAffectEntireUnlatched: {
		SongFixture f = make_kit_song();
		(void)add_drum<SoundDrum>(f);
		f.clip->affectEntire = true;
		break;
	}
	case Case::KitAffectEntireNoRowUnlatched: {
		SongFixture f = make_kit_song();
		f.clip->affectEntire = true;
		break;
	}
	case Case::KitAffectEntireGateDrumUnlatched:
	case Case::KitAffectEntireGateDrum: {
		SongFixture f = make_kit_song();
		(void)add_drum<GateDrum>(f);
		f.clip->affectEntire = true;
		break;
	}
	case Case::KitAffectEntireMidiDrumUnlatched:
	case Case::KitAffectEntireMidiDrum: {
		SongFixture f = make_kit_song();
		(void)add_drum<MIDIDrum>(f);
		f.clip->affectEntire = true;
		break;
	}
	case Case::KitSoundDrum:
	case Case::KitSoundDrumLatched: {
		SongFixture f = make_kit_song();
		(void)add_drum<SoundDrum>(f);
		break;
	}
	case Case::KitMidiDrum: {
		SongFixture f = make_kit_song();
		(void)add_drum<MIDIDrum>(f);
		break;
	}
	case Case::KitGateDrum: {
		SongFixture f = make_kit_song();
		(void)add_drum<GateDrum>(f);
		break;
	}
	case Case::KitNothing:
		(void)make_kit_song();
		break;
	case Case::Midi:
		(void)make_midi_song();
		break;
	case Case::Cv:
		(void)make_cv_song();
		break;
	case Case::Audio:
		(void)make_audio_clip_song();
		setRootUILowLevel(&audioClipView);
		return audioClipView;
	}
	setRootUILowLevel(&instrumentClipView);
	return instrumentClipView;
}

/// @brief A pad colour as the reference tables spell it: `r,g,b`.
inline std::string rgb_text(const RGB& c) {
	return std::to_string(c.r) + "," + std::to_string(c.g) + "," + std::to_string(c.b);
}

} // namespace deluge::ui::scenes::spec::clip_overview
