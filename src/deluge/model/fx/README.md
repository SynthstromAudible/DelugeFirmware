# FX

FX classes are between the application and the DSP. They have access to params via a
ParamManager, but don't know anything about settings, UI or the larger application state.

- `track_channel`, `master_channel`: a kit's or audio track's channel strip and the song's master channel, each
  owning its chain, the chain's settings and render copy (`effects_binding`) and its knobs' view (`chain_view`).
  `channel_setup` holds what both do when made and destroyed.
- `chain_inputs`: what each host chain reads from its params for one block.
- `song_stutter`: the song's one stutter, known by the chain core that plays it.
- `reverb_candidate`: the reverb's auto duck's search for the loudest send.

