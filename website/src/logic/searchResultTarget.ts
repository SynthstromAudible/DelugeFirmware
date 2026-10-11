const MENU_HIERARCHY_PATH =
  /\/reference\/(menu_hierarchies|menu-hierarchies)\/?$/i

function normalizeSearchTarget(value: string) {
  return value
    .toLocaleLowerCase()
    .replace(/[^\p{L}\p{N}]+/gu, " ")
    .trim()
}

const RESULT_SELECTOR =
  "p, li, td, th, dd, summary, figcaption, blockquote, pre"

function getSectionCandidates(content: HTMLElement, sectionId: string) {
  const heading = document.getElementById(sectionId)
  if (!(heading instanceof HTMLHeadingElement) || !content.contains(heading)) {
    return { candidates: [] as HTMLElement[], heading: null }
  }

  const headingLevel = Number(heading.tagName.slice(1))
  const sectionElements: HTMLElement[] = []
  let sibling = heading.nextElementSibling

  while (sibling) {
    if (
      sibling instanceof HTMLHeadingElement &&
      Number(sibling.tagName.slice(1)) <= headingLevel
    ) {
      break
    }

    if (sibling instanceof HTMLElement) {
      if (sibling.matches(RESULT_SELECTOR)) {
        sectionElements.push(sibling)
      }
      sectionElements.push(
        ...sibling.querySelectorAll<HTMLElement>(RESULT_SELECTOR),
      )
    }
    sibling = sibling.nextElementSibling
  }

  return { candidates: [heading, ...sectionElements], heading }
}

function getContextScore(node: HTMLElement, contextTerms: Set<string>) {
  if (!contextTerms.size) {
    return 0
  }

  const nodeTerms = new Set(
    normalizeSearchTarget(node.textContent ?? "")
      .split(" ")
      .filter(Boolean),
  )
  let matches = 0
  contextTerms.forEach((term) => {
    if (nodeTerms.has(term)) {
      matches += 1
    }
  })

  return matches / contextTerms.size
}

export function revealPageSearchTarget() {
  if (MENU_HIERARCHY_PATH.test(window.location.pathname)) {
    return
  }

  const searchTarget = new URLSearchParams(window.location.search)
    .get("search-target")
    ?.trim()
  const searchSection =
    new URLSearchParams(window.location.search).get("search-section") ?? ""
  const searchContext =
    new URLSearchParams(window.location.search).get("search-context") ?? ""
  const content = document.querySelector<HTMLElement>(".sl-markdown-content")

  if (
    !searchTarget ||
    !content ||
    content.dataset.revealedSearchTarget === searchTarget
  ) {
    return
  }

  const terms = normalizeSearchTarget(searchTarget).split(" ").filter(Boolean)
  const contextTerms = new Set(
    normalizeSearchTarget(searchContext).split(" ").filter(Boolean),
  )
  const section = getSectionCandidates(content, searchSection)
  const contentCandidates = section.heading
    ? section.candidates
    : Array.from(content.querySelectorAll<HTMLElement>(RESULT_SELECTOR))
  const pageTitle = document.querySelector<HTMLElement>(
    "main[data-pagefind-body] h1",
  )
  const candidatePool =
    !searchSection && pageTitle
      ? [pageTitle, ...contentCandidates]
      : contentCandidates
  const candidates = candidatePool.filter((node) => {
    const text = normalizeSearchTarget(node.textContent ?? "")
    return terms.every((term) => text.includes(term))
  })
  const target =
    candidates.sort((left, right) => {
      const scoreDifference =
        getContextScore(right, contextTerms) -
        getContextScore(left, contextTerms)

      return (
        scoreDifference ||
        (left.textContent?.length ?? 0) - (right.textContent?.length ?? 0)
      )
    })[0] ?? section.heading

  if (!target) {
    return
  }

  let ancestor = target.closest("details")
  while (ancestor) {
    ancestor.open = true
    ancestor = ancestor.parentElement?.closest("details") ?? null
  }

  content
    .querySelectorAll(".page-search-result-target")
    .forEach((node) => node.classList.remove("page-search-result-target"))
  target.classList.add("page-search-result-target")
  content.dataset.revealedSearchTarget = searchTarget

  const scrollToTarget = () => target.scrollIntoView({ block: "center" })

  requestAnimationFrame(scrollToTarget)
  window.setTimeout(scrollToTarget, 300)
}
