import { strict as assert } from "node:assert"
import { test } from "node:test"
import {
  buildIndex,
  uncovered,
  type IndexPage,
  type IndexVocab,
} from "./build-index.ts"

const pages: IndexPage[] = [
  {
    section: "7.6",
    title: "Editing Clips in Song View",
    href: "/guidebook/07-song-view/06-editing-clips-in-song-view",
    headings: [
      {
        depth: 2,
        slug: "deleting-a-clip-from-within-song-view",
        text: "Deleting a clip from within song view",
      },
      {
        depth: 2,
        slug: "cloning-a-clip-from-within-song-view",
        text: "Cloning a clip from within song view",
      },
    ],
  },
  {
    section: "9.6",
    title: "Recording into Audio Clips in Song View",
    href: "/guidebook/09-audio/06-recording-into-audio-clips-in-song-view",
    headings: [
      {
        depth: 2,
        slug: "selecting-the-audio-in",
        text: "Selecting the audio in",
      },
    ],
  },
  {
    section: "9.7",
    title: "Recording into Audio Clips in Arranger View",
    href: "/guidebook/09-audio/07-recording-into-audio-clips-in-arranger-view",
    headings: [
      {
        depth: 2,
        slug: "selecting-the-audio-in",
        text: "Selecting the audio in",
      },
      { depth: 4, slug: "deep", text: "A clip detail too deep to index" },
    ],
  },
  {
    section: "3.3",
    title: "Tempo & Swing",
    href: "/guidebook/03-sequencing/03-tempo-swing",
    headings: [{ depth: 2, slug: "adjusting-swing", text: "Adjusting swing" }],
  },
]

const vocab: IndexVocab = {
  headwords: [
    { head: "Clip", match: ["clip", "clips"], not: ["clip view"] },
    { head: "Song view", match: ["song view"] },
    { head: "Audio input", match: ["audio in"] },
    { head: "Swing", match: ["swing"] },
    {
      head: "Undo",
      match: [],
      refs: [
        {
          label: "in song view",
          href: "/guidebook/07-song-view/06-editing-clips-in-song-view",
          section: "7.6",
        },
      ],
    },
  ],
  see: [{ term: "Clone", see: ["Clip"] }],
  external: [
    {
      head: "Performance view",
      label: "community features",
      href: "/features/performance_view",
    },
  ],
}

const entry = (index: ReturnType<typeof buildIndex>, head: string) =>
  index.flatMap((g) => g.entries).find((e) => e.head === head)

test("a heading is listed under every headword it names, linked to its section", () => {
  const index = buildIndex(pages, vocab)
  const clip = entry(index, "Clip")
  assert.ok(clip)
  assert.deepEqual(
    clip.subs.map((s) => s.label),
    [
      "Cloning a clip from within song view",
      "Deleting a clip from within song view",
      "Editing Clips in Song View",
      "Recording into Audio Clips in Arranger View",
      "Recording into Audio Clips in Song View",
    ],
  )
  assert.deepEqual(clip.subs[1].links, [
    {
      text: "7.6",
      href: "/guidebook/07-song-view/06-editing-clips-in-song-view#deleting-a-clip-from-within-song-view",
    },
  ])
  // the same heading also sits under Song view; page titles count as headings too
  assert.deepEqual(
    entry(index, "Song view")?.subs.map((s) => s.label),
    [
      "Cloning a clip from within song view",
      "Deleting a clip from within song view",
      "Editing Clips in Song View",
      "Recording into Audio Clips in Song View",
    ],
  )
})

test("the same heading on several pages is one subentry with a link per section", () => {
  const audio = entry(buildIndex(pages, vocab), "Audio input")
  assert.deepEqual(audio?.subs, [
    {
      label: "Selecting the audio in",
      links: [
        {
          text: "9.6",
          href: "/guidebook/09-audio/06-recording-into-audio-clips-in-song-view#selecting-the-audio-in",
        },
        {
          text: "9.7",
          href: "/guidebook/09-audio/07-recording-into-audio-clips-in-arranger-view#selecting-the-audio-in",
        },
      ],
    },
  ])
})

test("whole words only, `not` phrases excluded, deep headings skipped", () => {
  const clip = entry(buildIndex(pages, vocab), "Clip")
  assert.ok(!clip?.subs.some((s) => /deep/.test(s.label)))
  const narrow: IndexVocab = {
    headwords: [
      { head: "Clip", match: ["clip"], not: ["clip from"] },
      { head: "Audio", match: ["audio"] },
    ],
    see: [],
    external: [],
  }
  const index = buildIndex(pages, narrow)
  // "clip" doesn't match "Clips" (whole words), and "clip from" is excluded
  assert.deepEqual(entry(index, "Clip")?.subs, [])
  // any case, anywhere in the heading
  assert.ok(
    entry(index, "Audio")?.subs.some(
      (s) => s.label === "Selecting the audio in",
    ),
  )
})

test("hand-added refs, see entries and external pointers", () => {
  const index = buildIndex(pages, vocab)
  assert.deepEqual(entry(index, "Undo")?.subs, [
    {
      label: "in song view",
      links: [
        {
          text: "7.6",
          href: "/guidebook/07-song-view/06-editing-clips-in-song-view",
        },
      ],
    },
  ])
  assert.deepEqual(entry(index, "Clone")?.see, ["Clip"])
  assert.deepEqual(entry(index, "Performance view")?.external, {
    label: "community features",
    href: "/features/performance_view",
  })
})

test("entries are grouped by letter, A to Z", () => {
  const index = buildIndex(pages, vocab)
  assert.deepEqual(
    index.map((g) => [g.letter, g.entries.map((e) => e.head)]),
    [
      ["A", ["Audio input"]],
      ["C", ["Clip", "Clone"]],
      ["P", ["Performance view"]],
      ["S", ["Song view", "Swing"]],
      ["U", ["Undo"]],
    ],
  )
})

test("uncovered lists the headings no headword catches", () => {
  const thin: IndexVocab = {
    headwords: [{ head: "Swing", match: ["swing"] }],
    see: [],
    external: [],
  }
  assert.deepEqual(
    uncovered(pages, thin).map((h) => `${h.section} ${h.text}`),
    [
      "7.6 Editing Clips in Song View",
      "7.6 Deleting a clip from within song view",
      "7.6 Cloning a clip from within song view",
      "9.6 Recording into Audio Clips in Song View",
      "9.6 Selecting the audio in",
      "9.7 Recording into Audio Clips in Arranger View",
      "9.7 Selecting the audio in",
    ],
  )
})

test("a headword can claim every heading on given sections", () => {
  const v: IndexVocab = {
    headwords: [{ head: "Tempo", match: [], sections: ["3.3"] }],
    see: [],
    external: [],
  }
  assert.deepEqual(
    entry(buildIndex(pages, v), "Tempo")?.subs.map((s) => s.label),
    ["Adjusting swing", "Tempo & Swing"],
  )
})

test("a title and heading that differ only in case are one subentry, one link per section", () => {
  const same: IndexPage[] = [
    {
      section: "3.2",
      title: "Playing a Sequence",
      href: "/guidebook/03-sequencing/02-playing-a-sequence",
      headings: [
        { depth: 2, slug: "playing-a-sequence", text: "Playing a sequence" },
      ],
    },
  ]
  const v: IndexVocab = {
    headwords: [{ head: "Playing", match: ["playing"] }],
    see: [],
    external: [],
  }
  assert.deepEqual(entry(buildIndex(same, v), "Playing")?.subs, [
    {
      label: "Playing a Sequence",
      links: [
        {
          text: "3.2",
          href: "/guidebook/03-sequencing/02-playing-a-sequence#playing-a-sequence",
        },
      ],
    },
  ])
})

test("appendix sections sort after numbered ones", () => {
  const page = (section: string): IndexPage => ({
    section,
    title: `Page ${section}`,
    href: `/guidebook/p-${section}`,
    headings: [{ depth: 2, slug: "shared", text: "Shared heading" }],
  })
  const v: IndexVocab = {
    headwords: [{ head: "Shared", match: ["shared"] }],
    see: [],
    external: [],
  }
  const shared = entry(
    buildIndex([page("21.4"), page("A"), page("3.2")], v),
    "Shared",
  )
  assert.deepEqual(
    shared?.subs[0].links.map((l) => l.text),
    ["3.2", "21.4", "A"],
  )
})

test("a version badge in a heading stays out of the index", () => {
  const badged: IndexPage = {
    section: "16.8",
    title: "Threshold Recording",
    href: "/guidebook/16-audio/08-threshold-recording",
    headings: [
      {
        depth: 2,
        slug: "arming",
        text: 'Arming a clip <Badge text="C1.3" variant="tip" />',
      },
    ],
  }
  const groups = buildIndex([badged], {
    headwords: [{ head: "Clip", match: ["clip"] }],
    see: [],
    external: [],
  })
  const entry = groups.flatMap((g) => g.entries)[0]
  assert.deepEqual(
    entry.subs.map((s) => s.label),
    ["Arming a clip"],
  )
})
