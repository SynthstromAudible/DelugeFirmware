#pragma once
#include "gui/menu_item/audio_clip/audio_source_selector.h"
#include "gui/menu_item/audio_clip/sample_marker_editor.h"
#include "gui/menu_item/edit_name.h"
#include "gui/menu_item/firmware/version.h"
#include "gui/menu_item/note/fill.h"
#include "gui/menu_item/note/iterance_divisor.h"
#include "gui/menu_item/note/iterance_preset.h"
#include "gui/menu_item/note/iterance_step_toggle.h"
#include "gui/menu_item/note/probability.h"
#include "gui/menu_item/note_row/fill.h"
#include "gui/menu_item/note_row/iterance_divisor.h"
#include "gui/menu_item/note_row/iterance_preset.h"
#include "gui/menu_item/note_row/iterance_step_toggle.h"
#include "gui/menu_item/note_row/probability.h"
#include "gui/menu_item/osc/source/wave_index.h"
#include "gui/menu_item/osc/sync.h"
#include "gui/menu_item/patch_cables.h"
#include "gui/menu_item/patched_param/integer_non_fm.h"
#include "gui/menu_item/randomizer/midi_cv/note_probability.h"
#include "gui/menu_item/randomizer/midi_cv/spread_velocity.h"
#include "gui/menu_item/randomizer/randomizer_lock.h"
#include "gui/menu_item/sample/end.h"
#include "gui/menu_item/sample/start.h"
#include "gui/menu_item/sequence/direction.h"
#include "gui/menu_item/source/patched_param/modulator_level.h"
#include "gui/menu_item/stem_export/start.h"
#include "gui/menu_item/submenu.h"
#include <array>

namespace deluge::gui::menu_item {
class HorizontalMenu;
}

extern deluge::gui::menu_item::patched_param::IntegerNonFM noiseMenu;
extern deluge::gui::menu_item::osc::Sync oscSyncMenu;
extern deluge::gui::menu_item::osc::source::WaveIndex source0WaveIndexMenu;
extern deluge::gui::menu_item::osc::source::WaveIndex source1WaveIndexMenu;

extern deluge::gui::menu_item::sample::Start sample0StartMenu;
extern deluge::gui::menu_item::sample::Start sample1StartMenu;
extern deluge::gui::menu_item::sample::End sample0EndMenu;
extern deluge::gui::menu_item::sample::End sample1EndMenu;
extern deluge::gui::menu_item::audio_clip::SampleMarkerEditor audioClipSampleMarkerEditorMenuStart;
extern deluge::gui::menu_item::audio_clip::SampleMarkerEditor audioClipSampleMarkerEditorMenuEnd;
extern deluge::gui::menu_item::EditName nameEditMenu;
extern deluge::gui::menu_item::Submenu dxMenu;
extern deluge::gui::menu_item::Submenu stemExportMenu;
extern deluge::gui::menu_item::stem_export::Start startStemExportMenu;

extern deluge::gui::menu_item::firmware::Version firmwareVersionMenu;
extern deluge::gui::menu_item::sequence::Direction sequenceDirectionMenu;
extern deluge::gui::menu_item::Submenu soundEditorRootMenuMIDIOrCV;
extern deluge::gui::menu_item::Submenu soundEditorRootMenuMidiDrum;
extern deluge::gui::menu_item::Submenu soundEditorRootMenuGateDrum;
extern deluge::gui::menu_item::Submenu soundEditorRootMenuAudioClip;
extern deluge::gui::menu_item::Submenu soundEditorRootMenuPerformanceView;
extern deluge::gui::menu_item::Submenu soundEditorRootMenuSongView;
extern deluge::gui::menu_item::Submenu soundEditorRootMenuKitGlobalFX;
extern deluge::gui::menu_item::Submenu soundEditorRootMenu;
extern deluge::gui::menu_item::Submenu settingsRootMenu;

extern deluge::gui::menu_item::randomizer::RandomizerLock randomizerLockMenu;
extern deluge::gui::menu_item::randomizer::midi_cv::SpreadVelocity spreadVelocityMenuMIDIOrCV;
extern deluge::gui::menu_item::randomizer::midi_cv::NoteProbability randomizerNoteProbabilityMenuMIDIOrCV;

// note editor menu's
extern deluge::gui::menu_item::Submenu noteEditorRootMenu;
extern deluge::gui::menu_item::note::Probability noteProbabilityMenu;
extern deluge::gui::menu_item::note::IterancePreset noteIteranceMenu;
extern deluge::gui::menu_item::note::IteranceDivisor noteCustomIteranceDivisor;
extern deluge::gui::menu_item::note::IteranceStepToggle noteCustomIteranceStep1;
extern deluge::gui::menu_item::note::IteranceStepToggle noteCustomIteranceStep2;
extern deluge::gui::menu_item::note::IteranceStepToggle noteCustomIteranceStep3;
extern deluge::gui::menu_item::note::IteranceStepToggle noteCustomIteranceStep4;
extern deluge::gui::menu_item::note::IteranceStepToggle noteCustomIteranceStep5;
extern deluge::gui::menu_item::note::IteranceStepToggle noteCustomIteranceStep6;
extern deluge::gui::menu_item::note::IteranceStepToggle noteCustomIteranceStep7;
extern deluge::gui::menu_item::note::IteranceStepToggle noteCustomIteranceStep8;
extern deluge::gui::menu_item::note::Fill noteFillMenu;
// note row editor menu's
extern deluge::gui::menu_item::Submenu noteRowEditorRootMenu;
extern deluge::gui::menu_item::note_row::Probability noteRowProbabilityMenu;
extern deluge::gui::menu_item::note_row::IterancePreset noteRowIteranceMenu;
extern deluge::gui::menu_item::note_row::IteranceDivisor noteRowCustomIteranceDivisor;
extern deluge::gui::menu_item::note_row::IteranceStepToggle noteRowCustomIteranceStep1;
extern deluge::gui::menu_item::note_row::IteranceStepToggle noteRowCustomIteranceStep2;
extern deluge::gui::menu_item::note_row::IteranceStepToggle noteRowCustomIteranceStep3;
extern deluge::gui::menu_item::note_row::IteranceStepToggle noteRowCustomIteranceStep4;
extern deluge::gui::menu_item::note_row::IteranceStepToggle noteRowCustomIteranceStep5;
extern deluge::gui::menu_item::note_row::IteranceStepToggle noteRowCustomIteranceStep6;
extern deluge::gui::menu_item::note_row::IteranceStepToggle noteRowCustomIteranceStep7;
extern deluge::gui::menu_item::note_row::IteranceStepToggle noteRowCustomIteranceStep8;
extern deluge::gui::menu_item::note_row::Fill noteRowFillMenu;

extern deluge::gui::menu_item::PatchCables patchCablesMenu;
extern deluge::gui::menu_item::source::patched_param::ModulatorLevel modulator0Volume;
extern deluge::gui::menu_item::source::patched_param::ModulatorLevel modulator1Volume;

extern MenuItem* midiOrCVParamShortcuts[kDisplayHeight];

extern MenuItem* gateDrumParamShortcuts[kDisplayHeight];
extern MenuItem* paramShortcutsForSounds[kDisplayWidth][kDisplayHeight];
extern MenuItem* paramShortcutsForSoundsSecondLayer[kDisplayWidth][kDisplayHeight];
extern MenuItem* paramShortcutsForAudioClips[kDisplayWidth][kDisplayHeight];
extern MenuItem* paramShortcutsForSongView[kDisplayWidth][kDisplayHeight];
extern MenuItem* paramShortcutsForKitGlobalFX[kDisplayWidth][kDisplayHeight];

extern const std::array<deluge::gui::menu_item::HorizontalMenu*, 17> horizontalMenusChainForSound;
extern const std::array<deluge::gui::menu_item::HorizontalMenu*, 12> horizontalMenusChainForKit;
extern const std::array<deluge::gui::menu_item::HorizontalMenu*, 9> horizontalMenusChainForSong;
extern const std::array<deluge::gui::menu_item::HorizontalMenu*, 11> horizontalMenusChainForAudioClip;
extern const std::array<deluge::gui::menu_item::HorizontalMenu*, 2> horizontalMenusChainForMidiOrCv;
