import { strict as assert } from "node:assert"
import { test } from "node:test"
import type { Root, RootContent } from "mdast"
import type { PanelMap } from "../../components/guidebook/panel/panel-map.ts"
import { bindPanelSteps } from "./remark-panel-steps.ts"

const circle = { kind: "button", shape: "circle", cx: 0, cy: 0, r: 10 } as const
const panel = {
  controls: {
    Clip: circle,
    Play: circle,
    Shift: circle,
    "Horizontal-Encoder": circle,
  },
  padLabels: {},
  ambiguousPadLabels: {},
} as unknown as PanelMap

const t = (value: string) => ({ type: "text", value })
const key = (value: string) => ({
  type: "textDirective",
  name: "key",
  children: [t(value)],
})
const p = (...children: unknown[]) => ({ type: "paragraph", children })
const item = (...children: unknown[]) => ({ type: "listItem", children })
const list = (...items: unknown[]) => ({
  type: "list",
  ordered: true,
  children: items,
})
const steps = (...items: unknown[]) =>
  ({
    type: "mdxJsxFlowElement",
    name: "Steps",
    attributes: [],
    children: [list(...items)],
  }) as unknown as RootContent
const figure = () =>
  ({
    type: "mdxJsxFlowElement",
    name: "PanelFigure",
    attributes: [{ type: "mdxJsxAttribute", name: "id", value: "fig-x" }],
    children: [],
  }) as unknown as RootContent
const heading = () =>
  ({
    type: "heading",
    depth: 3,
    children: [t("Next")],
  }) as unknown as RootContent

const stepControlsOf = (tree: Root, index: number) => {
  const node = tree.children[index] as unknown as {
    attributes: { name: string; value: string }[]
  }
  const attr = node.attributes.find((a) => a.name === "stepControls")
  return attr ? JSON.parse(attr.value) : undefined
}

const run = (tree: Root, warnings: string[] = []) =>
  bindPanelSteps(tree, panel, (m) => warnings.push(m))

test("each step's :key controls, in step order", () => {
  const tree: Root = {
    type: "root",
    children: [
      steps(
        item(p(t("Press "), key("Clip"))),
        item(p(t("Press "), key("Play"))),
        item(p(t("Press "), key("Horizontal + Play"))),
      ),
      figure(),
    ],
  }
  run(tree)
  assert.deepEqual(stepControlsOf(tree, 1), [
    [{ id: "Clip", action: "press" }],
    [{ id: "Play", action: "press" }],
    [
      { id: "Horizontal-Encoder", action: "hold" },
      { id: "Play", action: "press" },
    ],
  ])
})

test("a prose-only step keeps its place with no controls", () => {
  const tree: Root = {
    type: "root",
    children: [
      steps(item(p(key("Clip"))), item(p(t("Listen."))), item(p(key("Play")))),
      figure(),
    ],
  }
  run(tree)
  const got = stepControlsOf(tree, 1)
  assert.equal(got.length, 3)
  assert.deepEqual(got[1], [])
  assert.deepEqual(got[2], [{ id: "Play", action: "press" }])
})

test("keys in a nested list belong to their step but are not new steps", () => {
  const tree: Root = {
    type: "root",
    children: [
      steps(item(p(key("Clip")), list(item(p(key("Shift")))))),
      figure(),
    ],
  }
  run(tree)
  const got = stepControlsOf(tree, 1)
  assert.equal(got.length, 1)
})

test("binds to the nearest Steps before it, not an earlier list", () => {
  const tree: Root = {
    type: "root",
    children: [
      steps(item(p(key("Clip")))),
      steps(item(p(key("Play"))), item(p(key("Shift")))),
      p(t("The figure shows this.")) as unknown as RootContent,
      figure(),
    ],
  }
  run(tree)
  assert.equal(stepControlsOf(tree, 3).length, 2)
})

test("a heading in between means the figure has no steps", () => {
  const tree: Root = {
    type: "root",
    children: [steps(item(p(key("Clip")))), heading(), figure()],
  }
  run(tree)
  assert.equal(stepControlsOf(tree, 2), undefined)
})

test("a figure with no Steps before it gets no stepControls", () => {
  const tree: Root = { type: "root", children: [figure()] }
  run(tree)
  assert.equal(stepControlsOf(tree, 0), undefined)
})

const h = (depth: number) =>
  ({ type: "heading", depth, children: [t("H")] }) as unknown as RootContent
const attrOf = (tree: Root, index: number, name: string) =>
  (
    tree.children[index] as unknown as {
      attributes: { name: string; value: string }[]
    }
  ).attributes.find((a) => a.name === name)?.value

test("a deeper heading (an example under the procedure) doesn't cut the binding", () => {
  const tree: Root = {
    type: "root",
    children: [
      h(3),
      steps(item(p(key("Clip"))), item(p(key("Play")))),
      h(4),
      p(t("Example.")) as unknown as RootContent,
      figure(),
    ],
  }
  run(tree)
  assert.equal(stepControlsOf(tree, 4).length, 2)
})

test("a heading at the procedure's level cuts the binding", () => {
  const tree: Root = {
    type: "root",
    children: [h(3), steps(item(p(key("Clip")))), h(3), figure()],
  }
  run(tree)
  assert.equal(stepControlsOf(tree, 3), undefined)
})

test("the bound Steps is named by its index among all Steps in the page", () => {
  const aside = {
    type: "mdxJsxFlowElement",
    name: "Aside",
    attributes: [],
    children: [steps(item(p(key("Shift"))))],
  } as unknown as RootContent
  const tree: Root = {
    type: "root",
    children: [
      steps(item(p(key("Clip")))), // Steps #0
      steps(item(p(key("Play"))), item(p(key("Shift")))), // Steps #1, bound
      aside, // contains Steps #2
      figure(),
    ],
  }
  run(tree)
  assert.equal(attrOf(tree, 3, "stepsIndex"), "1")
  assert.equal(stepControlsOf(tree, 3).length, 2)
})
