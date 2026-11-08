import type { Root } from "mdast"
import { linkTerms, type XrefTerm } from "./link-terms.ts"

/** The part of a VFile this plugin reads. */
type SourceFile = { path?: string }

/**
 * Site path of a guidebook page from its source file, e.g.
 * `.../src/content/docs/guidebook/03-sequencing/02-playing-a-sequence.mdx`
 * -> `/guidebook/03-sequencing/02-playing-a-sequence` (the site has no trailing slashes).
 * Null for any other page.
 */
export function guidebookHref(filePath: string | undefined): string | null {
  const m = filePath
    ?.replaceAll("\\", "/")
    .match(/\/src\/content\/docs\/(guidebook\/.*)\.mdx?$/)
  if (!m) return null
  return `/${m[1].replace(/\/index$/, "")}`
}

/**
 * Links concept and menu terms on Guidebook pages: the first mention of each term per page,
 * never in headings, links, code, `:key[...]` or figures, and never to the page itself.
 */
export const remarkCrossLinks =
  ({ terms }: { terms: XrefTerm[] }) =>
  (tree: Root, file: SourceFile) => {
    const pageHref = guidebookHref(file.path)
    if (pageHref) linkTerms(tree, { terms, pageHref })
  }
