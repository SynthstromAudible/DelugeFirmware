#pragma once
#include "gui/menu_item/audio_clip/audio_source_selector.h"
#include "gui/menu_item/audio_clip/sample_marker_editor.h"
#include "gui/menu_item/edit_name.h"
#include "gui/menu_item/filter/param.h"
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
#include "gui/menu_item/osc/audio_recorder.h"
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
#include "gui/menu_item/submenu/mod_fx.h"

namespace deluge::gui::menu_item {
class HorizontalMenu;
class HorizontalMenuGroup;
class HorizontalMenuContainer;
} // namespace deluge::gui::menu_item

extern gui::menu_item::patched_param::IntegerNonFM noiseMenu;
extern gui::menu_item::osc::Sync oscSyncMenu;
extern gui::menu_item::osc::source::WaveIndex source0WaveIndexMenu;
extern gui::menu_item::osc::source::WaveIndex source1WaveIndexMenu;

extern gui::menu_item::sample::Start sample0StartMenu;
extern gui::menu_item::sample::Start sample1StartMenu;
extern gui::menu_item::sample::End sample0EndMenu;
extern gui::menu_item::sample::End sample1EndMenu;
extern gui::menu_item::osc::AudioRecorder sample0RecorderMenu;
extern gui::menu_item::osc::AudioRecorder sample1RecorderMenu;
extern gui::menu_item::audio_clip::SampleMarkerEditor audioClipSampleMarkerEditorMenuStart;
extern gui::menu_item::audio_clip::SampleMarkerEditor audioClipSampleMarkerEditorMenuEnd;
extern gui::menu_item::EditName nameEditMenu;
extern gui::menu_item::Submenu dxMenu;
extern gui::menu_item::Submenu stemExportMenu;
extern gui::menu_item::stem_export::Start startStemExportMenu;

extern gui::menu_item::firmware::Version firmwareVersionMenu;
extern gui::menu_item::sequence::Direction sequenceDirectionMenu;
extern gui::menu_item::Submenu soundEditorRootMenuMIDIOrCV;
extern gui::menu_item::Submenu soundEditorRootMenuMidiDrum;
extern gui::menu_item::Submenu soundEditorRootMenuGateDrum;
extern gui::menu_item::Submenu soundEditorRootMenuAudioClip;
extern gui::menu_item::Submenu soundEditorRootMenuPerformanceView;
extern gui::menu_item::Submenu soundEditorRootMenuSongView;
extern gui::menu_item::Submenu soundEditorRootMenuKitGlobalFX;
extern gui::menu_item::Submenu soundEditorRootMenu;
extern gui::menu_item::Submenu settingsRootMenu;
extern gui::menu_item::submenu::ModFxHorizontalMenu globalModFXMenu;
extern gui::menu_item::submenu::ModFxHorizontalMenu modFXMenu;

extern gui::menu_item::randomizer::RandomizerLock randomizerLockMenu;
extern gui::menu_item::randomizer::midi_cv::SpreadVelocity spreadVelocityMenuMIDIOrCV;
extern gui::menu_item::randomizer::midi_cv::NoteProbability randomizerNoteProbabilityMenuMIDIOrCV;

// note editor menu's
extern gui::menu_item::HorizontalMenu noteEditorRootMenu;
extern gui::menu_item::note::Probability noteProbabilityMenu;
extern gui::menu_item::note::IterancePreset noteIteranceMenu;
extern gui::menu_item::note::IteranceDivisor noteCustomIteranceDivisor;
extern gui::menu_item::note::IteranceStepToggle noteCustomIteranceStep1;
extern gui::menu_item::note::IteranceStepToggle noteCustomIteranceStep2;
extern gui::menu_item::note::IteranceStepToggle noteCustomIteranceStep3;
extern gui::menu_item::note::IteranceStepToggle noteCustomIteranceStep4;
extern gui::menu_item::note::IteranceStepToggle noteCustomIteranceStep5;
extern gui::menu_item::note::IteranceStepToggle noteCustomIteranceStep6;
extern gui::menu_item::note::IteranceStepToggle noteCustomIteranceStep7;
extern gui::menu_item::note::IteranceStepToggle noteCustomIteranceStep8;
extern gui::menu_item::note::Fill noteFillMenu;
// note row editor menu's
extern gui::menu_item::HorizontalMenu noteRowEditorRootMenu;
extern gui::menu_item::note_row::Probability noteRowProbabilityMenu;
extern gui::menu_item::note_row::IterancePreset noteRowIteranceMenu;
extern gui::menu_item::note_row::IteranceDivisor noteRowCustomIteranceDivisor;
extern gui::menu_item::note_row::IteranceStepToggle noteRowCustomIteranceStep1;
extern gui::menu_item::note_row::IteranceStepToggle noteRowCustomIteranceStep2;
extern gui::menu_item::note_row::IteranceStepToggle noteRowCustomIteranceStep3;
extern gui::menu_item::note_row::IteranceStepToggle noteRowCustomIteranceStep4;
extern gui::menu_item::note_row::IteranceStepToggle noteRowCustomIteranceStep5;
extern gui::menu_item::note_row::IteranceStepToggle noteRowCustomIteranceStep6;
extern gui::menu_item::note_row::IteranceStepToggle noteRowCustomIteranceStep7;
extern gui::menu_item::note_row::IteranceStepToggle noteRowCustomIteranceStep8;
extern gui::menu_item::note_row::Fill noteRowFillMenu;

extern gui::menu_item::PatchCables patchCablesMenu;
extern gui::menu_item::source::patched_param::ModulatorLevel modulator0Volume;
extern gui::menu_item::source::patched_param::ModulatorLevel modulator1Volume;

extern MenuItem* midiOrCVParamShortcuts[kDisplayHeight];
extern MenuItem* gateDrumParamShortcuts[kDisplayHeight];
extern MenuItem* paramShortcutsForSounds[kDisplayWidth][kDisplayHeight];
extern MenuItem* paramShortcutsForSoundsSecondLayer[kDisplayWidth][kDisplayHeight];
extern MenuItem* paramShortcutsForAudioClips[kDisplayWidth][kDisplayHeight];
extern MenuItem* paramShortcutsForSongView[kDisplayWidth][kDisplayHeight];
extern MenuItem* paramShortcutsForKitGlobalFX[kDisplayWidth][kDisplayHeight];

extern deluge::vector<gui::menu_item::HorizontalMenu*> horizontalMenusChainForSound;
extern deluge::vector<gui::menu_item::HorizontalMenu*> horizontalMenusChainForKit;
extern deluge::vector<gui::menu_item::HorizontalMenu*> horizontalMenusChainForSong;
extern deluge::vector<gui::menu_item::HorizontalMenu*> horizontalMenusChainForAudioClip;
extern deluge::vector<gui::menu_item::HorizontalMenu*> horizontalMenusChainForMidiOrCv;
extern deluge::vector<gui::menu_item::HorizontalMenuContainer*> horizontalMenuContainers;

extern gui::menu_item::HorizontalMenuGroup sourceMenuGroup;
extern gui::menu_item::HorizontalMenu audioClipSampleMenu;
