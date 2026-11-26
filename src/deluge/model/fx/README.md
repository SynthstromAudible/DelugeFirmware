# FX

FX classes are between the application and the DSP. They have access to params via a
ParamManager, but don't know anything about settings, UI or the larger application state.

- `track_channel`, `master_channel`: a kit's or audio track's channel strip and the song's master channel, each
  owning its chain, the chain's settings and render copy (`effects_binding`) and its knobs' view (`chain_view`).
  `channel_setup` holds what both do when made and destroyed.
- `chain_inputs`: what each host chain reads from its params for one block.
- `song_stutter`: the song's one stutter, known by the chain core that plays it.
- `reverb_candidate`: the reverb's auto duck's search for the loudest send.
- `effects_owner`: `EffectsOwner`, one owner's effects as a value (its chain core, its settings binding, a host chain's
  view, the sound when it is one, its `knob_carrier`), and the functions over it: store, bind and project the settings,
  set a host chain's view, bind and unbind its chain record, clear the mod FX, and take the owner out of the render. A
  sound, a kit, an audio track and the master each name theirs; code that holds a ModelStack reaches it from the
  stack's typed owner through `model::effects_owner_of` (`model/lane_owner.h`). Code that holds an address finds it
  through `bridge::chainAt` / `effectsOwnerAt` / `soundAt`.
- `knob_carrier`: the learned MIDI knobs an owner holds outside a song's table (read from its file before a song holds
  it, and a render-made clone's source).
- `stutter_gesture`, `stutter_ui`: beginning and ending the song's stutter from a gold knob's press. Its UI half (the
  modes a stutter begins in, `UI_MODE_STUTTERING`, the LED redraw) is declared in `stutter_ui.h` and defined by the
  app's UI (`gui/ui/stutter_ui.cpp`), so this directory calls no UI.
- `chain_codec`: an owner's part of its file (a host chain's view words, the effects' attributes and tags, the learned
  knobs, a host chain's param block), called by each owner's codec where its file holds them.
- `chain_params`: the effects params' defaults for a sound, a host chain and an audio clip, from the owners' tables.
- `effect_reads`: an owner's unpatched effect params read by its table's ids, as the effects take them.
- `output_stage_gain`: the output stage's gain constants.

