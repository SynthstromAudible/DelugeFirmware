# The test registry

`registry.toml` lists every test in the repository: the ctest suites under `tests/`, the cargo suites, the
component rigs (differentials, sanitizer stress runs, fuzzers) and the integration scenarios, each with its level,
runner, tier and prerequisites. `./dbt test` runs any selection of them:

    ./dbt test                          # the pr tier: what a pull request runs
    ./dbt test --level integration      # one level: unit, component or integration
    ./dbt test --tier nightly           # everything up to and including nightly
    ./dbt test <name> [<name> ...]      # by name, whatever the tier
    ./dbt test unit -- -R stitch_spec   # arguments after -- go to ctest
    ./dbt test --list                   # what exists, and what you can run right now
    ./dbt test --doctor                 # what is missing, and how to get it

A test whose prerequisites are missing is reported as skipped, with the reason; `--strict` (which CI passes) makes
a skip fail the run.

Each test also names its CI **job** (`render`, `app`, `model`, `storage`, `unit`, `unit-cpp`, `unit-cpp-variants`,
`tooling`): CI runs one GitHub job per value, and `./dbt test --ci-matrix` prints that matrix. `render`, `app` and
`model` run against a `golden_vt_render` built once for the whole run (`./dbt test --prebuilt`), so a test in them
builds only `golden-vt-render`.

**The table below is generated.** Edit `registry.toml`, then regenerate it (the pre-commit hook does this when
`registry.toml` is staged):

    ./dbt test --update-readme

<!-- BEGIN GENERATED TABLE -->
| Test | Level | Runner | Kind | Status | Gates | Tier | Job | Runtime |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| fs-differential | component | cargo | differential | gate | embedded-fatfs must agree with the reference FAT implementation on every filesystem operation | pr | storage | ~1m |
| upgrade-differential | integration | script | differential | gate | a file the upgrader (tools/deluge-upgrade) writes as JSON must load to exactly what its original loaded | nightly | storage | ~10s |
| upgrade-differential-corpus | integration | script | differential | gate | the upgrader against a real card backup | nightly | storage | ~10m |
| upgrader | unit | cargo | unit | gate | the file upgrader reads a Deluge document as the firmware's parser does, converts it as official firmware loaded it, and writes the firmware's JSON | pr | storage | ~30s |
| load-policy | integration | scenario | regression | gate | the sample loader's candidate order, admission rule, build contract and stream paths, policies no loader state can show | pr | app | ~10s |
| load-policy-wavetable-churn | integration | scenario | property | gate | wavetables evicted under memory pressure reload as they were built | nightly | app | ~1m |
| audio-format-corpus | component | cargo | differential | gate | the audio-format decoder must parse every file of a real WAV/AIFF corpus as the C++ header entry points do (skipped unless DELUGE_AUDIO_CORPUS is set) | pr | unit | ~20s |
| region-fill-differential | component | cargo | differential | gate | the Rust cluster fill must stay byte-identical to the C++ convert/stitch reference | pr | storage | ~1m |
| region-read-differential | component | cargo | differential | gate | the sample range-reader must resolve the same frames as the chunk-payload oracle | pr | storage | ~1m |
| region-source-differential | component | cargo | differential | gate | the region cursor must resolve a chunk's payload, not its header, over real streamed backing | pr | storage | ~1m |
| recorder-roundtrip | integration | scenario | simulation | gate | the SampleRecorder record-then-read-back round trip must be byte-exact on the Embassy host BSP | nightly | render | ~5m |
| audio-load-ab | integration | scenario | differential | gate | the async Rust audio loader must load every file the SAME way the synchronous C++ card path does, loaded in the same process as the reference | pr | app | ~5m |
| song-preview-ab | integration | scenario | differential | gate | the Rust prefix channel draws the same song artwork, and reports the same decode outcome and error, as the same file decoded synchronously | nightly | render | ~5m |
| preemptive-race-tsan | integration | script | sanitizer | gate | every cross-tier race site seen during sustained playback-while-record under ThreadSanitizer must have an entry in races.txt (the second invariant's inventory) | manual | unit | ~30m |
| snapshot-primitive-tsan | component | script | sanitizer | gate | Published<T> and SpscRing<E,N> must be race-free under concurrent stress | pr | unit | ~3m |
| dsp-ubsan | unit | ctest | sanitizer | gate | the DSP library specs (tests/unit/dsp) run without a signed overflow, an out-of-range shift or an out-of-range float-to-int conversion under UBSan | pr | unit-cpp-variants | ~5s |
| param-mapping-sweep | component | script | differential | gate | every one of the 2^32 stored words maps to the same knob position and menu number through each plugin mapping kind as the reference arithmetic | nightly | unit | ~40s |
| dsp-diff | component | script | differential | gate | the ARM/NEON DSP fast paths must be byte-identical to the portable host fallback | nightly | unit | ~5m |
| corpus-guard | integration | script | property | gate | golden_vt_render refuses, with exit 2, a run that asks for the corpus fixture's card without declaring the song corpus | pr | unit | ~1s |
| embassy-layout-invariance | integration | script | property | gate | the Embassy host's cordae MIXDOWN must not depend on any allocation address | nightly | unit | ~45s |
| save-roundtrip | integration | scenario | property | gate | a song saved through the real SaveSongUI ENTER gesture round-trips byte-identically through save, load and overwrite | nightly | model | ~5m |
| document-save-channel | integration | scenario | property | gate | a preset saved through the real ENTER gesture is written whole, read back through the channel and stored through the byte channel's write-from | nightly | app | ~10m |
| sample-collect-channel | integration | scenario | property | gate | a song saved with collect-media lands every sample it references in its own folder, byte-identical, and leaves every resident file findable | nightly | app | ~10m |
| sample-load-tree | integration | scenario | property | gate | a song saved with collected media loads back with each sample holder bound to its collected copy once the originals are gone | nightly | app | ~5m |
| sample-claim | integration | scenario | property | gate | files claimed from the sample browser, the menu's file selector and a favourite's by-path listing are bound by the Select encoder's claim | nightly | app | ~3m |
| preset-load | integration | scenario | property | gate | synth and kit presets written through the firmware's serialisers, each naming samples of its own, must load through the real preset browser | nightly | app | ~3m |
| song-save-channel | integration | scenario | property | gate | a song save builds its whole document in memory, writes it through the byte channel's write-from and reads back its length, head and tail | nightly | app | ~12m |
| clear-song-undo | integration | scenario | property | gate | Create New Song must run to completion on the tier its caller stands on, reading no card of its own | nightly | app | ~1m |
| sound-lifetime | integration | scenario | property | gate | Every deletion path must end a Sound's voices before any part of it is destroyed | nightly | app | ~1m |
| clip-lifetime | integration | scenario | property | gate | Which clip deletions free a clip the render still points at | nightly | app | ~1m |
| container-guard | integration | scenario | property | gate | Which structural edits to the containers the audio render walks are made while the render could preempt them | nightly | app | ~1m |
| playhead-moves | integration | scenario | property | gate | The interaction tier moves the playhead only with the render excluded | pr | app | ~30s |
| card-reinsert | integration | scenario | property | gate | what a card swap does to the samples still in memory | nightly | app | ~6s |
| audio-tick-hold | integration | scenario | regression | gate | the audio tick's hold and re-entry guard, on the booted app | pr | app | ~2s |
| stem-export | integration | scenario | property | gate | an offline stem export must render byte-identical audio and write it | nightly | render | ~12s |
| song-load-channel | integration | scenario | property | gate | the song load must read its bytes through the storage channel | nightly | app | ~10m |
| settings-channel | integration | scenario | property | gate | a settings value written through the real consumer entry point must survive a read back, and no settings file operation may begin on the interaction tier | nightly | app | ~4m |
| favourites-channel | integration | scenario | property | gate | the favourites bank must never be written with less than it held, and none of its card I/O may begin on the interaction tier | nightly | app | ~5m |
| startup-failsafe | integration | scenario | property | gate | no file operation in the startup song fail-safe may begin on the interaction tier | nightly | app | ~4m |
| path-operations | integration | scenario | property | gate | real SysEx path requests get their replies, no browser folder creation or delete runs on the interaction tier, and browser deletes go through the byte channel | nightly | app | ~4m |
| offset-io | integration | scenario | property | gate | blocks written at ascending offsets through the SysEx transfer verbs all stay on the card, and a copy reproduces its source byte for byte | nightly | app | ~4m |
| pattern-load | integration | scenario | property | gate | a pattern file the firmware's pattern writer made loads back through the pattern browser, and as a PLAY preview, with exactly the notes written | nightly | app | ~1m |
| staging-release | integration | scenario | property | gate | a staged document read its consumer abandons never writes into the buffer once it has been freed and handed out again | nightly | app | ~1m |
| clock-input | integration | scenario | property | gate | trigger-clock edges through the BSP's handler start playback with analog auto-start on, none dropped when they arrive slower than the playback routine | pr | app | ~1m |
| recorder-take | integration | scenario | property | gate | an audio-input recording take must leave the rest of the app running, and must attach its sample only to the sound it was recorded for | pr | app | ~5s |
| stale-ui | integration | scenario | property | gate | a song load takes every UI down for its whole length, and nothing acts on a UI the load took down while the UI stack is empty | pr | app | ~5s |
| ui-scripts | integration | scenario | regression | gate | the level-3 UI scripts drive the whole app through the BSP's own input path on the virtual clock | pr | app | ~1m |
| ui-menu-dump | integration | scenario | regression | gate | the declarative menu tree the app dumps as JSON equals website/src/data/generated/menu-tree.json | pr | app | ~2s |
| ui-scenes-spec | integration | scenario | regression | gate | the scenes' specs (tests/integration/ui/scenes) pass on golden_vt_render, one file per process from the app's own init, each on its card | pr | app | ~10s |
| load-cancel | integration | scenario | property | gate | a preset load can be cancelled by the ticket its request returned, and BACK in the preset browser cancels the browser's scroll-load | pr | app | ~5s |
| ui-timer-deadline | integration | scenario | property | gate | the "ui routine" job runs the UI timers at their deadlines instead of polling | pr | app | ~1m |
| idle-job-rates | integration | scenario | property | gate | an idle app runs each job only as often as it has work | pr | app | ~5s |
| render-song | integration | scenario | property | gate | a song must play as the device plays it | pr | render | ~20s |
| render-kit-fx | integration | scenario | property | gate | a kit's effect chain renders as recorded | pr | render | ~20s |
| render-kit-filters | integration | scenario | property | gate | a kit's filter chain renders as recorded | pr | render | ~20s |
| render-audio-track-filters | integration | scenario | property | gate | an audio track's filter chain renders as recorded | pr | render | ~20s |
| render-master-filters | integration | scenario | property | gate | the song master's filter chain renders as recorded | pr | render | ~20s |
| render-audio-cents | integration | scenario | property | gate | an audio clip's fine tune renders as recorded, and differs from the clip untuned | pr | render | ~20s |
| render-audio-stretch | integration | scenario | property | gate | audio clips stretched to the song's tempo render as recorded, and differ from the same clips resampled | pr | render | ~20s |
| render-audio-stretch-native | integration | scenario | property | gate | audio clips at their native tempo render as recorded, a fudged stretch crossfading their loops | pr | render | ~20s |
| render-audio-stretch-arranged | integration | scenario | property | gate | stretched audio clips render as recorded from arrangement instances that end without looping | pr | render | ~20s |
| render-audio-stretch-edges | integration | scenario | property | gate | audio clips at the edges of the stretcher's paths (short pre-margins, odd rates, tied, past the end) render as recorded | pr | render | ~8s |
| render-audio-stretch-marker | integration | scenario | property | gate | a stretched audio clip's end marker dragged in while its voice plays renders as recorded, and differs from the clip left alone | pr | render | ~20s |
| render-audio-stretch-length | integration | scenario | property | gate | a stretched audio clip lengthened by a stretch while its voice plays renders as recorded, and differs from the clip left alone | pr | render | ~20s |
| render-audio-stretch-late | integration | scenario | property | gate | playback started part-way through stretched arrangement instances renders as recorded, each clip starting late | pr | render | ~20s |
| render-audio-track-fx | integration | scenario | property | gate | an audio track's effect chain renders as recorded | pr | render | ~20s |
| render-master-fx | integration | scenario | property | gate | the song master's effect chain renders as recorded | pr | render | ~20s |
| render-master-pan-stereo | integration | scenario | property | gate | the song master's pan renders as recorded in stereo, where it applies | pr | render | ~20s |
| render-master-arranger | integration | scenario | property | gate | the song master's automation plays in the arranger | pr | render | ~20s |
| render-master-grain-off | integration | scenario | property | gate | the song master's granular mod FX in its soft off renders as recorded | pr | render | ~20s |
| render-kit-svf-morph | integration | scenario | property | gate | a kit's state-variable filters render as recorded with their morphs up | pr | render | ~20s |
| render-audio-track-drive | integration | scenario | property | gate | an audio track's oversampled drive filter, saturating ladder HPF and granular mod FX render as recorded | pr | render | ~20s |
| render-kit-filter-fade | integration | scenario | property | gate | a kit's filters fade in as recorded when switched on | pr | render | ~20s |
| render-audio-track-filter-fade | integration | scenario | property | gate | an audio track's filters fade in as recorded when switched on | pr | render | ~20s |
| render-audio-ducked | integration | scenario | property | gate | sidechain hits duck as recorded | pr | render | ~20s |
| render-duck-shapes | integration | scenario | property | gate | the sidechain envelope's attack and release shapes, and a stronger hit taken mid-attack and mid-release, render as recorded on the host owners | pr | render | ~20s |
| render-kit-comp | integration | scenario | property | gate | a kit's compressors render as recorded | pr | render | ~20s |
| render-kit-delay | integration | scenario | property | gate | a kit's delay renders as recorded through rate changes and its end | pr | render | ~20s |
| render-delay-owners | integration | scenario | property | gate | a delay renders as recorded on each owner the kit and audio-track delay gates leave out | pr | render | ~20s |
| render-stutter-audio | integration | scenario | property | gate | an audio track's stutter renders as recorded | pr | render | ~20s |
| render-stutter-synth | integration | scenario | property | gate | a synth's stutter renders as recorded | pr | render | ~20s |
| render-stutter-kit | integration | scenario | property | gate | a kit's quantized, reversed stutter renders as recorded | pr | render | ~20s |
| render-stutter-master | integration | scenario | property | gate | the song master's ping-pong stutter renders as recorded | pr | render | ~20s |
| render-stutter-rate | integration | scenario | property | gate | a stutter whose rate knob is turned while it plays renders as recorded | pr | render | ~20s |
| render-stutter-handoff | integration | scenario | property | gate | a stutter handed from one owner to another renders as recorded | pr | render | ~20s |
| render-tempo-delays | integration | scenario | property | gate | five synced delays resizing together after a tempo change render as recorded | pr | render | ~20s |
| render-audio-track-delay | integration | scenario | property | gate | an audio track's digital delay renders as recorded through rate changes | pr | render | ~20s |
| render-kit-pingpong-stereo | integration | scenario | property | gate | a kit's ping-pong delay renders as recorded in stereo, where its echoes cross channels | pr | render | ~20s |
| render-audio-track-delay-toggle | integration | scenario | property | gate | an analog delay switched off and on again while it rings renders as recorded | pr | render | ~20s |
| render-audio-track-delay-switch | integration | scenario | property | gate | an analog delay switched to digital and back to analog while it rings renders as recorded | pr | render | ~40s |
| render-pingpong-stereo-sweep | integration | scenario | property | gate | ping-pong delays render as recorded in stereo, crossing channels while their rates step through new buffers | pr | render | ~20s |
| render-audio-track-delay-extremes | integration | scenario | property | gate | a delay renders as recorded at both ends of its rate's range | pr | render | ~20s |
| render-low-clip | integration | scenario | property | gate | a host chain's saturation below amount 3, where it does not shift back up, renders as recorded | pr | render | ~20s |
| render-bitcrush | integration | scenario | property | gate | the bitcrush paths the FX songs leave out render as recorded | pr | render | ~20s |
| render-eq-bands | integration | scenario | property | gate | the EQ's single bands render as recorded | pr | render | ~20s |
| render-reverb-duck-override | integration | scenario | property | gate | the reverb's duck taken from a synth with the most reverb renders as recorded | pr | render | ~20s |
| render-reverb-duck-override-track | integration | scenario | property | gate | the reverb's duck taken from an audio track with the most reverb renders as recorded | pr | render | ~20s |
| render-metronome | integration | scenario | property | gate | the metronome's click renders as recorded | pr | render | ~20s |
| render-audio-track-comp-onset | integration | scenario | property | gate | a compressor switched on while its audio sounds renders as recorded | pr | render | ~20s |
| render-audio-track-comp-reset | integration | scenario | property | gate | a compressor reset mid-sound renders as recorded when it comes back on | pr | render | ~20s |
| render-audio-track-comp-blend | integration | scenario | property | gate | a blended compressor on a loud audio track renders as recorded, so its 64-bit level sum is pinned | pr | render | ~20s |
| render-modfx-mono | integration | scenario | property | gate | the warble and dimension mod FX, which no synth golden plays, render as recorded in mono | pr | render | ~20s |
| render-modfx-stereo-a | integration | scenario | property | gate | the flanger, chorus and phaser mod FX render as recorded in stereo, where the comb types read a second tap for the right channel | pr | render | ~20s |
| render-modfx-stereo-b | integration | scenario | property | gate | the stereo chorus, warble and dimension mod FX render as recorded in stereo | pr | render | ~20s |
| render-modfx-soft-off | integration | scenario | property | gate | a mod FX switched off and on again by its knob renders as recorded | pr | render | ~20s |
| render-modfx-switch | integration | scenario | property | gate | a mod FX switched to another type and back while it plays renders as recorded, its LFO re-seating each time its wave changes after it has rendered | pr | render | ~20s |
| render-modfx-clear | integration | scenario | property | gate | a mod FX's memory cleared while it plays renders as recorded, both with a delay line and without one | pr | render | ~20s |
| render-grain-mono | integration | scenario | property | gate | the granular mod FX renders as recorded in mono on a host owner's chain | pr | render | ~20s |
| render-grain-stereo | integration | scenario | property | gate | the granular mod FX renders as recorded in stereo, where each grain is panned | pr | render | ~20s |
| render-grain-switch | integration | scenario | property | gate | the granular mod FX switched on and off by its type, and its memory cleared, while it plays renders as recorded | pr | render | ~20s |
| render-grain-master | integration | scenario | property | gate | the song's master chain entering and leaving the granular mod FX by its type while it plays renders as recorded | pr | render | ~20s |
| render-reverb-freeverb | integration | scenario | property | gate | the song's Freeverb reverb renders as recorded at knobs of its own, its width at half, so each channel's output takes in the other's (wet2 above 0) | pr | render | ~20s |
| render-reverb-mutable | integration | scenario | property | gate | the song's Mutable reverb renders as recorded at knobs of its own, its room, damping, width, high-pass and low-pass away from the boot song's | pr | render | ~20s |
| render-reverb-digital | integration | scenario | property | gate | the song's Digital reverb renders as recorded at knobs of its own, panned halfway left and rendered in stereo, so the two channels' return levels differ | pr | render | ~20s |
| render-reverb-switch | integration | scenario | property | gate | the song's reverb switched between its models, with its knobs turned while it plays, renders as recorded | pr | render | ~20s |
| render-grain-restart | integration | scenario | property | gate | the granular mod FX started again renders as recorded, its grains reading 0 from every slot not yet written since the restart | pr | render | ~25s |
| render-grain-owners | integration | scenario | property | gate | the granular mod FX switched by a synth's own type setter, and cleared through each owner's own clear, while it plays renders as recorded | pr | render | ~20s |
| render-song-corpus | integration | scenario | property | gate | the render-song checks, on a real song from the maintainer's card backup (Cordae) | nightly | render | ~30s |
| golden-synth | integration | scenario | differential | gate | every synth golden master must render as recorded | pr | render | ~20s |
| param-dump | integration | scenario | regression | gate | the audible window's parameter dump | nightly | render | ~5s |
| golden-synth-arm | integration | scenario | differential | gate | the synth golden masters on 32-bit ARM | nightly | unit | ~1m |
| render-audio-stretch-arm | integration | scenario | property | gate | audio clips stretched to the song's tempo render on 32-bit ARM as recorded on x86 | nightly | unit | ~20s |
| render-audio-stretch-edges-arm | integration | scenario | property | gate | audio clips at the edges of the stretcher's paths render on 32-bit ARM as recorded on x86 | nightly | unit | ~20s |
| streaming-margin | integration | scenario | property | gate | sample playback must keep up when storage is as slow as the device's | nightly | render | ~30s |
| streaming-margin-sweep | integration | scenario | simulation | investigative | nothing: it prints the streaming margin curve, the underruns at each fill-task wait in MARGIN_SWEEP | manual | render | ~1m |
| recorder-write | integration | scenario | property | gate | a real recording must still read back byte-correctly | nightly | app | ~10m |
| unit | unit | ctest | unit | gate | the test directories under tests/unit that name the host tree (the DSP library specs among them), and the gfx-layer and layer-rules checks, on the -m32 host | pr | unit-cpp | ~5s |
| unit-arm | unit | ctest | unit | gate | the DSP library, NEON, model, effects, instrument, music, waveform view, serialization, plugin, host-chain and bench specs and model equality hold on ARM | pr | unit-cpp-variants | ~5s |
| crates | unit | cargo | unit | gate | every #[test] in the core libraries: the allocator, the resource manager and the sample crates | pr | unit | ~20s |
| bsp-host | unit | cargo | unit | gate | the Rust BSP's host tests and in-crate #[test]s | pr | unit | ~30s |
| sample-convert | unit | cargo | unit | gate | deluge_storage_sample_convert's sample format conversions agree with the app C++ its build.rs compiles | pr | storage | ~1m |
| param-projection | integration | scenario | unit | gate | the param/automation DOM leaf must agree with the engine it replaces | pr | model | ~1s |
| param-table-check | integration | scenario | unit | gate | the engines' param tables agree with the runtime | pr | model | ~1s |
| param-target-check | integration | scenario | unit | gate | the host names params by target | pr | model | ~1s |
| param-clone | integration | scenario | unit | gate | cloning the real engine param containers (AutoParam, a ParamManager with patched, unpatched and patch-cable collections) must give deep-independent copies | pr | model | ~1s |
| patch-routing | integration | scenario | property | gate | a patch-cable set's routing and save order must not change | pr | model | ~2s |
| document-version | integration | scenario | unit | gate | the firmware version a document is read as is that document's own | pr | model | ~1s |
| preset-parse | integration | scenario | unit | gate | a JSON synth preset reads into a kit row as an XML one does | pr | model | ~1s |
| effect-owners | integration | scenario | unit | gate | every owner of effects (a synth's sound, a kit, an audio track, the song master) keeps its effect settings in its own state, and the render's copy follows | pr | model | ~1s |
| edit-cost | integration | scenario | benchmark | investigative | nothing: it times SetEngineSetting on a new synth for a re-voicing scalar, a retuning scalar, an oscillator type switch and an engine switch | manual | model | ~5s |
| source-memory | integration | scenario | benchmark | investigative | nothing: it prints the SRAM and SDRAM heaps' free bytes and low-water marks across a sample oscillator's switch to a saw and back | manual | model | ~5s |
| edit-substrate | integration | scenario | unit | gate | the Edit substrate on its own | pr | model | ~1s |
| voice-pins | integration | scenario | unit | gate | which voice each cull, steal and admit takes across sounds, against the record | pr | model | ~1s |
| mapping-dispatch | integration | scenario | property | gate | the mapping index matches incoming MIDI as the dispatch's scans of the engine fields do | pr | app | ~5s |
| xml-replay-diff | integration | scenario | differential | gate | ValueTreeDeserializer's XML replay must read exactly as XMLDeserializer does | pr | model | ~1s |
| song-roundtrip | integration | corpus | regression | gate | what a song saves as must not change unexplained | nightly | model | ~10s |
| model-equality-corpus | unit | script | property | gate | every corpus song's model records survive a JSON round trip unchanged, through a driver built from the model and serialization libraries only | nightly | tooling | ~2s |
| preset-roundtrip | integration | corpus | regression | gate | what a preset saves as must not change unexplained | nightly | model | ~10s |
| serializer-check | integration | corpus | property | gate | the tree ValueSerializer builds for every output of every corpus song equals the tree the JSON writer's text parses to | nightly | model | ~10s |
| cable-decode | integration | corpus | property | gate | the Deluge instrument decodes every song's and preset's patch cables as the app's reader does | nightly | model | ~20s |
| host-effect-settings | integration | corpus | property | gate | a host owner's effects go through its chain | nightly | model | ~30s |
| engine-settings | integration | corpus | property | gate | a sound's settings go through its slot | nightly | model | ~30s |
| song-settings | integration | corpus | property | gate | a song's settings go through SetSetting | nightly | model | ~30s |
| instrument-codec-decode | integration | corpus | property | gate | the Deluge instrument reads its own state as the app's reader does | nightly | model | ~20s |
| instrument-codec-encode | integration | corpus | property | gate | the Deluge instrument writes its own state | nightly | model | ~20s |
| instrument-state-roundtrip | integration | corpus | property | gate | the Deluge instrument's state round-trips through its codec | nightly | model | ~20s |
| json-roundtrip | integration | corpus | property | gate | the JSON read path must be faithful to the JSON write path | nightly | model | ~10s |
| model-decode-diff | integration | corpus | property | gate | decode reads a saved song's model records exactly as the engine loader does | pr | model | ~10s |
| song-edit-roundtrip | integration | corpus | property | gate | a DOM-first edit must persist | nightly | model | ~10s |
| param-record-diff | integration | corpus | regression | gate | the engine's live automation recording must reproduce its committed baselines (tests/golden/param_record) | nightly | model | ~5s |
| arrangement-take | integration | scenario | regression | gate | recording the session into the arrangement must make the takes it made when its baseline was recorded (tests/golden/arrangement_take) | nightly | render | ~20s |
| audio-clip-edit-diff | integration | corpus | regression | gate | an AudioClip's descriptor edits, their undo, and a save and reload reproduce the committed baseline byte for byte | nightly | model | ~5s |
| edit-undo-redo | integration | corpus | property | gate | every reified Edit applied to a corpus song resolves through a live DOM address and comes back exactly through the real undo and redo | nightly | model | ~10s |
| clip-clone-roundtrip | integration | corpus | property | gate | InstrumentClip::clone() of a corpus song's clip is a distinct, deep-independent copy of its note rows, notes, param collections and stores | nightly | model | ~10s |
| param-snapshot-bench | integration | scenario | benchmark | investigative | nothing: it times ParamSnapshot::applyPatch against the full projection rebuild for the same single-param edits | manual | app | ~2s |
| setting-menus | unit | script | unit | gate | no gesture under src/deluge/gui or src/ui/scenes sets a bound sound's instrument state outside its Edit | pr | tooling | ~1s |
| test-support-names | unit | script | unit | gate | no per-directory support/ folder under tests/ holds a path tests/support/ also holds | pr | tooling | ~1s |
| spec-discovery | unit | script | unit | gate | a test tree builds exactly the test directories that name it, and refuses a spec no driver builds or a CMakeLists.txt that names no tree or an unknown one | pr | tooling | ~1s |
| doc-hygiene-selftest | unit | script | unit | gate | the UI docs lint flags history breadcrumbs, any line number in a citation, and a backticked path naming no one file or a symbol that file lacks | pr | tooling | ~1s |
| spec-comparisons-selftest | unit | script | unit | gate | the spec-comparison check finds a comparison judged by a truth matcher and nothing else, and refuses only the sites a commit adds | pr | tooling | ~1s |
| fixed-point-raw-selftest | unit | script | unit | gate | the fixed-point raw-arithmetic ratchet counts raw helper calls, DSP inline asm, seam views and typed-boundary crossings apart, outside comments | pr | tooling | ~1s |
| fixed-point-raw | unit | script | unit | gate | raw fixed-point arithmetic, raw seam views and typed-boundary crossings outside the core fixed-point headers only shrink | pr | tooling | ~2s |
| spec-comparisons | unit | script | unit | gate | no C++ spec judges a comparison with to_be_true(), to_be_false(), to_be_truthy() or to_be_falsy(), so every failed comparison prints its values | pr | tooling | ~2s |
| dx7-presets | unit | script | unit | gate | the render presets that hold a DX7, and whether each one's other oscillator can sound, are as recorded | pr | tooling | ~1s |
| dx7-presets-selftest | unit | script | unit | gate | the DX7 preset scan finds a DX7 on either slot, and an audible other oscillator by its volume or a cable | pr | tooling | ~1s |
| layers-selftest | unit | script | unit | gate | the include-layer checker holds each library to what it declares, counts each rule, and refuses a new src/ directory, a shadowed path and legacy growth | pr | tooling | ~1s |
| registry-selftest | unit | script | unit | gate | this registry's loader, selection and probes, the SDRAM packer, four source checkers, Cargo members, crate-header lists, temp-dir removal, coverage-tool seams | pr | tooling | ~1s |
| render-preset-selftest | unit | script | unit | gate | the golden preset render's staging removes a JSON preset's own MIDI learn and leaves the preset valid JSON, wherever the member stands | pr | tooling | ~1s |
| make-sample-selftest | unit | script | unit | gate | the sample golden cases' synthetic sources are what their cases assume, and the committed WAVs are their generator's output | pr | tooling | ~1s |
| dsp-bench-parser | unit | script | unit | gate | the DSP bench host script's parsing of device captures | pr | tooling | ~1s |
| compare-codegen-selftest | unit | script | unit | gate | the machine-code comparison of two images names what an address points at instead of its value | pr | tooling | ~1s |
| relocate-selftest | unit | script | unit | gate | a library move respells the includes resolving into it, rewrites its paths, namespaces and targets, and refuses an ambiguous move | pr | tooling | ~1s |
| compare-strings-selftest | unit | script | unit | gate | the string comparison of two images equates strings that differ only by the renamed paths and crates, and reports any other change | pr | tooling | ~1s |
| stream-bench-parser | unit | script | unit | gate | the streaming benchmark host script's parsing of device captures | pr | tooling | ~1s |
| device-check-parser | unit | script | unit | gate | device_check's reading of a rig capture | pr | tooling | ~1s |
| rig-session | unit | script | unit | gate | rig_session's reading of the rig harness's RTT stream | pr | tooling | ~2s |
| rig-lock | unit | script | unit | gate | one run at a time holds the rig, every tool that opens it is refused or waits, and a holder that dies lets go | pr | tooling | ~6s |
| device-parity | unit | script | unit | gate | emu_parity's script runner drives delugemu or the rig through the same steps and timings, and device_parity seeds delugemu's flash from the rig's settings | pr | tooling | ~1s |
| ui-script-cards | unit | script | unit | gate | the cards a UI script's header names, as the UI script runners read them | pr | tooling | ~1s |
| x86-parity-tool | unit | script | unit | gate | x86_parity turns a parity script into the host app's UI script, keys its card cache by the image's mtime, and lists every file two runs disagree on | pr | tooling | ~1s |
| deluge-jlink | unit | script | unit | gate | deluge_jlink's load script stops every DMA channel after the MMU is off and before the load | pr | tooling | ~1s |
| ui-parity | integration | script | differential | investigative | the parity scripts show the same panels on this image as on main's, in delugemu, on every card their headers name | manual | tooling | ~20m |
| x86-parity | integration | script | differential | gate | every parity script played twice on the same host app gives identical panels, LEDs, pads and MIDI out: the determinism a base-against-head comparison rests on | manual | tooling | ~1m |
| ui-parity-card | integration | script | differential | investigative | the content-card parity scripts show the same panels on this image as on main's, in delugemu | manual | tooling | ~5m |
| ui-parity-songs | integration | script | differential | investigative | eleven real songs load and play with the same panels on this image as on main's, in delugemu | manual | tooling | ~90s |
| ui-device | integration | script | differential | investigative | the device scripts show the same panels on the Deluge on the probe rig as in delugemu running the same image | manual | tooling | ~3m |
| ui-device-record | integration | script | benchmark | investigative | nothing: it records the device-only scripts' snapshots and debug output on the Deluge on the probe rig | manual | tooling | ~3m |
| usbmidi | integration | script | property | gate | a Deluge plugged into this machine enumerates as a USB-MIDI device with three MIDI ports each way | manual | tooling | ~5s |
| audible-window-tooling | unit | script | unit | gate | the audible window's analyser and song set | pr | tooling | ~1s |
| audible-window-identity | integration | script | regression | gate | the audible window's reference snapshot (.superpowers/estate/audible-window/reference/, or AUDIBLE_WINDOW_REFERENCE) compared with itself reports nothing moved | manual | tooling | ~1m |
| bench-tooling | unit | script | unit | gate | the bench tools' host side | pr | tooling | ~1s |
| ptm-trace-tool | unit | script | unit | gate | the PTM trace tool's host side | pr | tooling | ~1s |
| app-asan-sample-load-tree | integration | scenario | sanitizer | gate | the sample-load-tree gate's checks, on a host app built with AddressSanitizer and UBSan and the allocator's heap poisoning | nightly | unit | ~5m |
| app-asan-sample-claim | integration | scenario | sanitizer | gate | the sample-claim gate's checks, on a host app built with AddressSanitizer and UBSan and the allocator's heap poisoning | nightly | unit | ~15m |
| app-asan-preset-load | integration | scenario | sanitizer | gate | the preset-load gate's checks, on a host app built with AddressSanitizer and UBSan and the allocator's heap poisoning | nightly | unit | ~5m |
| app-asan-recorder-take | integration | scenario | sanitizer | gate | the recorder-take gate's checks, on a host app built with AddressSanitizer and UBSan and the allocator's heap poisoning | nightly | unit | ~5m |
| app-asan-ui-scenes-spec | integration | scenario | sanitizer | gate | the ui-scenes-spec gate's checks, on a host app built with AddressSanitizer and UBSan and the allocator's heap poisoning | nightly | unit | ~5m |
| app-asan-effect-owners | integration | scenario | sanitizer | gate | the effect-owners gate's checks, on a host app built with AddressSanitizer and UBSan and the allocator's heap poisoning | nightly | unit | ~20s |
| app-asan-edit-substrate | integration | scenario | sanitizer | gate | the edit-substrate gate's checks, on a host app built with AddressSanitizer and UBSan and the allocator's heap poisoning | nightly | unit | ~5m |
<!-- END GENERATED TABLE -->

## Where things live, and why

Placement follows coupling to the BSP source tree, not language or purpose.

- **`tests/component/differential/`** — harnesses whose subject is a Rust library crate under `src/` or
  `cc`-compiled application C++, rather than the BSP itself.

  One of these, `fs_differential`, also uses the BSP: its `tests/efatfs_core.rs` drives the BSP
  library's storage-generic `storage::efatfs::portable` over the harness's host filesystem. It stays
  here because its subject is the `embedded-fatfs` stack (deluge-sdk's published fork), not the BSP.
- **`tests/component/stress/`, `tests/component/dsp-diff/`** — standalone C++ rigs with their own `run.sh`, deliberately
  not ctest-driven.
- **`tests/harness/golden_vt_render/`, `tests/harness/preemptive_race_tsan/`** — harnesses built on the BSP
  library. `golden_vt_render` runs the BSP under its own executor and virtual clock; its build script links the
  host app through `src/platform/host/link`. `src/platform/device/HOST_HARNESS.md` documents both.

All of these are members of the one Cargo workspace at the repository root, with one `Cargo.lock`, one
`rust-toolchain.toml` and one `[patch.crates-io]` (the embassy fork). Build them with `-p <package>`, never
`--workspace`:

1. `embassy-time`'s `std` and `mock-driver` features both emit `#[no_mangle] _embassy_time_now`. Cargo unifies
   features across all the packages of one build, so two time drivers cannot share a build.
2. The device crate's platform features are mutually exclusive with the host ones, so a build that spans it and a host
   harness would unify them wrongly.

`src/base/alloc/fuzz` and `tools/deluge-upgrade` are the exceptions: each is its own workspace root, with its own lock.

## Build products and trees

A test declares the build products it needs in `builds`; `./dbt test` builds the union of a selection's products
once, dependencies first, before running any test (`scripts/tasks/test_builds.py`). No test builds a product
itself, so none can run against a stale one.

| Product | Tree | What it is |
| --- | --- | --- |
| `host-app` | `build/host-app` | the C++ app's object files for x86-64, from `src/platform/host/`; nothing links here |
| `golden-vt-render` | `build/cargo-harness` | the Embassy host harness binary every scenario gate runs; its `build.rs` archives `build/host-app`'s objects |

A ctest entry's own tree (`build/test-host`, `build/test-arm`, `build/test-ubsan`) is configured and built by its
runner.

Every cargo-built test and product shares `build/cargo-harness`, and cargo takes an exclusive lock per target
directory, so two runs at once serialize rather than run in parallel. `golden_vt_render` builds under the `harness`
profile (`build/cargo-harness/harness/golden_vt_render`); the C++ unit-test tree's staticlib builds under `host-test`.

Running `cargo` by hand without the runner's target directory creates a local `target/`, and duplicate build trees add
up to tens of gigabytes. Prefer `./dbt test <name>`;
`./dbt test --clean` drops every tree the runner builds.

`scripts/tasks/stretcher_coverage.sh [out-dir]` is the one build outside the runner: it compiles the host app with
clang source coverage into its own trees under `out-dir` (default `build/stretcher-coverage`), links its own
`golden_vt_render`, runs golden-synth and every render-song gate compared with songs.json on it through their own
checks, and reports the line and branch coverage of the time stretcher and of its users' functions that call into it
(`scripts/tasks/stretcher_coverage.py`'s `SCOPE`). Only renders compared with a record count. Run it again after a
change to the stretcher or its callers, to see which branches the goldens still pin; `llvm-profdata` and `llvm-cov`
must be the version of the `clang` on the path. Its trees are its own: `./dbt test --clean` does not remove
`build/stretcher-coverage` (or another `out-dir`); delete it by hand.

## Adding a test

Add a `[[harness]]` block to `registry.toml` and regenerate the table. `name`, `level`, `runner`, `kind`, `status`,
`gates`, `tier`, `job` and `runtime_s` are mandatory: a test that cannot state the regression it catches is one nobody
can later decide to delete: a crate whose own header declares it throwaway survives indefinitely when nothing records
that fact anywhere a reader would look. The loader refuses a field it does not know.

### Writing an entry

- **`gates`** is one sentence of at most 160 characters, with no full stop: what the test gates, as the table shows
  it. Everything else a reader needs -- the legs, the markers, why a check exists -- is the test's explanation, a
  block in the file that judges it: a check module's docstring (scenario entries with a `check`), the scenario's or
  driver's `.cpp` comment (scenario entries without one), the corpus runner's docstring, the crate's `Cargo.toml`,
  the tree's `CMakeLists.txt`, or, for a script entry, the header of the file its `run` runs:

      Gate <name>:
          <the explanation, indented under the header>

  A script entry's file is in its `path`: the program `run` starts with, the script after `python3`, or
  `<module>.py` for `python3 -m <module>`. For `python3 -m unittest` it is the first module named, and with none
  named, or `discover`, the module named after `path`'s last directory (`path = "tools/x"` reads `tools/x/x.py`);
  the loader refuses a `run` from which it cannot name one.
  A check whose entries' explanations would swamp its docstring keeps them in a notes file beside it, one
  `## <name>` section per entry (`harness_registry.CHECK_NOTES`: `render_song.py`'s are in `render_song_cases.md`).
  `./dbt test --list --verbose <name>` prints the explanation. Every entry has one, unless its gates line says
  everything: such an entry is listed in `NO_EXPLANATION` (`scripts/tasks/test_harness_registry.py`), and
  `registry-selftest` fails on any other entry without an explanation, and on a block whose entry is gone.
- **`runtime_s`** is the whole seconds a run takes with its build products already built. A cold build's cost goes in
  a comment above it. A test with no natural end states its usual run, and a comment says so.
- **`path`** belongs only to `cargo` and `script` entries: the directory their commands run in. A ctest entry runs in
  its `source`.
- **`requires`** lists the probes the test needs (`./dbt test --doctor` names the missing ones); leave it out when it
  needs none.
- **Templates.** `template = "<name>"` takes the fields of `[template.<name>]` at the top of `registry.toml`
  (`render-song`, `render-preset`, `scenario-driver`, `scenario`, `corpus`, `unittest`). An entry that sets a field its
  template also sets must list it, `override = ["markers"]`; an undeclared difference, an override that changes
  nothing and an override of a field the entry does not set are all refused. Templates do not nest.
  A `render-song` entry adds its `gates` and the song it plays: `env = { RENDER_GENERATED = "<song>" }` for a song
  `tests/golden/render/songs/make_render_song.py` generates, or `RENDER_SONG` with its `GOLDEN_FIXTURE` card for
  a song from the corpus (with neither, it plays RS_STREAM); its explanation is a `## <name>` section in
  `tests/integration/checks/render_song_cases.md`, and `./dbt test --update <name>` records its output
  (`render_song.py`'s docstring has the rest).
  A `unittest` entry adds `path`, `gates` and `run` (`["python3", "-m", "unittest", "<module>", ...]`, run in
  `path`); its explanation is a `Gate <name>:` block in the docstring of the first module `run` names.
- **Variants.** `variants = ["asan"]` on a scenario entry generates `app-asan-<name>`: the entry's scenario, markers,
  result and check on the host app built with AddressSanitizer (`[variant.asan]` gives its builds, environment, job
  and tier). `variant_fields = { asan = { runtime_s = N } }` sets the copy's run time. The copy's explanation is a
  `Gate app-asan-<name>:` block in the base's file, the one that judges both. A copy never owns a baseline:
  `./dbt test --update <name>` re-records the base, and the copy only checks.
- `./dbt test --dump-expanded` prints every entry as the runner sees it, templates and variants applied; a rewrite of
  the registry that changes no test leaves it identical.

### A unit spec

`tests/unit/<path>/` holds the specs for `src/<path>/`: a top-level library's specs are in its directory
(`tests/unit/effects/`, `tests/unit/ui/widgets/`), a library nested in another has its own (`tests/unit/ui/scenes/bind/`), and the
app's are by subsystem (`tests/unit/deluge/storage/`, `tests/unit/deluge/gui/`). Specs of code outside `src/` keep its
path: `tests/unit/harness/` for `tests/harness/`,
`tests/unit/integration/scenarios/support/` for the scenarios' shared support. A spec whose stand-ins cannot share a
driver with its neighbours' (an `IN_UNIT_TESTS` world, a deserializer stack) gets a driver of its own in a directory
named for its subject file, `tests/unit/<path>/<subject stem>/` (`tests/unit/deluge/modulation/sync/` for
`src/deluge/modulation/sync.cpp`).

A directory's `CMakeLists.txt` declares its driver and its trees, the tree names on `TREES`'s own line:

    deluge_specs_driver(<name> TREES host arm [NEON] [ONLY_HOST|ONLY_ARM|ONLY_UBSAN <spec>...] [SOURCES ...]
                        [LIBRARIES ...] [INCLUDES ...] [DEFINITIONS ...])

(`tests/support/cmake/specs.cmake` documents every argument). A new driver is named after its directory under
`tests/unit`, with `/` and `-` as `_`, plus `_specs` (`deluge_storage_song_preview_specs`). Every driver builds at
C++26 with `_GLIBCXX_ASSERTIONS`, so a spec directory sets neither. Every `*_spec.cpp` below the directory, down to the
next directory with its own `CMakeLists.txt`, joins the driver as one ctest test named after its file, and its entry
point is named after the file's stem: `CPPSPEC_SPEC(<stem without _spec>)`, or `DELUGE_SPEC(<stem>, ...)` from
`tests/support/spec_main.h`. The three trees (`tests/trees/host`, `tests/trees/arm`, `tests/trees/ubsan`) find spec
directories by these declarations, so a directory with its own `CMakeLists.txt` needs no other edit. A directory
holding specs needs that `CMakeLists.txt`, or a parent's below `tests/unit`, declaring a `deluge_specs_driver`: a
`*_spec.cpp` whose nearest `CMakeLists.txt` declares none, or that has none, fails the configure, as does a
`CMakeLists.txt` that names no tree. The trees read every `CMakeLists.txt` under `tests/unit`, so each one declares its
trees; a directory with no spec driver (a standalone driver) declares them with
`deluge_test_directory(TREES ...)`. A tree that builds none of a directory's specs gets no driver target from it, so a
call on that target after `deluge_specs_driver` goes inside `if(TARGET <name>)`.
`tests/support/cmake/libs.cmake` adds each `src/` library once per tree; a spec directory only links them.

Support code shared between test directories lives in `tests/support/mocks/` (stand-ins for product code: the display,
file I/O, the model hooks, and shared test utilities such as `tracking_allocator.h`), `tests/support/fixtures/`,
`tests/support/matchers/` (matchers CppSpec lacks) and `tests/support/printers/` (an `operator<<` in a type's own
namespace, so CppSpec prints its values, alone or in a container, rather than its address); code one directory's
specs share lives in that directory's own `support/`. A spec includes a header from another test directory by its path
under `tests/` (`"support/spec_main.h"`, `"unit/ui/gfx/support/golden.h"`, `"harness/ui_script.h"`),
never by `../`. A spec's own directory comes first on its include path, so a file in a per-directory `support/` named
like one in `tests/support/` would shadow it; `scripts/check_test_support_names.py` refuses such a name.

A spec judges a value with the matcher that prints it when it fails: `expect(a).to_equal(b)`, never
`expect(a == b).to_be_true()`, which fails with "expected true" and nothing about `a` or `b`.

| Condition | Matcher |
| --- | --- |
| `a == b`, `a != b` | `expect(a).to_equal(b)`, `expect(a).not_().to_equal(b)` |
| `p == nullptr`, `p != nullptr` | `expect(p).to_be_null()`, `expect(p).not_().to_be_null()` |
| `a < b`, `a > b` | `expect(a).to_be_less_than(b)`, `expect(a).to_be_greater_than(b)` |
| `a <= b`, `a >= b` | `to_be_at_most(expect(a), b)`, `to_be_at_least(expect(a), b)` |
| `lo <= a && a <= hi` | `expect(a).to_be_between(lo, hi)` |
| `abs(a - b) <= t` | `expect(a).to_be_within(t).of(b)` |
| `s.find(x) != npos`, `== npos` | `expect(s).to_contain(x)`, `expect(s).not_().to_contain(x)` |
| `memcmp(a, b, n) == 0` | `to_equal_bytes(expect(std::span<const uint8_t>(a, n)), std::span<const uint8_t>(b, n))` |
| `c1 && c2` | one expectation each, one that reads through a pointer an earlier one checks inside `if` |
| one of several values | `expect(a).to_satisfy([&](auto v) { return v == x \|\| v == y; })` |

`to_be_at_most`, `to_be_at_least` and `to_equal_bytes` are free functions, which argument-dependent lookup finds from
`expect(...)`, in `#include "support/matchers/value_matchers.h"`; the others, `to_contain` among them, are CppSpec's
members. A matcher's last argument is a message naming the subject,
which a failure prints on the line above the matcher's own: `expect(x).to_equal(want, "the row count")`.
Two function pointers compare as addresses through `address_of` (`tests/support/matchers/address_of.h`, with
`using deluge::test::matchers::address_of;`):
`expect(address_of(node.get)).to_equal(address_of(&get_level))`, since `std::ostream` prints one as `1`.

A type CppSpec cannot print shows as its address. A type a spec compares gets an inline `operator<<` in its own
namespace: beside the type when the type is the spec's own, or in a `tests/support/printers/<type>_printer.h` when
specs share it (`optional_printer.h` prints an optional field as its value or `none`). Every spec that compares a
type with a shared printer includes that header: one that does not instantiates CppSpec's printing of the type
without the `operator<<`, the binary then holds two definitions of one template, and the link keeps either.

The `spec-comparisons` pre-commit hook refuses a commit that adds a comparison judged by `to_be_true()`,
`to_be_false()`, `to_be_truthy()` or `to_be_falsy()`; `./dbt test spec-comparisons` checks the whole tree.

### A scenario gate

A scenario gate is a stepped scenario the Embassy host harness runs (`runner = "scenario"`). Adding one:

1. **The scenario:** `integration/scenarios/<name>_scenario.cpp`, declaring its entry points with
   `DELUGE_SCENARIO_DECLARE(<name>)` (`integration/scenarios/support/scenario_declare.h`) and defining `begin`,
   `complete`, `dispatch_step`, `step_done` and `failures`, and a row in `integration/scenarios/scenario_table.cpp`,
   which declares them the same way. Only the host tree compiles that directory, so it needs no `DELUGE_HOST` guard,
   and includes are `"scenarios/…"`.
   Report failures through a `ScenarioReport` (`integration/scenarios/support/scenario_report.h`): `fail()` prints
   `<PREFIX>-FAIL <what>`, `printResult()` the result line, and `failures()` is the row's `failures`. A stage that
   waits for something polls a `Wait` with a tick budget; one that runs out is a recorded failure.
   The row also names the scenario's pacing (`ScenarioPacing`, default virtual time), boot (`ScenarioBoot`, default
   the full boot), drive (`ScenarioDrive`, default interaction-tier steps) and card (`ScenarioCard`, default empty).
   A row takes `NoSettingsPrelude` when the scenario needs the byte channel free from the first tick and reads no
   settings, and `OwnClock` when it drives the app's machines as a BSP task does, one tick per virtual millisecond,
   rather than as interaction-tier entries. golden_vt_render applies all of them and runs any row by name, so nothing
   on the Rust side changes. A card of the scenario's own is a manifest beside it,
   `integration/scenarios/<name>.card`, in the format `src/platform/device/src/storage/card_manifest.rs` documents.
   A row on the song-corpus fixture (`kCorpusFixture`) makes its tests local-only: each entry that runs it must
   require `song-corpus` (a registry unit test checks this, and golden_vt_render refuses the card otherwise).
2. **The registry entry:** `scenario = "<name>"`, `builds = ["golden-vt-render"]`, the `markers` each run must print
   and the `result` line that says it passed (the loader refuses an entry without either). `./dbt test` checks those,
   and the exit status, itself.
3. **A check, only if the gate needs more:** `tests/integration/checks/<name>.py`, named by the entry's `check`,
   defining `check(gate, run)` (after the standard run) or `run(gate)` (instead of it), which judges the entry's
   markers and result itself through `gate.require_standard`, `gate.require_markers` or `gate.shortfall`. Read marker
   lines through `scenario_gate`'s helpers. See `scripts/tasks/scenario_gate.py`.

### A corpus gate

A corpus gate (`runner = "corpus"`) runs one of the per-song runners in `tests/integration/corpus`, `<name>_run.py`,
which the entry names with `corpus_runner = "<name>"`. The runner drives one golden_vt_render run per song through
`tests/integration/corpus/driver.py` and judges each itself (a fixture hash, a stdout baseline, or the driver's own exit
status), so the entry has no `scenario`, `markers`, `result` or `check`: the runner names its scenario and is the check.
What a runner compares with lives under `tests/golden/<consumer>/`. A runner that finds nothing to check exits 77, which
fails the gate. Corpus runners drive driver rows on an empty card; the registry's static corpus-fixture check does not
cover them, and golden_vt_render's run-time refusal does. `scripts/tasks/corpus_gate.py` runs it.

## Re-recording a baseline

A test that compares with something committed -- a fixture hash, a golden snapshot, a render record, a record header --
lists it in its registry entry's `baseline`: files or directories under `tests/golden/<consumer>/`
(`tests/golden/README.md` names each directory and what reads it), or a record header beside the spec it pins: the
loader accepts any `tests/**/*_records.h` by its name. The loader refuses a baseline anywhere else; the one exception is
the website's generated menu tree, which `ui-menu-dump` keeps equal to the app's. One command re-records it:

    ./dbt test --update <name>...                   # e.g. ./dbt test --update golden-synth render-kit-fx
    ./dbt test --update unit -- -R <spec>           # a ctest tree: arguments after -- go to ctest

`--update` takes names only (never `--tier` or `--level`), refuses a test whose entry lists no baseline, always builds
first, and runs the named tests one at a time with `DELUGE_GOLDEN_UPDATE=1` (set for that test alone; one set in your
shell is ignored). Each test runs as usual but writes what it would have compared with. Then the runner

- prints one `GOLDEN-UPDATED <file or file:key> <old> -> <new>` line per item that changed, or `GOLDEN-UPDATED none`;
- fails, and puts every baseline file back as it was (`GOLDEN-UPDATE-REVERTED <path>`), when the run failed (a crash
  or a failed check is not a new baseline), when a file outside the entry's `baseline` changed, or when a baseline
  changed with no line saying so (its `GOLDEN-UPDATED` lines are printed as `GOLDEN-UPDATE-DISCARDED`). What it wrote
  outside `baseline` is put back too: a file that was clean takes its committed content again, a new file is deleted,
  and a file you had already changed or added before the run is left as it is (`GOLDEN-UPDATE-LEFT <path>`). Before a
  file is put back, what the run left in it is copied to a directory of the update's own under the system temp
  directory (`dbt-test-update-strays.*`, kept after the run), named on its line: `GOLDEN-UPDATE-REVERTED <path> (saved:
  <copy>)`. A file that could not be put back is named on a `GOLDEN-UPDATE-UNRESTORED` line and counted in the test's
  outcome. An interrupt (Ctrl-C) puts everything back the same way;
- builds again and runs each test as usual: a new baseline that fails that second run is put back too.

Tests whose `baseline` lists overlap (the render-song entries share `songs.json`) are re-recorded all or nothing: the
shared files are snapshotted once, before the group's first record run, and if any member fails (its record run, its
second run or an interrupt) every file of the group goes back to that snapshot and every member is reported as not
re-recorded. Tests with disjoint baselines are judged one by one.

The dsp record headers shared by both ctest trees (`dx7_workload_records.h`, `hop_scheduler_records.h`,
`pitch_shift_component_records.h`) are recorded by the host's run (`unit`); the ARM tree runs the same specs only to
check them, and `unit-arm` records only the asm pins (`dx7_asm_pin_records.h`). A change to both re-records with
`./dbt test --update unit`, then `./dbt test --update unit-arm`.

**Evidence.** A changed baseline is a finding. Commit it with its `GOLDEN-UPDATED` lines in the commit body and, for
each, why it moved (the change that moved it, or the bug whose fix it records). A line you cannot explain is a
regression: do not commit it.

**Writing an owner.** Python checks and runners use `scripts/tasks/golden_update.py`
(`updating(gate.env)`, `write_bytes`, `write_text`, `write_json`, `update_json_key`, `remove`); C++ specs and scenarios
use `tests/support/golden_update.h`. A record header keeps its generated lines between
`// GOLDEN-RECORDS BEGIN <region>` and `// GOLDEN-RECORDS END <region>` and its spec hands them over with
`golden::record` (`scripts/tasks/golden_records.py` rewrites the region). An owner re-records only the comparison it
owns; every other check of the run still applies. A registry unit test checks that an entry lists a baseline exactly
when its code records one.
