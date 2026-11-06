import type { Link, Nodes, Parent, Root, Text } from "mdast"
import { SKIP, visit } from "unist-util-visit"

/** A linkable term: any of `phrases` in prose links to `href`. */
export type XrefTerm = {
  phrases: string[]
  href: string
  kind: "concept" | "menu"
  /** Menu names match only as written (book capitals); concepts match any case. */
  caseSensitive?: boolean
}

/** Nodes whose text is never linked: headings, existing links, code, directives (`:key[...]`), JSX (figures). */
const NO_LINK_INSIDE = new Set<string>([
  "heading",
  "link",
  "linkReference",
  "definition",
  "inlineCode",
  "code",
  "html",
  "image",
  "imageReference",
  "textDirective",
  "leafDirective",
  "containerDirective",
  "mdxJsxFlowElement",
  "mdxJsxTextElement",
  "mdxFlowExpression",
  "mdxTextExpression",
  "yaml",
  "toml",
])

const pathOf = (href: string) => href.split("#")[0]
const escape = (s: string) => s.replace(/[.*+?^${}()|[\]\\]/g, "\\$&")

/** A capitalised word such as SIDECHAIN or MOD-FX (two or more characters, starting with a letter). */
const CAPS_WORD = "\\p{Lu}[\\p{Lu}\\p{N}\\-]+"
const CAPS_BEFORE = new RegExp(`(?:^|[^\\p{L}\\p{N}])${CAPS_WORD}\\s+$`, "u")
const CAPS_AFTER = new RegExp(`^\\s+${CAPS_WORD}(?![\\p{Ll}])`, "u")

/**
 * A menu name only counts when it is the whole capitalised run: AUTOMATION in
 * ‘AUTOMATION DELETED’ (a display message) or COMPRESSOR in SIDECHAIN COMPRESSOR is part of
 * something else.
 */
function isWholeCapsRun(text: string, index: number, length: number): boolean {
  return (
    !CAPS_BEFORE.test(text.slice(0, index)) &&
    !CAPS_AFTER.test(text.slice(index + length))
  )
}

type Matcher = { re: RegExp; term: XrefTerm; length: number }

function matchersFor(terms: XrefTerm[]): Matcher[] {
  return terms
    .flatMap((term) =>
      term.phrases.map((phrase) => ({
        // Whole words only: no letter or digit on either side.
        re: new RegExp(
          `(?<![\\p{L}\\p{N}])${escape(phrase)}(?![\\p{L}\\p{N}])`,
          term.caseSensitive ? "gu" : "giu",
        ),
        term,
        length: phrase.length,
      })),
    )
    .sort((a, b) => b.length - a.length)
}

/**
 * Link the first mention of each term on a page. A term never links to the page it is on.
 * Mutates `tree`.
 */
export function linkTerms(
  tree: Root,
  { terms, pageHref }: { terms: XrefTerm[]; pageHref: string },
): void {
  const here = pathOf(pageHref)
  const matchers = matchersFor(terms.filter((t) => pathOf(t.href) !== here))
  if (!matchers.length) return
  const linked = new Set<string>() // hrefs already linked on this page

  const split = (value: string): (Text | Link)[] => {
    const out: (Text | Link)[] = []
    let from = 0
    for (;;) {
      let best: { index: number; text: string; term: XrefTerm } | undefined
      for (const m of matchers) {
        if (linked.has(m.term.href)) continue
        m.re.lastIndex = from
        let hit = m.re.exec(value)
        while (
          hit &&
          m.term.kind === "menu" &&
          !isWholeCapsRun(value, hit.index, hit[0].length)
        ) {
          hit = m.re.exec(value)
        }
        if (hit && (!best || hit.index < best.index)) {
          best = { index: hit.index, text: hit[0], term: m.term }
        }
      }
      if (!best) break
      if (best.index > from)
        out.push({ type: "text", value: value.slice(from, best.index) })
      out.push({
        type: "link",
        url: best.term.href,
        children: [{ type: "text", value: best.text }],
        data: {
          hProperties: { className: ["xref", `xref-${best.term.kind}`] },
        },
      })
      linked.add(best.term.href)
      from = best.index + best.text.length
    }
    if (!out.length) return []
    if (from < value.length)
      out.push({ type: "text", value: value.slice(from) })
    return out
  }

  visit(tree, (node: Nodes, index, parent: Parent | undefined) => {
    if (NO_LINK_INSIDE.has(node.type)) return SKIP
    if (node.type !== "text" || !parent || index === undefined) return
    const parts = split(node.value)
    if (!parts.length) return
    parent.children.splice(index, 1, ...(parts as Parent["children"]))
    return [SKIP, index + parts.length]
  })
}
