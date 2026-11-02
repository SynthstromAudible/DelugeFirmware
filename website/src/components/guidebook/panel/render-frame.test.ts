import { strict as assert } from "node:assert"
import { test } from "node:test"
import { renderFrame, windowBox } from "./render-frame.ts"
import type { PanelMap } from "./panel-map.ts"

const svg = [
  '<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 5808 3952" class="gb-panel-art">',
  '<g class="ctrl">',
  '<circle data-c="Play" cx="10" cy="10" r="5" stroke-width="4"/>',
  '<circle data-c="Shift" cx="30" cy="10" r="5" stroke-width="4"/>',
  '<rect data-c="pad-1-1" x="0" y="0" width="10" height="10" rx="2" stroke-width="4"/>',
  "</g></svg>",
].join("\n")

const panel = {
  regions: { transport: [4000, 300, 1700, 1200] },
} as unknown as PanelMap

const attrsOf = (out: string, id: string) =>
  out.match(new RegExp(`<[a-z]+[^>]*data-c="${id}"[^>]*>`))?.[0] ?? ""

test("the viewBox becomes the window", () => {
  const out = renderFrame(svg, {}, [], [4000, 300, 1700, 1200])
  assert.match(out, /viewBox="4000 300 1700 1200"/)
  assert.doesNotMatch(out, /viewBox="0 0 5808 3952"/)
})

test("pad colours are inline fills", () => {
  const out = renderFrame(
    svg,
    { pads: { "pad-1-1": "#ff0000" } },
    [],
    [0, 0, 1, 1],
  )
  assert.match(attrsOf(out, "pad-1-1"), /style="fill:#ff0000"/)
})

test("lit and blinking LEDs are classes", () => {
  const out = renderFrame(
    svg,
    { leds: { Play: "green", Shift: { colour: "white", blink: true } } },
    [],
    [0, 0, 1, 1],
  )
  assert.match(attrsOf(out, "Play"), /class="led led-green"/)
  assert.match(attrsOf(out, "Shift"), /class="led led-white led-blink"/)
})

test("an off LED adds nothing", () => {
  const out = renderFrame(svg, { leds: { Play: "off" } }, [], [0, 0, 1, 1])
  assert.doesNotMatch(attrsOf(out, "Play"), /class=/)
})

test("highlights mark only the named controls, with their action", () => {
  const out = renderFrame(
    svg,
    { leds: { Play: "green" } },
    [
      { id: "Shift", action: "hold" },
      { id: "Play", action: "press" },
    ],
    [0, 0, 1, 1],
  )
  assert.match(attrsOf(out, "Shift"), /class="hl hl-hold"/)
  assert.match(attrsOf(out, "Play"), /class="led led-green hl hl-press"/)
  assert.doesNotMatch(attrsOf(out, "pad-1-1"), /hl/)
})

test("the output never contains script", () => {
  const out = renderFrame(
    svg,
    {},
    [{ id: "Play", action: "turn" }],
    [0, 0, 1, 1],
  )
  assert.doesNotMatch(out, /<script/i)
})

test("windowBox resolves region names and explicit boxes", () => {
  assert.deepEqual(windowBox("transport", panel), [4000, 300, 1700, 1200])
  assert.deepEqual(windowBox([1, 2, 3, 4], panel), [1, 2, 3, 4])
  assert.throws(() => windowBox("attic", panel), /attic/)
})

test("frameAttributes: the classes and fill each control gets in a frame", async () => {
  const { frameAttributes } = await import("./render-frame.ts")
  const attrs = frameAttributes(
    {
      leds: { Play: "green", Shift: "off" },
      pads: { "pad-1-1": "#ff0000" },
    },
    [{ id: "Play", action: "press" }],
  )
  assert.deepEqual(attrs, [
    { id: "Play", class: "led led-green hl hl-press" },
    { id: "pad-1-1", style: "fill:#ff0000" },
  ])
})
