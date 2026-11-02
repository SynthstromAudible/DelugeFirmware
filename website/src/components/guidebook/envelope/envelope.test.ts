import { strict as assert } from "node:assert"
import { test } from "node:test"
import envelopeJson from "../../../data/guidebook-envelope.json" with { type: "json" }
import { EnvelopeDataSchema } from "./envelope-data.ts"
import {
  curve,
  defaultGate,
  formatTime,
  levelAt,
  niceTicks,
  timing,
} from "./envelope.ts"

const data = EnvelopeDataSchema.parse(envelopeJson)
const book = { attack: 30, decay: 20, sustain: 25, release: 30 }
const near = (a: number, b: number, eps: number, msg?: string) =>
  assert.ok(Math.abs(a - b) <= eps, `${msg ?? ""} ${a} != ${b} ± ${eps}`)

test("stage times follow the firmware's rates (tests/data/envelope_rates_ref.txt)", () => {
  // attack 50: rate 64 per sample; decay 20 and release 30 from the same table
  near(timing(data, { ...book, attack: 50 }).attack, 2.972, 0.001)
  near(timing(data, book).decay, 3.28, 0.001)
  near(timing(data, book).release, 3.459, 0.001)
  near(timing(data, book).sustain, 0.5, 0.001)
  near(timing(data, { ...book, sustain: 50 }).sustain, 1, 1e-6) // 2^31 − 16 in the firmware
  assert.equal(timing(data, { ...book, sustain: 0 }).sustain, 0)
})

test("held note: attack rises from 0 to full, decay settles at sustain", () => {
  const t = timing(data, book)
  const gate = 100
  assert.deepEqual(levelAt(data, book, 0, gate).stage, "attack")
  near(levelAt(data, book, 0, gate).level, 0, 0.01)
  near(levelAt(data, book, t.attack * 0.999, gate).level, 1, 0.05)
  const mid = levelAt(data, book, t.attack * 0.5, gate).level
  assert.ok(mid > 0.3 && mid < 1, `curved attack mid-level ${mid}`)
  assert.equal(
    levelAt(data, book, t.attack + t.decay * 0.5, gate).stage,
    "decay",
  )
  const s = levelAt(data, book, t.attack + t.decay + 1, gate)
  assert.equal(s.stage, "sustain")
  near(s.level, 0.5, 0.001)
})

test("the decay curve eases toward sustain: most of the drop comes early", () => {
  const t = timing(data, book)
  const early = levelAt(data, book, t.attack + t.decay * 0.25, 100).level
  near(early, 0.5, 0.25)
  assert.ok(early < 0.75, `${early}`)
})

test("release falls from the level at note off to zero", () => {
  const t = timing(data, book)
  const gate = t.attack + t.decay + 0.5
  assert.equal(levelAt(data, book, gate + 0.001, gate).stage, "release")
  assert.ok(levelAt(data, book, gate + 0.001, gate).level <= 0.5)
  const end = levelAt(data, book, gate + t.release + 0.001, gate)
  assert.equal(end.stage, "off")
  assert.equal(end.level, 0)
})

test("letting go during the attack releases from the level reached", () => {
  const slow = { ...book, attack: 45 }
  const t = timing(data, slow)
  const gate = t.attack / 2
  const at = levelAt(data, slow, gate, Infinity).level
  const after = levelAt(data, slow, gate + 0.0001, gate)
  assert.equal(after.stage, "release")
  near(after.level, at, 0.01)
  assert.ok(at < 1)
})

test("sustain 0: the envelope ends after the decay even while held", () => {
  const z = { ...book, sustain: 0 }
  const t = timing(data, z)
  const after = levelAt(data, z, t.attack + t.decay + 0.1, 100)
  assert.equal(after.stage, "off")
  assert.equal(after.level, 0)
})

test("curve: starts at note on, ends silent, times increase", () => {
  const gate = defaultGate(data, book)
  const pts = curve(data, book, gate)
  assert.equal(pts[0].t, 0)
  assert.equal(pts.at(-1)?.level, 0)
  for (let i = 1; i < pts.length; i++) assert.ok(pts[i].t >= pts[i - 1].t)
  assert.ok(pts.some((p) => p.stage === "sustain"))
  // the gate falls after attack + decay, so every stage shows
  const t = timing(data, book)
  assert.ok(gate > t.attack + t.decay)
})

test("time labels and ticks", () => {
  assert.equal(formatTime(0.0004), "0.4 ms")
  assert.equal(formatTime(0.107), "107 ms")
  assert.equal(formatTime(3.459), "3.46 s")
  assert.equal(formatTime(27.17), "27.2 s")
  assert.deepEqual(niceTicks(1.2), [0, 0.2, 0.4, 0.6, 0.8, 1, 1.2])
  assert.deepEqual(niceTicks(7), [0, 2, 4, 6])
  assert.ok(niceTicks(0.003).length >= 3)
})
