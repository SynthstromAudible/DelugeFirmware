# render_song's cases

What each render gate judged by `render_song.py` covers, beyond its one-sentence `gates` line in
`tests/registry.toml`: one `## <test name>` section per test. `./dbt test --list --verbose <test name>` prints a
section.

Every generated song with an audio track playing sample A (track RSAUD, whose clip saves no `pitchSpeedIndependent`)
time-stretches it: the attribute defaults true, and the clip's six seconds of sample over its eight-second clip is a
ratio of 0.75, in the stretcher's fine band. Thirty-nine render-* gates play such a song (render-audio-cents,
render-audio-track-*, the reverb, mod FX, grain, stutter, bitcrush, EQ and ducking songs among them), so a change to
the time stretcher re-records them too.

## render-song

a song must play as the device plays it: loaded through the real load browser (RS-LOADED, which fails if the load gave
up and left the previous song playing), played in real time for the whole length without stopping (RS-RENDER), with its
sample voices rendered on the audio tier (RS-VOICES), streamed from the card while playing (RS-STREAM), without an
underrun (RS-UNDERRUN) and without the render ever making a synchronous card call (RS-TIER-SYNC-AUDIO). The rendered
output is compared with its record in tests/golden/render/songs.json. The song is generated for each run
(tests/golden/render/songs/make_render_song.py), so the check needs nothing outside the repository. No other gate
renders sample playback in real time on the audio tier.

## render-kit-fx

a kit's effect chain renders as recorded: a generated song (tests/golden/render/songs/make_render_song.py --song
RS_KIT_FX) with every one of its effect settings away from its default (clipping, filter, bitcrush, sample-rate
reduction, EQ, mod FX, delay, compressor, reverb send, pan, volume) beside RS_STREAM's two synths, compared with its
record in tests/golden/render/songs.json; its dry twin with those settings at their defaults must render
differently (RS-FX), so the settings reach the render

## render-kit-filters

a kit's filter chain renders as recorded: a generated song (tests/golden/render/songs/make_render_song.py --song
RS_KIT_FILTERS) with only the filters on (a kit's drive 24 dB LPF and ladder HPF, low-to-high) beside RS_STREAM's two
synths, compared with its record in tests/golden/render/songs.json; its dry twin with the filters at their defaults
must render differently (RS-FX), so the filters reach the render

## render-audio-track-filters

an audio track's filter chain renders as recorded: a generated song (tests/golden/render/songs/make_render_song.py
--song RS_AUDIO_FILTERS) with only the filters on (an audio track's SVF notch LPF and SVF band HPF, in parallel) beside
RS_STREAM's two synths, compared with its record in tests/golden/render/songs.json; its dry twin with the filters
at their defaults must render differently (RS-FX), so the filters reach the render

## render-master-filters

the song master's filter chain renders as recorded: a generated song (tests/golden/render/songs/make_render_song.py
--song RS_MASTER_FILTERS) with only the filters on (the song master's 12 dB ladder LPF and SVF band HPF, high-to-low)
beside RS_STREAM's two synths, compared with its record in tests/golden/render/songs.json; its dry twin with the
filters at their defaults must render differently (RS-FX), so the filters reach the render

## render-audio-cents

an audio clip's fine tune renders as recorded, and differs from the clip untuned: a generated song
(tests/golden/render/songs/make_render_song.py --song RS_AUDIO_CENTS) whose audio clip plays 30 cents up, its effects at
their defaults, beside RS_STREAM's two synths, compared with its record in tests/golden/render/songs.json; its dry twin
plays the clip untuned and must render differently (RS-FX), so the clip's cents reach the render

## render-audio-stretch

audio clips stretched to the song's tempo render as recorded, and differ from the same clips resampled: a generated
song (tests/golden/render/songs/make_render_song.py --song RS_AUDIO_STRETCH) of ten audio clips alone at 120 BPM, pitch
and speed independent, each at its own ratio to the tempo, so the time stretcher hops through each in step with the
song's ticks: the sample goldens' tone and percussive source sped up and slowed within an octave, past one octave and
past two (where the coarse hop tables clamp), reversed with a pre-margin after its end marker, and transposed two
semitones, which reads a cache; and sample A within a hair of its native tempo, left unstretched until it drifts out
of sync, then stretched until it is back. Compared with its record in tests/golden/render/songs.json; its dry twin
plays the same clips with pitch and speed tied, so nine resample rather than stretch (the transposed clip still
stretches, as a tied clip with a transpose does), and must render differently (RS-FX), so the stretch, not just the
tempo, reaches the render

## render-audio-stretch-native

audio clips at their native tempo render as recorded, a fudged stretch crossfading their loops: a generated song
(make_render_song.py --song RS_AUDIO_STRETCH_NATIVE) of the tone and the percussive source at their native tempo (250
samples a tick), from start markers with frames before them. Neither is stretched, but at a loop end that a render
block starts within reach of (88 of about 110 for the tone) a time stretcher is fudged in to crossfade into those
frames, the pre-margin, without a click. Compared with its record in
songs.json; its dry twin plays the same frames from files that hold only them, with no pre-margin and so no fudged
crossfade, and must render differently (RS-FX)

## render-audio-stretch-arranged

stretched audio clips render as recorded from arrangement instances that end without looping: a generated song
(make_render_song.py --song RS_AUDIO_STRETCH_ARRANGED) in the arranger, its stretched clips played from instances that
end part-way through a loop or after a single pass, so the clip does not loop at its end; the forward tone and the
reversed tone have waveform past the end they play to, so each fades out before its instance ends. Compared with its
record in songs.json; its dry twin plays the clips resampled and must render differently (RS-FX)

## render-audio-stretch-arm

audio clips stretched to the song's tempo render on 32-bit ARM as recorded on x86: render-audio-stretch's song and
record, run by the ARM build under qemu, where the stretcher's synced path does its double and 64-bit divides

## render-audio-track-fx

an audio track's effect chain renders as recorded: a generated song (tests/golden/render/songs/make_render_song.py
--song RS_AUDIO_FX) with every one of its effect settings away from its default (clipping, filter, bitcrush, sample-rate
reduction, EQ, mod FX, delay, compressor, reverb send, pan, volume) beside RS_STREAM's two synths, compared with its
record in tests/golden/render/songs.json; its dry twin with those settings at their defaults must render
differently (RS-FX), so the settings reach the render

## render-master-fx

the song master's effect chain renders as recorded: a generated song (tests/golden/render/songs/make_render_song.py
--song RS_MASTER_FX) with every one of its effect settings away from its default (clipping, filter, bitcrush,
sample-rate reduction, EQ, mod FX, delay, compressor, reverb send, pan, volume) beside RS_STREAM's two synths, compared
with its record in tests/golden/render/songs.json; its dry twin with those settings at their defaults must render
differently (RS-FX), so the settings reach the render

## render-master-pan-stereo

the song master's pan renders as recorded in stereo, where it applies: render-master-fx's song under its own name
(tests/golden/render/songs/make_render_song.py --song RS_MASTER_PAN), its pan away from the centre, rendered in
stereo (RENDER_STEREO=1), so the master's post-reverb volume is panned; compared with its record in
tests/golden/render/songs.json; its dry twin must render differently (RS-FX)

## render-master-arranger

the song master's automation plays in the arranger: a generated song (tests/golden/render/songs/make_render_song.py
--song RS_MASTER_ARRANGER) opened in the arranger, RS_STREAM's two synths each playing its clip from an instance at the
arrangement's start, the master's effects as render-master-fx's with its low-pass cutoff and its volume ramping over the
first two bars; the loaded song must have automation to play on its own params (RS-SONGPARAMS, RENDER_SONG_AUTOMATED=1),
it is compared with its record in tests/golden/render/songs.json, and its dry twin must render differently (RS-FX)

## render-master-grain-off

the song master's granular mod FX in its soft off renders as recorded: a generated song
(tests/golden/render/songs/make_render_song.py --song RS_MASTER_GRAIN_OFF) whose master holds GRAIN with its
selected knob (depth) at its minimum, so every block takes the grain's ring up afresh and renders no grain, its other
effects as render-master-fx's; compared with its record in tests/golden/render/songs.json; its dry twin must render
differently (RS-FX)

## render-kit-svf-morph

a kit's state-variable filters render as recorded with their morphs up: a generated song
(tests/golden/render/songs/make_render_song.py --song RS_KIT_SVF_MORPH) with only the filters on (a kit's SVF band
LPF morphed past its midpoint and SVF notch HPF morphed, low-to-high) beside RS_STREAM's two synths, compared with its
record in tests/golden/render/songs.json; its dry twin with the filters at their defaults must render differently
(RS-FX), so the filters reach the render

## render-audio-track-drive

an audio track's oversampled drive filter, saturating ladder HPF and granular mod FX render as recorded: a generated
song (tests/golden/render/songs/make_render_song.py --song RS_AUDIO_DRIVE) with an audio track's drive LPF high and
resonant enough to oversample, its ladder HPF resonant enough to saturate and morphed, low-to-high, and its mod FX the
granular one, beside RS_STREAM's two synths, compared with its record in tests/golden/render/songs.json; its dry
twin with those settings at their defaults must render differently (RS-FX), so they reach the render

## render-kit-filter-fade

a kit's filters fade in as recorded when switched on: a generated song
(tests/golden/render/songs/make_render_song.py --song RS_KIT_FILTER_FADE) whose kit clip automates its 24 dB ladder
LPF on from fully open at the third beat and its SVF band HPF on from fully closed at the fourth, in parallel, beside
RS_STREAM's two synths, compared with its record in tests/golden/render/songs.json; its dry twin with the filters
at their defaults must render differently (RS-FX), so the filters reach the render

## render-audio-track-filter-fade

an audio track's filters fade in as recorded when switched on: a generated song
(tests/golden/render/songs/make_render_song.py --song RS_AUDIO_FILTER_FADE) whose audio clip automates its SVF
notch LPF on from fully open at the third beat and its ladder HPF on from fully closed at the fourth, high-to-low,
beside RS_STREAM's two synths, compared with its record in tests/golden/render/songs.json; its dry twin with the
filters at their defaults must render differently (RS-FX), so the filters reach the render

## render-audio-ducked

sidechain hits duck as recorded: a generated song (tests/golden/render/songs/make_render_song.py --song
RS_AUDIO_DUCKED) whose kit drums send sidechain hits, a full-strength one and then a weaker one inside its duck, which
is ignored; they duck an audio track through its synced sidechain volume, the song's reverb through its sidechain volume
set by hand, and a third drum through its own sidechain cable, which takes each hit up late, as it wakes from skipping
rendering, in that hit's attack, its release and after it; beside RS_STREAM's two synths, compared with its record in
tests/golden/render/songs.json; its dry twin without the hits, the audio track's sidechain volume or the reverb's
must render differently (RS-FX), so the hits reach the render

## render-duck-shapes

the sidechain envelope's instant attack, straight and clamped release shapes, and a stronger hit taken mid-attack and
mid-release render as recorded on the host owners: a generated song (tests/golden/render/songs/make_render_song.py
--song RS_DUCK_SHAPES) whose kit drums send a quarter-strength sidechain hit and then a full-strength one inside its
duck, twice; they duck an audio track whose sidechain attack is at its minimum, so each hit skips the attack and the
stronger one lands mid-release, the kit through its own sidechain with the shape param at its maximum, where the
release's curve is clamped, and the stronger hit mid-release, and the song's reverb through its sidechain with the shape
param at its minimum, a straight release, and an attack slow enough that the stronger hit lands mid-attack; beside
RS_STREAM's two synths, compared with its record in tests/golden/render/songs.json; its dry twin without the hits
or the three sidechain volumes must render differently (RS-FX), so the hits reach the render

## render-kit-comp

a kit's compressors render as recorded: a generated song (tests/golden/render/songs/make_render_song.py --song
RS_KIT_COMP) whose kit drum runs its own compressor and whose kit runs one blended half wet with its sidechain high-pass
up, beside RS_STREAM's two synths, compared with its record in tests/golden/render/songs.json; its dry twin with
both thresholds at zero must render differently (RS-FX), so the compressors reach the render

## render-kit-delay

a kit's delay renders as recorded through rate changes and its end: a generated song
(tests/golden/render/songs/make_render_song.py --song RS_KIT_DELAY) whose kit's digital ping-pong delay (rendered
in mono, as plain feedback) has its rate stepped by automation past double its buffer's rate, a little down, and below
half, each change resampling and growing a new buffer swapped in once written, and whose drum stops long enough before
the clip loops for the delay to abandon its buffers; beside RS_STREAM's two synths, compared with its record in
tests/golden/render/songs.json; its dry twin with the delay's feedback at its minimum must render differently
(RS-FX), so the delay's settings reach the render

## render-delay-owners

a delay renders as recorded on each owner the kit and audio-track delay gates leave out: a generated song
(tests/golden/render/songs/make_render_song.py --song RS_DELAY_OWNERS) with a digital delay on a synth and an
analog delay on a kit drum's own sound, each holding a note while its rate is stepped by automation past double its
buffer's rate, a little down, and below half, so each starts, grows and swaps in new buffers and abandons them before
the clip loops, and a digital delay on the song's master (whose automation plays only in the arranger, so its rate
holds); beside RS_STREAM's two synths, compared with its record in tests/golden/render/songs.json; its dry twin
with every delay's feedback at its minimum must render differently (RS-FX), so the delays' settings reach the render

## render-stutter-audio

an audio track's stutter renders as recorded: a generated song (tests/golden/render/songs/make_render_song.py
--song RS_STUTTER_AUDIO) whose audio track, with its own stutter settings (free, forward), is stuttered from block 100
to 600 (RENDER_STUTTER) as the gold knob's press and release do, so it records a tape at its stutter rate and then plays
it back; beside RS_STREAM's two synths, compared with its record in tests/golden/render/songs.json; the scenario's
RS-STUTTER summary must show the begin and the end made and ticks stuttered, and the song rendered without the stutter
must render differently (RS-SWITCH)

## render-stutter-synth

a synth's stutter renders as recorded: a generated song (tests/golden/render/songs/make_render_song.py --song
RS_STUTTER_SYNTH, render-stutter-audio's song) whose first synth's Sound, with the song's stutter settings (quantized),
is stuttered from block 100 to 600 (RENDER_STUTTER) as the gold knob's press and release do on a synth, its stutter rate
snapped to a note length at the begin and restored at the end; compared with its record in
tests/golden/render/songs.json; the RS-STUTTER summary must show the begin and end made and ticks stuttered, and
the unstuttered song must render differently (RS-SWITCH)

## render-stutter-kit

a kit's quantized, reversed stutter renders as recorded: a generated song
(tests/golden/render/songs/make_render_song.py --song RS_STUTTER_KIT) whose kit, with its own stutter settings
(quantized, reversed), is stuttered from block 150 to 700 (RENDER_STUTTER), its stutter rate snapped to a note length at
the begin and restored at the end; beside RS_STREAM's two synths, compared with its record in
tests/golden/render/songs.json; the RS-STUTTER summary must show the begin and end made and ticks stuttered, and
the unstuttered song must render differently (RS-SWITCH)

## render-stutter-master

the song master's ping-pong stutter renders as recorded: a generated song
(tests/golden/render/songs/make_render_song.py --song RS_STUTTER_MASTER) whose master, its stutter ping-pong, is
stuttered from block 100 to 900 (RENDER_STUTTER), so the played tape turns at each end; beside RS_STREAM's two synths
and an audio track, compared with its record in tests/golden/render/songs.json; the RS-STUTTER summary must show
the begin and end made and ticks stuttered, and the unstuttered song must render differently (RS-SWITCH)

## render-stutter-rate

a stutter whose rate knob is turned while it plays renders as recorded: a generated song
(tests/golden/render/songs/make_render_song.py --song RS_STUTTER_RATE, render-stutter-audio's song) stuttered from
block 100 to 900 (RENDER_STUTTER), its stutter-rate param set by the gold knob's SetParamValue Edit at block 400
(RENDER_STUTTER_RATE), so the recorded tape plays resampled; compared with its record in
tests/golden/render/songs.json; the RS-STUTTER summary must show the begin, the end and the rate change made, and
the unstuttered song must render differently (RS-SWITCH)

## render-stutter-handoff

a stutter handed from one owner to another renders as recorded: a generated song
(tests/golden/render/songs/make_render_song.py --song RS_STUTTER_HANDOFF) whose audio track is stuttered from block
100 and whose kit takes the stutter over at block 400 (RENDER_STUTTER: the track's ended, then the kit's begun, in the
same block, as the performance view hands a stutter over), ending at 800; beside RS_STREAM's two synths, compared with
its record in tests/golden/render/songs.json; the RS-STUTTER summary must show every begin and end made and ticks
stuttered, and the unstuttered song must render differently (RS-SWITCH)

## render-tempo-delays

five synced delays resizing together after a tempo change render as recorded: a generated song
(tests/golden/render/songs/make_render_song.py --song RS_TEMPO_DELAYS) whose kit's five drums each ring a digital
delay synced to a half note, the song's tempo set to 70 bpm at block 300 (RENDER_TEMPO) while all five ring, so every
delay grows a new, longer tape from the same block; the delays' arena, its reserves following the tempo, must have room
for every one (RS-DELAY-ARENA shortfalls=0), so each takes its tape on the block it asks to, as the general heap gave
it; beside RS_STREAM's two synths, compared with its record in tests/golden/render/songs.json; the same song
rendered without the tempo change must render differently (RS-SWITCH)

## render-audio-track-delay

an audio track's digital delay renders as recorded through rate changes: a generated song
(tests/golden/render/songs/make_render_song.py --song RS_AUDIO_DELAY) whose audio track's digital delay has its
rate stepped by automation past double its buffer's rate, a little down, and below half, each change resampling and
growing a new buffer swapped in once written; an audio clip renders until it loops, so this delay never abandons its
buffers (render-kit-delay covers that); beside RS_STREAM's two synths, compared with its record in
tests/golden/render/songs.json; its dry twin with the delay's feedback at its minimum must render differently
(RS-FX), so the delay's settings reach the render

## render-kit-pingpong-stereo

a kit's ping-pong delay renders as recorded in stereo, where its echoes cross channels: a generated song
(tests/golden/render/songs/make_render_song.py --song RS_KIT_PINGPONG) rendered with headphones plugged in
(RENDER_STEREO=1; RS-CAPTURE fails a run in the other width), beside RS_STREAM's two synths, compared with its record in
tests/golden/render/songs.json; its dry twin with the delay's feedback at its minimum, also in stereo, must render
differently (RS-FX), so the delay's settings reach the render

## render-audio-track-delay-toggle

an analog delay switched off and on again while it rings renders as recorded: a generated song
(tests/golden/render/songs/make_render_song.py --song RS_AUDIO_DELAY_TOGGLE) whose audio track's analog delay (its
tanh at a host chain's drive) has its feedback dropped to its minimum by automation, which abandons its buffers at once,
and raised again a beat later while the audio still plays, so the delay starts again with its impulse response's history
from the block before it went off; beside RS_STREAM's two synths, compared with its record in
tests/golden/render/songs.json; its twin with the feedback held throughout must render differently (RS-FX), so the
switch reaches the render

## render-audio-track-delay-switch

an analog delay switched to digital and back to analog while it rings renders as recorded: a generated song
(tests/golden/render/songs/make_render_song.py --song RS_AUDIO_DELAY_SWITCH) whose audio track's analog delay rings
with its feedback held, switched by the Edit the gold knob's press on the delay makes (RENDER_DELAY_SWITCH,
RS-DELAY-SWITCH) at block 600 and again at block 1000, the same delay each time, so its digital blocks leave the impulse
response's history as the last analog block did and the analog blocks after resume from it; beside RS_STREAM's two
synths, compared with its record in tests/golden/render/songs.json; the same song rendered unswitched must render
differently (RS-SWITCH), so the switches reach the render

## render-pingpong-stereo-sweep

ping-pong delays render as recorded in stereo, crossing channels while their rates step through new buffers: a generated
song (tests/golden/render/songs/make_render_song.py --song RS_PINGPONG_SWEEP) whose kit's digital ping-pong delay
and audio track's analog one each have their rate stepped by automation past double their buffer's rate, a little down,
and below half, so each crosses at its native rate, resampling below and above it, and on a new delay's first blocks
while its first buffer grows; the kit's drum stops so its delay is abandoned. Rendered with headphones plugged in
(RENDER_STEREO=1; RS-CAPTURE fails a run in the other width), beside RS_STREAM's two synths, compared with its record in
tests/golden/render/songs.json; its dry twin with both delays' feedback at its minimum, also in stereo, must render
differently (RS-FX), so the delays' settings reach the render

## render-audio-track-delay-extremes

a delay renders as recorded at both ends of its rate's range: a generated song
(tests/golden/render/songs/make_render_song.py --song RS_AUDIO_DELAY_EXTREMES) whose audio track's digital delay
has its rate held at its minimum, which gives it a two-second buffer, then stepped by automation to its maximum, where
that buffer resamples so fast its 8.24 spin wraps (about 177 steps per sample) until a 128-sample buffer is swapped in,
then back to its minimum, where the small buffer crawls at the slowest a tape resamples while a new two-second one
grows; beside RS_STREAM's two synths, compared with its record in tests/golden/render/songs.json; its dry twin with
the delay's feedback at its minimum must render differently (RS-FX), so the delay's settings reach the render. The
record is the delay line's frame order: at the maximum rate the 128-sample buffer's delay is shorter than a 128-frame
block, where a block that read every frame before writing any dropped the echoes that came round within it
(delay_echo_spec and delay_frame_order_spec pin it), and with 32-frame blocks, where no block laps, both orders render
the song bit for bit alike

## render-low-clip

a host chain's saturation below amount 3, where it does not shift back up, renders as recorded: a generated song
(tests/golden/render/songs/make_render_song.py --song RS_LOW_CLIP) whose kit saturates at amount 1 and whose audio
track at amount 2, every other host setting at its default, beside RS_STREAM's two synths, compared with its record in
tests/golden/render/songs.json; its dry twin with both amounts at 0 must render differently (RS-FX), so the
saturation reaches the render

## render-bitcrush

the bitcrush paths the FX songs leave out render as recorded: a generated song
(tests/golden/render/songs/make_render_song.py --song RS_BITCRUSH) whose kit crushes its bit depth alone at preset
6, its mask and a post-FX volume shifted down by 2, whose audio track's sample-rate reduction is stepped off at the
second bar and on again at the third, so it restarts from a zeroed grab, and whose master crushes both at preset 7, a
volume shift of 3; beside RS_STREAM's two synths, compared with its record in tests/golden/render/songs.json; its
dry twin with every bitcrush and sample-rate reduction off must render differently (RS-FX), so the settings reach the
render

## render-eq-bands

the EQ's single bands render as recorded: a generated song (tests/golden/render/songs/make_render_song.py --song
RS_EQ_BANDS) whose kit boosts its bass alone and whose audio track cuts its treble alone, the treble stepped flat at the
second bar and back at the third, so the band switches off and on while its memory holds; beside RS_STREAM's two synths,
compared with its record in tests/golden/render/songs.json; its dry twin with both EQs flat must render differently
(RS-FX), so the bands reach the render

## render-reverb-duck-override

the reverb's duck taken from a synth with the most reverb renders as recorded: a generated song
(tests/golden/render/songs/make_render_song.py --song RS_REVERB_DUCK_OVERRIDE) whose reverb sidechain is on auto,
so the reverb ducks through the sidechain cable to post-reverb volume of a synth with more reverb than the song and its
audio track, kit hits ducking it in every bar; beside RS_STREAM's two synths, compared with its record in
tests/golden/render/songs.json; its twin with the reverb's sidechain set by hand to nothing must render differently
(RS-FX), so the override reaches the render

## render-reverb-duck-override-track

the reverb's duck taken from an audio track with the most reverb renders as recorded: a generated song
(tests/golden/render/songs/make_render_song.py --song RS_REVERB_DUCK_OVERRIDE_TRACK) whose reverb sidechain is on
auto, so the reverb ducks through the sidechain volume, shape and rates of an audio track with more reverb than the
song, kit hits ducking it in every bar; beside RS_STREAM's two synths, compared with its record in
tests/golden/render/songs.json; its twin with the reverb's sidechain set by hand to nothing must render differently
(RS-FX), so the override reaches the render

## render-metronome

the metronome's click renders as recorded: a generated song (tests/golden/render/songs/make_render_song.py --song
RS_METRONOME, RS_STREAM under its own name) whose metronome is switched on at block 200 (RENDER_METRONOME, as its button
does through PlaybackHandler::toggleMetronomeStatus), so each beat and bar adds its square click at the metronome's
volume scaled by the song's; compared with its record in tests/golden/render/songs.json; the scenario's
RS-METRONOME summary must show the switch applied and the metronome on; the same song rendered without the switch must
render differently (RS-SWITCH), so the click reaches the render

## render-audio-track-comp-onset

a compressor switched on while its audio sounds renders as recorded: a generated song
(tests/golden/render/songs/make_render_song.py --song RS_AUDIO_COMP_ONSET) whose audio track's compressor threshold
holds at its minimum, where the compressor is off and reset every block, then steps up by automation at the second bar
while the audio plays, so the compressor's first block seeds its saturators from a sounding frame (every other
compressor golden seeds from silence); beside RS_STREAM's two synths, compared with its record in
tests/golden/render/songs.json; its dry twin with the threshold held at its minimum must render differently
(RS-FX), so the compressor reaches the render

## render-audio-track-comp-reset

a compressor reset mid-sound renders as recorded when it comes back on: a generated song
(tests/golden/render/songs/make_render_song.py --song RS_AUDIO_COMP_RESET) whose audio track's compressor
threshold, above its minimum from the start, drops to the minimum at the second bar, where the compressor is off and
reset every block while its level and detector carry over, and steps back up at the third while the audio plays, so the
compressor renders again after a reset; beside RS_STREAM's two synths, compared with its record in
tests/golden/render/songs.json; its dry twin with the threshold held at its minimum must render differently
(RS-FX), so the compressor reaches the render

## render-audio-track-comp-blend

a blended compressor on a loud audio track renders as recorded, so its 64-bit level sum is pinned: a generated song
(tests/golden/render/songs/make_render_song.py --song RS_AUDIO_COMP_BLEND) whose audio track plays a sample
normalised near full scale (SAMPLES/RS/LOUD.WAV) at the clip's full volume, its EQ's bass and treble at their maximum
boost (the volume alone is not loud enough), through its compressor blended half wet with the threshold above its
minimum, so the dry half carries the loud input into the level the compressor measures, loud enough that a q31 sum of a
block's squares would pass 2^31, which the 64-bit sum holds; beside RS_STREAM's two synths, compared with its record in
tests/golden/render/songs.json; its dry twin with the threshold at its minimum must render differently (RS-FX), so
the blended compressor reaches the render

## render-modfx-mono

the warble and dimension mod FX, which no synth golden plays, render as recorded in mono: a generated song
(tests/golden/render/songs/make_render_song.py --song RS_MODFX_MONO) whose kit's chain runs the warble, with its
second, random LFO, and whose audio track's chain runs the dimension, each with its knob on depth and its rate, depth,
feedback and offset away from their defaults, beside RS_STREAM's two synths, compared with its record in
tests/golden/render/songs.json; its dry twin with both mod FX off must render differently (RS-FX), so the mod FX
reach the render

## render-modfx-stereo-a

the flanger, chorus and phaser mod FX render as recorded in stereo, where the comb types read a second tap for the right
channel: a generated song (tests/golden/render/songs/make_render_song.py --song RS_MODFX_STEREO_A) whose kit's
chain runs the flanger, its audio track's the chorus and the song's master the phaser, each with its knob on depth and
its rate, depth, feedback and offset away from their defaults, rendered with headphones plugged in (RENDER_STEREO=1;
RS-CAPTURE fails a run in the other width), beside RS_STREAM's two synths, compared with its record in
tests/golden/render/songs.json; its dry twin with every mod FX off, also in stereo, must render differently
(RS-FX), so the mod FX reach the render

## render-modfx-stereo-b

the stereo chorus, warble and dimension mod FX render as recorded in stereo: a generated song
(tests/golden/render/songs/make_render_song.py --song RS_MODFX_STEREO_B) whose kit's chain runs the stereo chorus,
its audio track's the warble, with its second, random LFO, and the song's master the dimension, each with its knob on
depth and its rate, depth, feedback and offset away from their defaults, rendered with headphones plugged in
(RENDER_STEREO=1; RS-CAPTURE fails a run in the other width), beside RS_STREAM's two synths, compared with its record in
tests/golden/render/songs.json; its dry twin with every mod FX off, also in stereo, must render differently
(RS-FX), so the mod FX reach the render

## render-modfx-soft-off

a mod FX switched off and on again by its knob renders as recorded: a generated song
(tests/golden/render/songs/make_render_song.py --song RS_MODFX_SOFT_OFF) whose audio track's flanger and kit's
phaser each have their selected knob, the depth, taken by automation to its minimum at the second bar, which switches
the effect off (the flanger's delay line freed, the phaser's allpass memory kept), and back at the third while the audio
plays, so the flanger starts again on a zeroed delay line and the phaser on its carried memory, its LFO's phase carried;
beside RS_STREAM's two synths, compared with its record in tests/golden/render/songs.json; its dry twin with both
mod FX off must render differently (RS-FX), so the mod FX reach the render

## render-modfx-switch

a mod FX switched to another type and back while it plays renders as recorded, its LFO re-seating each time its wave
changes after it has rendered: a generated song (tests/golden/render/songs/make_render_song.py --song
RS_MODFX_SWITCH) whose audio track's warble is switched to the chorus at block 600 and back to the warble at block 1000
(RENDER_MODFX_SWITCH) by the Edit the mod FX type menu makes, so its main LFO re-seats from the warbler to the sine and
back to a fresh warbler while its second, random LFO, which renders only for the warble, keeps its state, and its delay
line carries across both switches; beside RS_STREAM's two synths, compared with its record in
tests/golden/render/songs.json; the scenario's RS-MODFX-SWITCH summary must show both switches applied, setting the
chorus then the warble, each leaving the track holding its ring; the same song rendered unswitched must render
differently (RS-SWITCH), so the switches reach the render

## render-modfx-clear

a mod FX's memory cleared while it plays renders as recorded, both with a delay line and without one: a generated song
(tests/golden/render/songs/make_render_song.py --song RS_MODFX_CLEAR) whose audio track's flanger has its memory
cleared at block 1000 (RENDER_MODFX_CLEAR: model::clearModFXMemory, as a sound cutting its tail calls it,
which for a comb type is effects::ModFx::clearMemory), which zeroes its delay line, then is switched to the phaser at
block 1400 (RENDER_MODFX_SWITCH), which frees the delay line, and cleared again at block 1800, which zeroes the phaser's
allpass memory; beside RS_STREAM's two synths, compared with its record in tests/golden/render/songs.json; the
scenario's RS-MODFX-SWITCH summary must show the switch applied, leaving the track holding no ring, and its
RS-MODFX-CLEAR summary both clears applied, one zeroing a ring and one the phaser's memories; the same song rendered
without its clears and switch must render differently (RS-SWITCH), so they reach the render

## render-grain-mono

the granular mod FX renders as recorded in mono on a host owner's chain: a generated song
(tests/golden/render/songs/make_render_song.py --song RS_GRAIN_MONO) whose kit's chain and the song's master each
run the grain, its knob on depth (its mix) and its rate, density and pitch randomness away from their defaults, the
kit's pitch randomness at its maximum so its grains start in every pitch class, the kit's chain also rendering its tail
between hits; beside RS_STREAM's two synths, compared with its record in tests/golden/render/songs.json; its dry
twin with both grains off must render differently (RS-FX), so the grains reach the render

## render-grain-stereo

the granular mod FX renders as recorded in stereo, where each grain is panned: a generated song
(tests/golden/render/songs/make_render_song.py --song RS_GRAIN_STEREO) whose kit's chain, audio track's chain and
the song's master each run the grain, its knob on depth (its mix) and its rate, density and pitch randomness away from
their defaults, the kit's and the master's pitch randomness at its maximum, rendered with headphones plugged in
(RENDER_STEREO=1; RS-CAPTURE fails a run in the other width), beside RS_STREAM's two synths, compared with its record in
tests/golden/render/songs.json; its dry twin with every grain off, also in stereo, must render differently (RS-FX),
so the grains reach the render

## render-grain-switch

the granular mod FX switched on and off by its type, and its memory cleared, while it plays renders as recorded: a
generated song (tests/golden/render/songs/make_render_song.py --song RS_GRAIN_SWITCH) whose audio track's chorus is
switched to the grain at block 600 (RENDER_MODFX_SWITCH, the Edit the mod FX type menu makes), so the chain takes its
granular ring and starts the grain on its next sounding block; the grain is cleared at block 1300 (RENDER_MODFX_CLEAR:
model::clearModFXMemory, which for the grain is effects::Granular::clear), which stops its grains and
starts its ring and its first pass over, and switched back to the chorus at block 1800, which releases its ring; beside
RS_STREAM's two synths, compared with its record in tests/golden/render/songs.json; the scenario's RS-MODFX-SWITCH
summary must show both switches applied, the grain holding no mod FX ring and the chorus one, and its RS-MODFX-CLEAR
summary the one clear applied to a grain; the same song rendered without its switches and clear must render differently
(RS-SWITCH), so they reach the render

## render-grain-master

the song's master chain entering and leaving the granular mod FX by its type while it plays renders as recorded: a
generated song (tests/golden/render/songs/make_render_song.py --song RS_GRAIN_MASTER) whose master runs the chorus,
switched to the grain at block 600 and back to the chorus at block 1800 (RENDER_MODFX_SWITCH with the owner `master`,
the Edit the mod FX type menu makes), so the master takes a granular ring and starts the grain on its next sounding
block, then releases the ring; beside RS_STREAM's two synths, compared with its record in
tests/golden/render/songs.json; each owner's own RS-MODFX-SWITCH marker must show what it holds after its switch,
the grain a granular ring and no mod FX ring and the chorus a mod FX ring and no granular one; the same song rendered
without its switches must render differently (RS-SWITCH), so they reach the render

## render-reverb-freeverb

the song's Freeverb reverb renders as recorded at knobs of its own, its width at half, so each channel's output takes in
the other's (wet2 above 0): a generated song (tests/golden/render/songs/make_render_song.py --song
RS_REVERB_FREEVERB) whose audio track sends to it beside RS_STREAM's two synths, compared with its record in
tests/golden/render/songs.json; its dry twin at the boot song's knobs must render differently (RS-FX)

## render-reverb-mutable

the song's Mutable reverb renders as recorded at knobs of its own, its room, damping, width, high-pass and low-pass away
from the boot song's: a generated song (tests/golden/render/songs/make_render_song.py --song RS_REVERB_MUTABLE)
whose audio track sends to it beside RS_STREAM's two synths, compared with its record in
tests/golden/render/songs.json; its dry twin at the boot song's knobs must render differently (RS-FX)

## render-reverb-digital

the song's Digital reverb renders as recorded at knobs of its own, panned halfway left and rendered in stereo, so the
two channels' return levels differ: a generated song (tests/golden/render/songs/make_render_song.py --song
RS_REVERB_DIGITAL) whose audio track sends to it beside RS_STREAM's two synths, compared with its record in
tests/golden/render/songs.json; its dry twin at the boot song's knobs must render differently (RS-FX)

## render-reverb-switch

the song's reverb switched between its models and its knobs turned while it plays renders as recorded, each model change
rebuilding the reverb with its memory zeroed as a song load does: a generated song
(tests/golden/render/songs/make_render_song.py --song RS_REVERB_SWITCH) playing Mutable, switched to Digital at
block 600, its high-pass turned at 800, switched to Freeverb at 1000 (the song's width at half, so wet2 above 0), its
width halved again at 1200 and its room turned at 1400, back to Mutable at 1600 and its low-pass turned at 1800
(RENDER_REVERB_SWITCH, as the reverb menus do); compared with its record in tests/golden/render/songs.json; the
RS-REVERB-SWITCH summary must show every switch applied with the model asked for, and the same song rendered unswitched
must render differently (RS-SWITCH)

## render-grain-restart

the granular mod FX started again renders as recorded, its grains reading 0 from every slot not yet written since the
restart: a generated song (tests/golden/render/songs/make_render_song.py --song RS_GRAIN_RESTART) whose kit's chain
falls silent until its grain decays and restarts, whose audio track's grain has its depth taken to its minimum at the
second bar (a soft off, which restarts it) and back at the third, and whose synth's one note stops long enough for the
sound to skip rendering, which restarts it; the clip's loop at 8 s and the depth's return start each grain again with
its first pass done, so grains start ahead of the write index and read slots not yet written since the restart, which
the kernel's high-water mask reads as 0 (as the record was made); rendered for 4,000 blocks so the grains run after each
restart; beside RS_STREAM's two synths, compared with its record in tests/golden/render/songs.json; its dry twin
with every grain off must render differently (RS-FX), so the grains reach the render

## render-grain-owners

the granular mod FX switched by a synth's own type setter, and cleared through each owner's own clear, while it plays
renders as recorded: a generated song (tests/golden/render/songs/make_render_song.py --song RS_GRAIN_OWNERS) whose
synth holds a note through the grain, beside a kit and the song's master each running the grain; the synth's type is
switched to the chorus at block 500 (RENDER_MODFX_SWITCH, the Edit the mod FX type menu makes, reaching the projection
of the type into the render, which stops the grain and releases its ring) and back to the grain at block 1200 (which
takes a ring again and clears the grain's State), and the synth's, the kit's and the master's grains are cleared at
blocks 1700, 1800 and 1900 (RENDER_MODFX_CLEAR: model::clearModFXMemory, as a sound cutting its tail
calls it); beside RS_STREAM's two synths, compared with its record in tests/golden/render/songs.json; the
scenario's RS-MODFX-SWITCH summary must show both switches applied, the chorus holding a ring and the grain none, and
its RS-MODFX-CLEAR summary the three clears applied to grains; the same song rendered without its switches and clears
must render differently (RS-SWITCH), so they reach the render

## render-song-corpus

the render-song checks, on a real song from the maintainer's card backup (Cordae: eight kits of sampled drums, played in
the arranger). The committed render-song song is two synths; this is the one that exercises kits, the arrangement and a
real song's sample set, and it is what the streaming-margin work is measured against. It runs nightly, so a change that
moves its render is seen beside the render-song re-records; it needs the backup.
