import { strict as assert } from "node:assert"
import { test } from "node:test"
import type { Root, RootContent } from "mdast"
import { linkTerms, type XrefTerm } from "./link-terms.ts"

const p = (...children: RootContent[]) =>
  ({ type: "paragraph", children }) as RootContent
const t = (value: string) => ({ type: "text", value }) as RootContent
const root = (...children: RootContent[]): Root => ({ type: "root", children })

const arranger: XrefTerm = {
  phrases: ["arranger view", "arranger"],
  href: "/guidebook/08-arranger-view/03-arranger-view-basics",
  kind: "concept",
}
const unison: XrefTerm = {
  phrases: ["UNISON"],
  href: "/reference/menu_hierarchies#menu-synthSoundMenu-unisonMenu",
  kind: "menu",
  caseSensitive: true,
}

/** Paragraph children rendered as a compact string: links as [text](url). */
function render(tree: Root): string[] {
  return tree.children.map((block) =>
    ((block as { children?: RootContent[] }).children ?? [])
      .map((n) =>
        n.type === "text"
          ? n.value
          : n.type === "link"
            ? `[${(n.children[0] as { value: string }).value}](${n.url})`
            : `<${n.type}>`,
      )
      .join(""),
  )
}

const page = "/guidebook/03-sequencing/02-playing-a-sequence"

test("links the first mention only, keeping the original text", () => {
  const tree = root(p(t("Open Arranger View. Later, arranger view again.")))
  linkTerms(tree, { terms: [arranger], pageHref: page })
  assert.deepEqual(render(tree), [
    `Open [Arranger View](${arranger.href}). Later, arranger view again.`,
  ])
})

test("first mention is per page, across paragraphs", () => {
  const tree = root(p(t("the arranger")), p(t("the arranger")))
  linkTerms(tree, { terms: [arranger], pageHref: page })
  assert.deepEqual(render(tree), [
    `the [arranger](${arranger.href})`,
    "the arranger",
  ])
})

test("prefers the longest phrase at the same position", () => {
  const tree = root(p(t("in arranger view")))
  linkTerms(tree, { terms: [arranger], pageHref: page })
  assert.deepEqual(render(tree), [`in [arranger view](${arranger.href})`])
})

test("matches whole words only", () => {
  const tree = root(p(t("rearrangers and arrangerish")))
  linkTerms(tree, { terms: [arranger], pageHref: page })
  assert.deepEqual(render(tree), ["rearrangers and arrangerish"])
})

test("menu names match only as written in capitals", () => {
  const tree = root(p(t("voices in unison. Set UNISON to 4.")))
  linkTerms(tree, { terms: [unison], pageHref: page })
  assert.deepEqual(render(tree), [
    `voices in unison. Set [UNISON](${unison.href}) to 4.`,
  ])
})

test("a menu name must be the whole capitalised run, not part of a longer one", () => {
  const automation: XrefTerm = {
    ...unison,
    phrases: ["AUTOMATION"],
    href: "/m#automation",
  }
  const tree = root(
    p(
      t(
        "The display shows ‘AUTOMATION DELETED’, then USB AUTOMATION. Set AUTOMATION to on.",
      ),
    ),
  )
  linkTerms(tree, { terms: [automation], pageHref: page })
  assert.deepEqual(render(tree), [
    "The display shows ‘AUTOMATION DELETED’, then USB AUTOMATION. Set [AUTOMATION](/m#automation) to on.",
  ])
})

test("multi-word menu names still link as a whole run", () => {
  const keyboardForText: XrefTerm = {
    ...unison,
    phrases: ["KEYBOARD FOR TEXT"],
    href: "/m#kft",
  }
  const tree = root(p(t("choose KEYBOARD FOR TEXT.")))
  linkTerms(tree, { terms: [keyboardForText], pageHref: page })
  assert.deepEqual(render(tree), ["choose [KEYBOARD FOR TEXT](/m#kft)."])
})

test("never links a page to itself", () => {
  const tree = root(p(t("arranger view")))
  linkTerms(tree, { terms: [arranger], pageHref: arranger.href })
  assert.deepEqual(render(tree), ["arranger view"])
})

test("a term targeting a heading on this page is a self-link too", () => {
  const sameHeading = { ...arranger, href: `${page}#tempo` }
  const tree = root(p(t("arranger view")))
  linkTerms(tree, { terms: [sameHeading], pageHref: page })
  assert.deepEqual(render(tree), ["arranger view"])
})

test("leaves headings, links, code, :key directives and JSX alone", () => {
  const tree = root(
    { type: "heading", depth: 2, children: [t("Arranger")] } as RootContent,
    p(
      { type: "link", url: "/x", children: [t("arranger")] } as RootContent,
      { type: "inlineCode", value: "arranger" } as RootContent,
      {
        type: "textDirective",
        name: "key",
        children: [t("Arranger")],
      } as RootContent,
      {
        type: "mdxJsxTextElement",
        name: "Figure",
        attributes: [],
        children: [t("arranger")],
      } as RootContent,
    ),
  )
  linkTerms(tree, { terms: [arranger], pageHref: page })
  assert.equal(
    JSON.stringify(tree).includes(arranger.href),
    false,
    "nothing inside excluded nodes is linked",
  )
})

test("links inside emphasis and list items", () => {
  const tree = root({
    type: "list",
    ordered: true,
    children: [
      {
        type: "listItem",
        children: [
          p({ type: "emphasis", children: [t("the arranger")] } as RootContent),
        ],
      },
    ],
  } as RootContent)
  linkTerms(tree, { terms: [arranger], pageHref: page })
  assert.ok(JSON.stringify(tree).includes(arranger.href))
})

test("several different terms in one text node", () => {
  const tree = root(p(t("Set UNISON, then open arranger view.")))
  linkTerms(tree, { terms: [arranger, unison], pageHref: page })
  assert.deepEqual(render(tree), [
    `Set [UNISON](${unison.href}), then open [arranger view](${arranger.href}).`,
  ])
})

test("links carry xref classes for styling", () => {
  const tree = root(p(t("UNISON")))
  linkTerms(tree, { terms: [unison], pageHref: page })
  const link = (tree.children[0] as { children: RootContent[] })
    .children[0] as {
    data: { hProperties: { className: string[] } }
  }
  assert.deepEqual(link.data.hProperties.className, ["xref", "xref-menu"])
})
