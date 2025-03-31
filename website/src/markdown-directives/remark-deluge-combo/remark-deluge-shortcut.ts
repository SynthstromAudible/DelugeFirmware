import type { Root } from "mdast"
import { visit } from "unist-util-visit"
import { toString } from "mdast-util-to-string"
import { displaySequence } from "./display-sequence.ts"

/**
 * Process `:key[SHIFT + LOAD > LOAD]` and display it as a formatted shortcut
 */
export const remarkDelugeShortcut = () => (tree: Root) => {
  visit(tree, function (node) {
    if (
      node.type === "containerDirective" ||
      node.type === "leafDirective" ||
      node.type === "textDirective"
    ) {
      if (node.name !== "key") return

      const data = node.data || (node.data = {})

      // TODO: add variants with attributes (e.g. "minimal")
      // const attributes = node.attributes || {}

      const string = toString(node)

      data.hName = "span"
      data.hProperties = {
        class: "button-sequence",
      }
      data.hChildren = displaySequence(string)
    }
  })
}
