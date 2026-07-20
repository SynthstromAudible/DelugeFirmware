<script lang="ts">
  import { onDestroy } from "svelte";
  import { tick } from "svelte";
  import PageMain from "../components/PageMain.svelte";
  import PageFooter from "../components/PageFooter.svelte";
  import SelectedFiltersBar from "../components/SelectedFiltersBar.svelte";
  import SearchView from "../components/Search.svelte";
  import ShortcutVisibilityToggles from "../components/ShortcutVisibilityToggles.svelte";
  import ViewFilter from "../components/ViewFilter.svelte";
  import { searchQuery } from "../stores/search_store.js";
  import {
    activeShortcutSubSubCapability,
    activeShortcutCapability,
    activeShortcutSubCapability,
  } from "../stores/capability_store.js";
  import { activeControl } from "../stores/control_store.js";
  import { activeFirmware } from "../stores/firmware_store.js";
  import { activeView } from "../stores/view_store.js";

  let isMobilePanelOpen = false;
  let scrollY = 0;
  let innerWidth = 0;
  let isBackToTopVisible = false;
  let isMobilePanelStuck = false;
  let controlPanelEl: HTMLElement | undefined;

  function getCompanionTitleEl() {
    return document.querySelector<HTMLElement>(
      "#dc-companion-title, h1#_top",
    );
  }

  async function toggleMobilePanel() {
    const willOpen = !isMobilePanelOpen;

    if (willOpen) {
      isMobilePanelOpen = true;
      return;
    }

    isMobilePanelOpen = false;
  }

  function isDesktopViewport() {
    return typeof window !== "undefined" && window.matchMedia("(min-width: 72rem)").matches;
  }

  function isMobileDrawerViewport() {
    return typeof window !== "undefined" && window.matchMedia("(max-width: 49.999rem)").matches;
  }

  function cssLengthToPx(rawValue: string) {
    const value = rawValue.trim();
    if (!value) {
      return 0;
    }

    if (value.endsWith("px")) {
      const parsed = Number.parseFloat(value.slice(0, -2));
      return Number.isFinite(parsed) ? parsed : 0;
    }

    if (value.endsWith("rem")) {
      const parsed = Number.parseFloat(value.slice(0, -3));
      if (!Number.isFinite(parsed)) {
        return 0;
      }
      const rootFontSize = Number.parseFloat(
        window.getComputedStyle(document.documentElement).fontSize,
      );
      return Number.isFinite(rootFontSize) ? parsed * rootFontSize : 0;
    }

    const parsed = Number.parseFloat(value);
    return Number.isFinite(parsed) ? parsed : 0;
  }

  function getTopChromeOffset() {
    let maxBottom = 0;
    const isDesktopLayout =
      typeof window !== "undefined" &&
      window.matchMedia("(min-width: 72rem)").matches;
    const candidates = document.querySelectorAll<HTMLElement>(
      "header, [role='banner'], .sl-header, .dc-control-panel",
    );

    for (const element of candidates) {
      // Desktop places this panel in the right column; it should not count as
      // top chrome for scroll anchoring.
      if (isDesktopLayout && element.classList.contains("dc-control-panel")) {
        continue;
      }

      const style = window.getComputedStyle(element);
      if (style.position !== "fixed" && style.position !== "sticky") {
        continue;
      }

      const rect = element.getBoundingClientRect();
      if (rect.top < 160 && rect.bottom > 0 && rect.bottom > maxBottom) {
        maxBottom = rect.bottom;
      }
    }

    return Math.max(0, Math.ceil(maxBottom)) + 8;
  }

  function scrollToTop() {
    if (typeof window === "undefined") {
      return;
    }
    window.scrollTo({ top: 0, behavior: "smooth" });
  }

  // Track filter/search state for Svelte reactivity without auto-scrolling.
  $: {
    $searchQuery;
    $activeFirmware;
    $activeShortcutCapability;
    $activeShortcutSubCapability;
    $activeShortcutSubSubCapability;
    $activeView;
    $activeControl;
  }

  // Show back-to-top only once the companion heading has moved above
  // the sticky top chrome region.
  $: {
    scrollY;

    if (typeof window === "undefined") {
      isBackToTopVisible = false;
    } else {
      const titleEl = getCompanionTitleEl();
      if (!titleEl) {
        isBackToTopVisible = false;
      } else {
        const topOffset = getTopChromeOffset();
        const titleRect = titleEl.getBoundingClientRect();
        isBackToTopVisible = titleRect.bottom <= topOffset;
      }
    }
  }

  // Match TOC strip colors only while the mobile panel is pinned under the header.
  $: {
    scrollY;

    if (
      typeof window === "undefined" ||
      !controlPanelEl ||
      window.matchMedia("(min-width: 72rem)").matches
    ) {
      isMobilePanelStuck = false;
    } else {
      const navHeightRaw = window
        .getComputedStyle(document.documentElement)
        .getPropertyValue("--sl-nav-height");
      const navHeight = cssLengthToPx(navHeightRaw) || 56;
      const panelTop = controlPanelEl.getBoundingClientRect().top;

      // Small tolerance avoids sticky-state flicker around the threshold.
      isMobilePanelStuck = panelTop <= navHeight + 1;
    }
  }

  // Lock document scrolling while the mobile drawer-style panel is open.
  $: {
    innerWidth;

    if (isMobilePanelOpen && isDesktopViewport()) {
      isMobilePanelOpen = false;
    }

    if (typeof document === "undefined") {
      // no-op during SSR
    } else {
      document.body.classList.remove("dc-mobile-panel-open");
    }
  }

  onDestroy(() => {
    if (typeof document !== "undefined") {
      document.body.classList.remove("dc-mobile-panel-open");
    }
  });
</script>

<div class="dc-shortcuts-layout mx-auto w-full max-w-none">
  <div class="dc-main-column">
    <PageMain />
    <PageFooter />
  </div>

  <aside
    bind:this={controlPanelEl}
    class:dc-mobile-panel-open={isMobilePanelOpen}
    class:dc-control-panel-stuck={isMobilePanelStuck}
    class="dc-control-panel"
    aria-label="Search and filters panel"
  >
    <button
      type="button"
      class="dc-control-panel-toggle"
      aria-expanded={isMobilePanelOpen}
      aria-controls="dc-control-panel-content"
      on:click={toggleMobilePanel}
    >
      <span>Search and Filters</span>
      <span class:open={isMobilePanelOpen} class="dc-control-panel-caret" aria-hidden="true">
        <svg class="dc-control-panel-caret-icon" width="16" height="16" viewBox="0 0 24 24" fill="currentColor">
          <path d="m14.83 11.29-4.24-4.24a1 1 0 1 0-1.42 1.41L12.71 12l-3.54 3.54a1 1 0 0 0 0 1.41 1 1 0 0 0 .71.29 1 1 0 0 0 .71-.29l4.24-4.24a1.002 1.002 0 0 0 0-1.42Z"></path>
        </svg>
      </span>
    </button>

    <div
      id="dc-control-panel-content"
      class:open={isMobilePanelOpen}
      class="dc-control-panel-content"
    >
      <div class="dc-control-panel-shell">
        <h2 class="dc-control-panel-title">Search and Filters</h2>
        <SelectedFiltersBar />
        <SearchView autoFocus={false} />
        <ViewFilter />
        <ShortcutVisibilityToggles />
      </div>
    </div>
  </aside>
</div>

{#if isBackToTopVisible}
  <button
    type="button"
    class="dc-back-to-top"
    aria-label="Back to top"
    on:click={scrollToTop}
  >
    Back to top
  </button>
{/if}

<svelte:window bind:scrollY={scrollY} bind:innerWidth={innerWidth} />

<style>
  .dc-shortcuts-layout {
    display: flex;
    flex-direction: column;
    gap: 1rem;
  }

  .dc-main-column {
    min-width: 0;
    padding-left: 0;
    padding-right: 0;
  }

  .dc-control-panel {
    order: -1;
    position: sticky;
    top: var(--sl-nav-height, 3.5rem);
    z-index: 8;
    width: auto;
    margin-left: calc(-1 * (var(--sl-content-pad-x, 1rem) + 0.5rem));
    margin-right: calc(-1 * (var(--sl-content-pad-x, 1rem) + 0.5rem));
    padding: 0.40625rem 1rem;
    border-bottom: 1px solid var(--sl-color-hairline-light);
    background: var(--sl-color-bg-nav);
    box-shadow: none;
    backdrop-filter: none;
    display: block;
  }

  .dc-control-panel.dc-control-panel-stuck {
    background: var(--sl-color-bg-nav);
  }

  :global(html[data-theme="dark"]) .dc-control-panel.dc-control-panel-stuck {
    background: var(--sl-color-bg-nav);
  }

  .dc-control-panel-toggle {
    width: auto;
    max-width: min(92vw, 20rem);
    min-height: 34px;
    border: 1px solid var(--sl-color-gray-5);
    border-radius: 0.5rem;
    margin: 0;
    padding: 0.5rem 0.5rem 0.5rem 0.75rem;
    background: var(--sl-color-bg);
    color: inherit;
    text-align: left;
    font-weight: 400;
    font-size: 0.8125rem;
    line-height: 1;
    display: inline-flex;
    align-items: center;
    justify-content: space-between;
    gap: 0.5rem;
    cursor: pointer;
  }

  .dc-control-panel-toggle:hover {
    border-color: color-mix(in srgb, var(--sl-color-gray-4) 82%, var(--sl-color-gray-5));
  }

  .dc-control-panel-toggle:focus-visible {
    outline: 2px solid var(--sl-color-accent);
    outline-offset: 2px;
  }

  .dc-control-panel-caret {
    display: inline-flex;
    align-items: center;
    justify-content: center;
    width: 1rem;
    height: 1rem;
    transition: transform 0.2s ease-in-out;
  }

  .dc-control-panel-caret-icon {
    width: 1em;
    height: 1em;
  }

  .dc-control-panel-caret.open {
    transform: rotateZ(90deg);
  }

  .dc-control-panel-content {
    display: none;
    border-top: 0;
  }

  .dc-control-panel-content.open {
    display: block;
    width: 100%;
  }

  .dc-control-panel-shell {
    margin-top: 0.5rem;
    padding: 0.75rem 0.75rem 0.15rem;
    border: 1px solid var(--sl-color-gray-5);
    border-radius: 0.75rem;
    background: color-mix(in srgb, var(--sl-color-bg) 94%, var(--sl-color-gray-6));
    box-shadow: 0 6px 20px color-mix(in srgb, var(--sl-color-black, #000) 12%, transparent);
    backdrop-filter: blur(6px);
    max-height: none;
    overflow-x: hidden;
    overflow-y: auto;
    overscroll-behavior: contain;
    -webkit-overflow-scrolling: touch;
    scrollbar-width: none;
  }

  .dc-control-panel-content.open .dc-control-panel-shell {
    height: calc(100dvh - var(--sl-nav-height, 3.5rem) - 11.75rem);
    max-height: calc(100dvh - var(--sl-nav-height, 3.5rem) - 11.75rem);
  }

  .dc-control-panel-shell::-webkit-scrollbar {
    width: 0;
    height: 0;
  }

  @media (max-width: 49.999rem) {
    .dc-shortcuts-layout {
      padding-top: 0;
    }

    :global(main > .content-panel:first-child) {
      padding-top: 3rem;
    }

    .dc-control-panel {
      position: fixed;
      left: 0;
      right: 0;
      top: var(--sl-nav-height, 3.5rem);
      z-index: 11;
      margin: 0;
      padding-left: 1rem;
      padding-right: 1rem;
    }

    .dc-control-panel.dc-mobile-panel-open .dc-control-panel-content.open .dc-control-panel-shell {
      height: calc(100dvh - var(--sl-nav-height, 3.5rem) - 11.75rem);
      max-height: calc(100dvh - var(--sl-nav-height, 3.5rem) - 11.75rem);
    }
  }

  @media (min-width: 50rem) and (max-width: 71.999rem) {
    .dc-shortcuts-layout {
      padding-top: 0;
    }

    :global(main > .content-panel:first-child) {
      padding-top: 3rem;
    }

    .dc-control-panel {
      position: fixed;
      left: var(--sl-sidebar-width, 18.75rem);
      right: 0;
      top: var(--sl-nav-height, 3.5rem);
      z-index: 11;
      margin: 0;
      padding-left: var(--sl-content-pad-x, 1rem);
      padding-right: var(--sl-content-pad-x, 1rem);
    }

    .dc-control-panel.dc-mobile-panel-open .dc-control-panel-content.open .dc-control-panel-shell {
      height: calc(100dvh - var(--sl-nav-height, 3.5rem) - 11.75rem);
      max-height: calc(100dvh - var(--sl-nav-height, 3.5rem) - 11.75rem);
    }
  }

  :global(body.dc-mobile-panel-open) {
    overflow: hidden;
  }

  .dc-control-panel-title {
    margin: 0 0 0.6rem;
    font-size: 0.9rem;
    line-height: 1.2;
    font-weight: 700;
    color: var(--sl-color-gray-2);
    letter-spacing: 0.01em;
    text-transform: uppercase;
  }

  .dc-back-to-top {
    position: fixed;
    right: 1rem;
    bottom: 1rem;
    z-index: 12;
    border: 1px solid color-mix(in srgb, var(--sl-color-gray-4) 50%, transparent);
    border-radius: 999px;
    padding: 0.52rem 0.9rem;
    font-size: 0.84rem;
    font-weight: 700;
    letter-spacing: 0.01em;
    color: var(--sl-color-text);
    background: color-mix(in srgb, var(--sl-color-bg) 92%, var(--sl-color-gray-6));
    box-shadow: 0 8px 20px color-mix(in srgb, var(--sl-color-black, #000) 20%, transparent);
    backdrop-filter: blur(6px);
    transition: transform 0.16s ease, box-shadow 0.2s ease;
  }

  .dc-back-to-top:hover {
    transform: translateY(-1px);
    box-shadow: 0 10px 24px color-mix(in srgb, var(--sl-color-black, #000) 28%, transparent);
  }

  .dc-back-to-top:focus-visible {
    outline: 2px solid var(--sl-color-accent);
    outline-offset: 2px;
  }

  @media (min-width: 72rem) {
    .dc-shortcuts-layout {
      display: block;
      padding-top: 0;
    }

    .dc-control-panel {
      order: 0;
      position: fixed;
      left: auto;
      right: 0;
      top: var(--sl-nav-height, 3.5rem);
      z-index: 10;
      display: block;
      width: 18.75rem;
      min-height: calc(100vh - var(--sl-nav-height, 3.5rem));
      margin: 0;
      padding: 0 0 0 1rem;
      border: 0;
      border-left: 1px solid var(--sl-color-hairline-light);
      border-radius: 0;
      background: var(--sl-color-bg);
      box-shadow: none;
      backdrop-filter: none;
    }

    .dc-control-panel.dc-control-panel-stuck {
      background: var(--sl-color-bg);
    }

    :global(html[data-theme="dark"]) .dc-control-panel.dc-control-panel-stuck {
      background: var(--sl-color-bg);
    }

    .dc-main-column {
      padding-left: 0;
      padding-right: 0;
    }

    .dc-control-panel-toggle {
      display: none;
    }

    .dc-control-panel-content {
      display: block;
      border-top: 0;
    }

    .dc-control-panel-shell {
      margin-top: 0;
      border: 0;
      border-radius: 0;
      background: var(--sl-color-bg);
      box-shadow: none;
      backdrop-filter: none;
      max-height: calc(100vh - var(--sl-nav-height, 3.5rem) - 2.8rem);
      padding-top: 0;
      padding-left: 0;
      padding-bottom: 0.6rem;
    }

    .dc-control-panel-title {
      margin: 0 0 0.5rem;
      font-size: 1.125rem;
      line-height: 1.2;
      font-weight: 600;
      letter-spacing: normal;
      text-transform: none;
      color: var(--sl-color-gray-1);
    }

    :global(html[data-theme="light"]) .dc-control-panel-title {
      color: rgb(23, 24, 28);
    }

    .dc-back-to-top {
      right: calc(18.75rem + 2.5rem);
    }
  }
</style>
