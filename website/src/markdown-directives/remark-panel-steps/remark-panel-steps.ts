import type { Root } from "mdast"
import { toString } from "mdast-util-to-string"
import { visit } from "unist-util-visit"
import {
  keyControls,
  type StepControl,
} from "../../components/guidebook/panel/key-controls.ts"
import type { PanelMap } from "../../components/guidebook/panel/panel-map.ts"

type Node = { type: string; name?: string; children?: Node[] }
type JsxElement = Node & {
  attributes: { type: string; name: string; value?: unknown }[]
}

const isJsx = (n: Node, name: string): n is JsxElement =>
  (n.type === "mdxJsxFlowElement" || n.type === "mdxJsxTextElement") &&
  n.name === name

/** `:key[...]` directives in a step, not descending into nested lists (sub-steps, options). */
function stepKeys(node: Node): string[] {
  if (node.type === "textDirective" && node.name === "key")
    return [toString(node as never)]
  if (node.type === "list") return []
  return (node.children ?? []).flatMap(stepKeys)
}

function stepsControls(
  steps: Node,
  panel: PanelMap,
  warn: (msg: string) => void,
): StepControl[][] {
  const list = steps.children?.find((c) => c.type === "list")
  return (list?.children ?? []).map((item) =>
    (item.children ?? [])
      .flatMap(stepKeys)
      .flatMap((k) => keyControls(k, panel, warn)),
  )
}

/** The depth of the nearest heading before `index` among `nodes`, or 0 when there is none. */
function owningHeadingDepth(nodes: Node[], index: number): number {
  for (let j = index - 1; j >= 0; j--) {
    const n = nodes[j] as Node & { depth?: number }
    if (n.type === "heading") return n.depth ?? 0
  }
  return 0
}

/**
 * Give every <PanelFigure> the controls of each step of the nearest <Steps> before it, as a
 * `stepControls` JSON attribute (one array of StepControl per step), and that Steps' index among
 * all Steps on the page as `stepsIndex` (the page script binds to the same list by index).
 * A heading between them cuts the binding unless it is deeper than the heading that owns the
 * Steps (an "Example" under the procedure keeps it).
 */
export function bindPanelSteps(
  tree: Root,
  panel: PanelMap,
  warn: (msg: string) => void,
): void {
  const stepsOrder = new Map<Node, number>()
  visit(tree as unknown as Node, (node: Node) => {
    if (isJsx(node, "Steps")) stepsOrder.set(node, stepsOrder.size)
  })

  visit(tree as unknown as Node, (node: Node) => {
    const children = node.children
    if (!children) return
    children.forEach((child, i) => {
      if (!isJsx(child, "PanelFigure")) return
      if (child.attributes.some((a) => a.name === "stepControls")) return
      let shallowest = Infinity // shallowest heading passed on the way back
      for (let j = i - 1; j >= 0; j--) {
        const before = children[j] as Node & { depth?: number }
        if (before.type === "heading") {
          shallowest = Math.min(shallowest, before.depth ?? 0)
          continue
        }
        if (isJsx(before, "Steps")) {
          const owner = owningHeadingDepth(children, j)
          const cut =
            shallowest !== Infinity && (owner === 0 || shallowest <= owner)
          if (cut) return
          child.attributes.push(
            {
              type: "mdxJsxAttribute",
              name: "stepControls",
              value: JSON.stringify(stepsControls(before, panel, warn)),
            },
            {
              type: "mdxJsxAttribute",
              name: "stepsIndex",
              value: String(stepsOrder.get(before)),
            },
          )
          return
        }
      }
    })
  })
}

const attribute = (name: string, value: string | null) => ({
  type: "mdxJsxAttribute",
  name,
  value,
})

/** `import PanelFigure from "<path>"` as an MDX ESM node (MDX compiles from `data.estree`). */
function importNode(path: string): Node {
  return {
    type: "mdxjsEsm",
    value: `import PanelFigure from ${JSON.stringify(path)}`,
    data: {
      estree: {
        type: "Program",
        sourceType: "module",
        body: [
          {
            type: "ImportDeclaration",
            specifiers: [
              {
                type: "ImportDefaultSpecifier",
                local: { type: "Identifier", name: "PanelFigure" },
              },
            ],
            source: {
              type: "Literal",
              value: path,
              raw: JSON.stringify(path),
            },
          },
        ],
      },
    },
  } as Node
}

/**
 * After every <Steps> that names panel controls and has no PanelFigure bound to it (run
 * bindPanelSteps first), add a collapsed demo: `<PanelFigure auto id="panel-demo-N" …/>`, bound
 * like any other. Adds the PanelFigure import (from `componentPath`) if the page lacks one.
 */
export function insertPanelDemos(
  tree: Root,
  panel: PanelMap,
  warn: (msg: string) => void,
  componentPath: string,
): void {
  const bound = new Set<string>()
  const stepsOrder = new Map<Node, number>()
  visit(tree as unknown as Node, (node: Node) => {
    if (isJsx(node, "Steps")) stepsOrder.set(node, stepsOrder.size)
    if (isJsx(node, "PanelFigure")) {
      const i = node.attributes.find((a) => a.name === "stepsIndex")?.value
      if (typeof i === "string") bound.add(i)
    }
  })

  const inserts: { parent: Node; after: Node; demo: Node }[] = []
  visit(tree as unknown as Node, (node: Node, _i, parent?: Node) => {
    if (!isJsx(node, "Steps") || !parent) return
    const index = String(stepsOrder.get(node))
    if (bound.has(index)) return
    const controls = stepsControls(node, panel, warn)
    if (!controls.some((c) => c.length)) return
    const n = Number(index) + 1
    inserts.push({
      parent,
      after: node,
      demo: {
        type: "mdxJsxFlowElement",
        name: "PanelFigure",
        attributes: [
          attribute("auto", null),
          attribute("id", `panel-demo-${n}`),
          attribute(
            "alt",
            "The Deluge front panel, marking the controls each step uses",
          ),
          attribute("stepControls", JSON.stringify(controls)),
          attribute("stepsIndex", index),
        ],
        children: [],
      } as JsxElement,
    })
  })
  if (!inserts.length) return
  for (const { parent, after, demo } of inserts) {
    const siblings = parent.children ?? []
    siblings.splice(siblings.indexOf(after) + 1, 0, demo)
  }
  const imported = (tree.children as Node[]).some(
    (n) =>
      n.type === "mdxjsEsm" &&
      /\bimport\s+PanelFigure\b/.test((n as { value?: string }).value ?? ""),
  )
  if (!imported) (tree.children as Node[]).unshift(importNode(componentPath))
}

/**
 * Binds PanelFigures to their Steps on every page (see bindPanelSteps), then gives every other
 * Steps that names panel controls a collapsed demo (see insertPanelDemos).
 */
export const remarkPanelSteps =
  ({ panel, component }: { panel: PanelMap; component: string }) =>
  (tree: Root, file: { path?: string }) => {
    const page = file.path ?? "?"
    bindPanelSteps(tree, panel, (msg) =>
      console.warn(`[panel figure] ${page}: ${msg}`),
    )
    // Demos are components: MDX pages only, and only the Guidebook's.
    if (!/\/guidebook\/.*\.mdx$/.test(page)) return
    // Demos are not hand-checked: one summary line per page, not one per control.
    const missed = new Set<string>()
    insertPanelDemos(tree, panel, (msg) => missed.add(msg), component)
    if (missed.size)
      console.warn(
        `[panel demo] ${page}: ${missed.size} control name(s) not highlighted (${[...missed].map((m) => m.split(";")[0]).join("; ")})`,
      )
  }
