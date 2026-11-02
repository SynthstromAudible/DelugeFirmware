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

/**
 * Give every <PanelFigure> the controls of each step of the nearest <Steps> before it (stopping
 * at a heading), as a `stepControls` JSON attribute: one array of StepControl per step.
 */
export function bindPanelSteps(
  tree: Root,
  panel: PanelMap,
  warn: (msg: string) => void,
): void {
  visit(tree as unknown as Node, (node: Node) => {
    const children = node.children
    if (!children) return
    children.forEach((child, i) => {
      if (!isJsx(child, "PanelFigure")) return
      if (child.attributes.some((a) => a.name === "stepControls")) return
      for (let j = i - 1; j >= 0; j--) {
        const before = children[j]
        if (before.type === "heading") return
        if (isJsx(before, "Steps")) {
          child.attributes.push({
            type: "mdxJsxAttribute",
            name: "stepControls",
            value: JSON.stringify(stepsControls(before, panel, warn)),
          })
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
