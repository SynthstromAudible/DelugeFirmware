# Hybrid session layout

Hold SONG and turn SELECT to cycle Rows → Grid → Hybrid. Hybrid is also available
as the default session layout in Settings. The selected layout is saved with the song.

Hybrid shows seven tracks, ordered like Grid's track columns, from top to bottom:

| Area | Function |
| --- | --- |
| Main columns 1–8, upper seven rows | Grid clip launch/edit pads; columns represent sections |
| Main columns 9–16, upper seven rows | Active clip previews, using Row view rendering and clip press/hold handling |
| Main columns 1–8, bottom row | Section pads, using Grid's launch/repeat/MIDI-learn behavior |
| Main columns 9–16, bottom row | Empty |
| First sidebar column, upper seven rows | Track-edit pads, enabled by the Track Edit setting |
| First sidebar column, bottom row | Empty |
| Second sidebar column | Existing Grid mode and loop pads |

The preview shows the same time span as Row view, with twice the time per pad.
It displays the output's active clip even while transport is stopped. Playback
positions appear only in the preview half. Press a preview briefly to open its
clip; hold it for clip controls. Copying between preview rows targets the source
clip's section on the destination track, and refuses an occupied destination.

The horizontal encoder scrolls sections; the vertical encoder scrolls tracks.
Hold and turn the horizontal encoder to zoom the right-hand previews in or out.
The launch pads and section viewport stay fixed while zooming.
The orange and cyan take loop pads from PR #5299 are available in the far-right sidebar.

Holding a track pad while turning the vertical encoder reorders that track.
Holding only a section pad while turning the horizontal encoder reorders that
section, carrying its clips, colour, repeat count, MIDI command, and macro
references with it. The section stays under the held pad; temporary blank columns
are inert. Section colours are saved with the song (PR #5314).

Track selection, audition, Shift-lock, parameter editing, and pulse settings use
the existing Track Edit implementation. Track pads do not mute tracks.
