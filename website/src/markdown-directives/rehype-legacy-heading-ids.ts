import { rehypeHeadingIds, type RehypePlugin } from "@astrojs/markdown-remark"
import type { Element } from "hast"
import { visit } from "unist-util-visit"

/**
 * Keeps heading anchors the same as they were before Astro 6.
 *
 * Astro 6 switched to GitHub-compatible heading IDs, which keep a trailing
 * hyphen when a heading ends in whitespace, e.g. `### Kit FX <Badge … />`
 * became `#kit-fx-` instead of `#kit-fx`. Deep links to those headings exist
 * outside the site, so this assigns IDs the old way: slug as usual, then drop
 * one trailing hyphen.
 *
 * Astro runs its own heading-ID pass last and keeps any `id` already set, so
 * the table of contents picks up these IDs too.
 */
export const rehypeLegacyHeadingIds: RehypePlugin = () => (tree, file) => {
  const isHeading = (node: Element) => /^h[1-6]$/.test(node.tagName)

  // IDs set explicitly (by an author or another plugin) are left untouched.
  const explicit = new Set<Element>()
  visit(tree, "element", (node) => {
    if (isHeading(node) && typeof node.properties.id === "string") {
      explicit.add(node)
    }
  })

  // Typed as a unified plugin (transformer | void) but always returns a
  // synchronous transformer.
  const assignIds = rehypeHeadingIds() as (
    t: typeof tree,
    f: typeof file,
  ) => void
  assignIds(tree, file)

  visit(tree, "element", (node) => {
    if (!isHeading(node) || explicit.has(node)) return
    const { id } = node.properties
    if (typeof id === "string" && id.endsWith("-")) {
      node.properties.id = id.slice(0, -1)
    }
  })
}
