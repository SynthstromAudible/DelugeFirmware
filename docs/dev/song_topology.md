# Song topology — the north star (tracks, chains, kits, clips, learn)

Status: **north-star / end-state (living doc, 2026-10-16).** How a song's parts *should* relate: what a track,
an instrument, a kit, a clip and a learned control are, and which owns what. Decided with Kate from first
principles. It sits beside `project_model_north_star.md` (the model is the truth, the engine plays it) and the
domain-split program (engines are CLAP-shaped components); this doc is the shape of the model those two describe.
Individual plans are checked against it; it does not specify them.

---

## The thesis

> **Separate what makes the sound, what plays it, and who controls it.** A *track* owns a chain of devices that
> makes sound; a *clip* owns the notes and automation that play it, plus the values it overrides; one *mapping
> table* owns every learned control.

---

## Why: what today's objects conflate

- **`Output` is both track and instrument.** One object carries the track's identity (name, mixer, MIDI input)
  and the instrument's (preset, engine state, MIDI channel).
- **A clip carries instrument state.** Beside its notes it holds a full set of the instrument's param values,
  the arp's settings and the MIDI program.
- **A kit is a special output** whose rows are `Drum`s: each half an instrument (its sound, its arp) and half a
  lane.
- **The arp has three homes:** the clip (melodic tracks), the drum (kit rows) and the kit clip (whole-kit arp).
- **MIDI learn is scattered** across clip, drum, instrument and section fields, beside the controller mappings.

---

## The target

```
Song
├── transport · groove · scale · arrangement
├── master chain · reverb
├── mapping table            source (cable, channel, note/CC) → target address, for every learned control
└── Track*
    ├── identity             name · colour · mixer
    ├── input routing        which cable and channel play it
    ├── chain                note devices → instrument → audio effects
    │                        └─ a kit's instrument is a Rack of Lanes;
    │                           each Lane = name · input routing · its own full chain
    └── Clip*
        ├── notes            by pitch, or by lane in a kit
        ├── automation       lanes addressing devices in the track's chain (lanes' devices included)
        ├── playback         length · direction · launch style · colour · section · name
        └── clip values      a value layer: any device value resolves  clip value → track value
```

- **A track** is identity, input routing and a chain. It is not an instrument.
- **A chain** is note devices (arp, transpose, chord, scale filter…) → one instrument → audio effects (the host
  chain the domain split's P3 built).
- **An instrument** is one device at the L3 boundary: the Deluge synth, the sampler/clip player (live input is one
  of its modes), MIDI out, CV out. Its preset identity, engine state and MIDI channel are its own, not the
  track's.
- **A kit** is a track whose instrument is a **rack**. A rack holds **lanes**, and each lane is a full nested
  chain with its own name and input routing; a lane's instrument may be any instrument. The whole-kit arp is a
  note device before the rack; a drum's arp is a note device in its lane.
- **A clip** is content: notes, automation and playback properties, plus **clip values**: a value layer over the
  track's devices. Any device value resolves clip value → track value. Deluge's "two clips of one synth sound
  different" is this layer. The per-clip params, per-clip arp and per-clip MIDI program are all it, not separate
  mechanisms.
- **The mapping table** is the only home of learned control. Knobs, clip mute, section launch, and a track's or
  lane's note input are each an entry from a source to a target address. P2c's controller mappings are the first
  part of it.

## Rulings

- **Clip values stay** (Kate, 2026-10-16), generalised as the value layer above.
- **The arp is split by where its modulation lives.** A synth's arp stays inside the Deluge instrument: its rate
  and gate are patched params the mod matrix drives, and anything the patcher drives stays inside the instrument
  (as the sound chain does). MIDI and CV tracks and kits use a host note-device arp.
- **Kit lanes are full chains**, not drum-only slots.
- **One mapping table** for all learn.

---

## How today's objects map

| Today | Target |
|---|---|
| `Output` (and `DocOutput`) | a track, plus its instrument device's state |
| `Drum` (and `DocDrum`) | a lane in a rack |
| `Kit` | a track whose instrument is a rack |
| `InstrumentClip::arpSettings` | clip values of the track's arp |
| `Drum::arpSettings` | the arp note device in a lane |
| `InstrumentClip` MIDI program | clip values of the MIDI-out instrument |
| a clip's param manager values | clip values of the track's devices |
| `Instrument::midiInput`, `Drum::midiInput` | the track's or lane's input routing, in the mapping table |
| clip/drum mute commands, section launch commands, `ControllerMapping` | the mapping table |
| `NoteRow` in a kit clip | the clip's notes for one lane |

---

## The path

1. **Records at their target homes, on today's owners** (P3d-1c step 1). Each setting group (arp, MIDI program,
   input routing, names, directions, audio input…) is an owner-agnostic value type with its own settings table
   and codec, written by one path-addressed Edit. Today's owners stand in for the target ones: `DocOutput` for
   the track and its instrument device, `DocDrum` for the lane, `DocClip` for the clip value layer, and the song
   for the mapping table. The engine stays as it is behind projections.
2. **The scenes bind to those records** (P3d-1c steps 2-4), and the legacy menus retire.
3. **The topology refactor** (the domain split's P5): the model's records take the target shape (Track, Chain with
   device slots, Rack and Lane), mostly renaming and re-nesting the records step 1 made, and the render
   materialises runtime objects by id through the reconciler.

## Open

- Where the track's mixer values (level, pan) live: the track, or the chain's last device.
- Stutter's place on a synth track (song-owned, but rendered mid-chain today; see the domain split's P3d notes).
- Whether a melodic clip's notes keep pitch rows with their own row values (today's note rows), or rows become
  lanes only in kits.
