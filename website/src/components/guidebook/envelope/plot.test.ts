import { strict as assert } from "node:assert"
import { test } from "node:test"
import envelopeJson from "../../../data/guidebook-envelope.json" with { type: "json" }
import { EnvelopeDataSchema } from "./envelope-data.ts"
import { defaultGate, timing } from "./envelope.ts"
import { renderPlot } from "./plot.ts"

const data = EnvelopeDataSchema.parse(envelopeJson)
const book = { attack: 30, decay: 20, sustain: 25, release: 30 }
const gate = defaultGate(data, book)

test("names every stage that has room, and marks note on and off", () => {
  const svg = renderPlot(data, book, { gate, destination: "volume" })
  for (const name of [
    "ATTACK",
    "DECAY",
    "SUSTAIN",
    "RELEASE",
    "Note ON",
    "Note OFF",
  ])
    assert.ok(svg.includes(`>${name}<`), name)
  assert.match(svg, /class="gb-env-curve" d="M75 155/)
  assert.ok(!svg.includes("gb-env-dot"))
})

test("a stage too short to label at this scale goes unnamed", () => {
  const svg = renderPlot(
    data,
    { ...book, attack: 0 },
    { gate, destination: "volume" },
  )
  assert.ok(!svg.includes(">ATTACK<"))
  assert.ok(svg.includes(">DECAY<"))
})

test("other destinations: the envelope is bipolar about the destination's value", () => {
  const svg = renderPlot(data, book, { gate, destination: "other" })
  assert.ok(svg.includes('class="gb-env-dest"'))
  assert.ok(svg.includes(">destination<") && svg.includes(">Mod depth<"))
  assert.ok(!svg.includes(">Silent<"))
})

test("a note being held: the curve runs to now, with a dot, and no note off yet", () => {
  const t = timing(data, book)
  const now = t.attack + t.decay / 2
  const svg = renderPlot(data, book, {
    gate,
    destination: "volume",
    now,
    holding: true,
    tMax: 5,
  })
  assert.ok(svg.includes("gb-env-dot"))
  assert.ok(!svg.includes(">Note OFF<"))
  assert.ok(!svg.includes(">RELEASE<"))
  // the curve stops at the dot
  const dot = svg.match(/class="gb-env-dot" cx="([\d.]+)" cy="([\d.]+)"/) ?? []
  const d = svg.match(/class="gb-env-curve" d="([^"]+)"/)?.[1] ?? ""
  assert.ok(d.endsWith(`L${dot[1]} ${dot[2]}`), d.slice(-40))
})

test("after letting go: note off at the gate, release drawn up to now", () => {
  const svg = renderPlot(data, book, {
    gate: 1,
    destination: "volume",
    now: 2,
    tMax: 10,
  })
  assert.ok(svg.includes(">Note OFF<"))
  assert.ok(svg.includes("gb-env-dot"))
})

test("drawn at the width shown: fewer ticks when narrow, labels without trailing zeros", () => {
  const wide = renderPlot(data, book, { gate, destination: "volume" })
  const narrow = renderPlot(data, book, {
    gate,
    destination: "volume",
    width: 340,
  })
  const ticks = (svg: string) =>
    [...svg.matchAll(/class="gb-env-tick"[^>]*>([^<]+)</g)].map((m) => m[1])
  assert.ok(
    ticks(narrow).length <= 4 && ticks(narrow).length >= 2,
    `${ticks(narrow)}`,
  )
  assert.ok(ticks(wide).length > ticks(narrow).length)
  assert.ok(ticks(wide).includes("2 s"), `${ticks(wide)}`)
  assert.match(narrow, /class="gb-env-bound" x1="75" y1="40" x2="328" y2="40"/)
})

test("in the book's style: the sustain level boxed on its arrow, stages between arrowheads", () => {
  const svg = renderPlot(data, book, { gate, destination: "other" })
  assert.match(svg, /class="gb-env-value"[^>]*>25</)
  assert.ok((svg.match(/class="gb-env-head"/g) ?? []).length >= 6)
  // volume: no destination line or mod depth
  const vol = renderPlot(data, book, { gate, destination: "volume" })
  assert.ok(!vol.includes("gb-env-dest") && !vol.includes(">Mod depth<"))
})
