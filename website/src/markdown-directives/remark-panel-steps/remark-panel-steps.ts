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

/** Binds PanelFigures to their Steps on every page (see bindPanelSteps). */
export const remarkPanelSteps =
  ({ panel }: { panel: PanelMap }) =>
  (tree: Root, file: { path?: string }) =>
    bindPanelSteps(tree, panel, (msg) =>
      console.warn(`[panel figure] ${file.path ?? "?"}: ${msg}`),
    )
