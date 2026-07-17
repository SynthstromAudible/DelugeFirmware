<script lang="ts">
  import { tick } from "svelte";
  import type { Shortcut } from "../types/shortcut.js";
  import StepContainerView from "./step/StepContainer.svelte";
  import DelugeUiExternal from "./DelugeUiExternal.svelte";
  import { Firmwares, firmwaresById } from "../data/firmware.js";
  import { viewsById } from "../data/views.js";
  import {
    getCapabilityTitle,
    getSubCapabilityTitle,
    getSubSubCapabilityTitle,
  } from "../stores/capability_store.js";
  import ParagraphView from "./ParagraphView.svelte";
  import SearchHighlight from "./SearchHighlight.svelte";
  import {
    openHardwarePreviewIds,
    setHardwarePreviewOpen,
    shortcutPreviewResetVersion,
  } from "../stores/preview_store.js";

  // One shortcut card instance plus a stable id used by shared preview state.
  export let shortcut: Shortcut;
  export let shortcutPreviewId: number;
  $: firmwares = shortcut.firmware.map((f) => firmwaresById[f]);
  $: views = shortcut.views.map((v) => viewsById[v]);
  $: capabilityTitle =
    getSubSubCapabilityTitle(shortcut.subSubCapabilityId) ??
    shortcut.subSubCapabilityTitle ??
    getSubCapabilityTitle(shortcut.capability) ??
    shortcut.subCapabilityTitle ??
    getCapabilityTitle(shortcut.capabilityParentId) ??
    shortcut.capabilityParentId;
  $: descriptionParagraphs = shortcut.paragraphs.filter((p) => p.type !== "community");
  $: communityParagraphs = shortcut.paragraphs.filter((p) => p.type === "community");

  const viewClassByColor: Record<string, string> = {
    neutral: "dc-view-neutral",
    gold: "dc-view-gold",
    red: "dc-view-red",
    green: "dc-view-green",
    blue: "dc-view-blue",
    purple: "dc-view-purple",
  };

  const firmwareBadgeClassById: Record<number, string> = {
    [Firmwares.OFFICIAL]: "dc-badge-note",
    [Firmwares.COMMUNITY]: "dc-badge-tip",
  };

  let showDetails: boolean = false;
  let shortcutCardEl: HTMLDivElement | undefined;
  let areDescriptionSectionsExpanded: boolean = true;
  let areCommunitySectionsExpanded: boolean = true;
  let lastResetVersion = 0;

  // Computes the occupied top offset from sticky/fixed site chrome so
  // programmatic scroll positioning does not hide content under the header.
  // Returns the scroll offset in pixels required to keep content below sticky UI.
  function getViewportTopOffset() {
    if (typeof window === "undefined") {
      return 0;
    }

    let maxBottom = 0;
    const candidates = document.querySelectorAll<HTMLElement>(
      "header, [role='banner'], .sl-header",
    );

    for (const element of candidates) {
      const style = window.getComputedStyle(element);
      if (style.position !== "fixed" && style.position !== "sticky") {
        continue;
      }

      const rect = element.getBoundingClientRect();
      if (rect.top <= 0 && rect.bottom > maxBottom) {
        maxBottom = rect.bottom;
      }
    }

    // Keep a small visual gap below the sticky top navigation.
    return Math.max(0, Math.ceil(maxBottom)) + 8;
  }

  // Card-local state mirrors the shared preview store.
  $: isPreviewOpen = $openHardwarePreviewIds.includes(shortcutPreviewId);

  // Keep card visibility and description expansion synced with shared preview state.
  $: if (showDetails !== isPreviewOpen) {
    showDetails = isPreviewOpen;
    if (showDetails) {
      areDescriptionSectionsExpanded = false;
      areCommunitySectionsExpanded = false;
    } else {
      areDescriptionSectionsExpanded = true;
      areCommunitySectionsExpanded = true;
    }
  }

  // React to global reset ticks emitted by filter/search clear actions.
  $: if ($shortcutPreviewResetVersion !== lastResetVersion) {
    lastResetVersion = $shortcutPreviewResetVersion;
    // Global filter/search resets should restore the card to collapsed content state.
    areDescriptionSectionsExpanded = true;
    areCommunitySectionsExpanded = true;
    showDetails = false;
  }

  // Toggles preview state and, when opening, scrolls this card into a stable visible position.
  // Returns a Promise that resolves once any required scroll alignment is scheduled.
  async function onStepsClicked() {
    const willOpen = !isPreviewOpen;

    // Delegate single-open policy to shared preview store.
    setHardwarePreviewOpen(shortcutPreviewId, !isPreviewOpen);

    if (typeof window === "undefined" || !willOpen) {
      return;
    }

    await tick();

    if (!shortcutCardEl) {
      return;
    }

    // Align the opened shortcut card under sticky header to maximize preview visibility.
    const top =
      window.scrollY +
      shortcutCardEl.getBoundingClientRect().top -
      getViewportTopOffset();
    window.scrollTo({ top, behavior: "smooth" });
  }

  // Keyboard accessibility for the clickable step sequence area.
  // Returns void.
  function onStepsKeydown(event: KeyboardEvent) {
    if (event.key === "Enter" || event.key === " ") {
      event.preventDefault();
      void onStepsClicked();
    }
  }

  // Expand/collapse the primary description block.
  // Returns void.
  function toggleDescriptionSection() {
    areDescriptionSectionsExpanded = !areDescriptionSectionsExpanded;
  }

  // Expand/collapse community-specific notes.
  // Returns void.
  function toggleCommunitySection() {
    areCommunitySectionsExpanded = !areCommunitySectionsExpanded;
  }
</script>

<!-- Shortcut card root: metadata chips, shortcut steps, descriptions, and optional hardware preview. -->
<div bind:this={shortcutCardEl} class="shortcut-card rounded-lg p-4 text-[var(--sl-color-text)]">
  <!-- Header with firmware/capability/view chips and title. -->
  <div class="shortcut-header">
    <div class="mb-0 flex flex-wrap gap-1 leading-none">
      {#each firmwares as firmware}
        <span
          class={`dc-view-chip dc-firmware-chip ${firmwareBadgeClassById[firmware.id] ?? "dc-badge-note"}`}
        >
          {firmware.title} Firmware
        </span>
      {/each}
      {#if capabilityTitle}
        <span class="dc-capability-chip">{capabilityTitle}</span>
      {/if}
      {#each views as view}
        <span
          class={`dc-view-chip ${viewClassByColor[view.color] ?? "dc-view-neutral"}`}
        >
          {view.title}
        </span>
      {/each}
    </div>
    <h3 class="shortcut-title">
      <SearchHighlight text={shortcut.name} />
    </h3>
  </div>
  <!-- Step sequence acts as preview toggle (mouse + keyboard accessible). -->
  <div
    role="button"
    tabindex="0"
    aria-expanded={showDetails}
    class="shortcut-steps m-0 rounded-md p-1 text-left"
    on:click={onStepsClicked}
    on:keydown={onStepsKeydown}
  >
    <span class="shortcut-steps-inner">
      {#each shortcut.steps as step (step)}
        <StepContainerView bind:step />
      {/each}
    </span>
  </div>
  <!-- Primary description section (default docs text). -->
  {#if shortcut.description || descriptionParagraphs.length > 0}
    <aside class="shortcut-aside shortcut-description-aside mt-4 rounded-md px-3 py-1" data-type="tip">
      <button
        type="button"
        class="aside-title-button"
        aria-expanded={areDescriptionSectionsExpanded}
        on:click={toggleDescriptionSection}
      >
        <span class="description-aside-title">Shortcut Description</span>
        <span class="aside-toggle-indicator">{areDescriptionSectionsExpanded ? "Hide" : "Show"}</span>
      </button>
      {#if areDescriptionSectionsExpanded}
        {#if shortcut.description}
          <p class="shortcut-description mt-2 mb-0 text-sm leading-6">
            <SearchHighlight text={shortcut.description} />
          </p>
        {/if}
        {#each descriptionParagraphs as paragraph}
          <ParagraphView bind:paragraph />
        {/each}
      {/if}
    </aside>
  {/if}
  <!-- Community-only notes section, separated visually as caution content. -->
  {#if communityParagraphs.length > 0}
    {#each communityParagraphs as paragraph}
      <aside
        class="shortcut-aside mt-4 rounded-md px-3 py-1"
        data-type="caution"
      >
        <button
          type="button"
          class="aside-title-button"
          aria-expanded={areCommunitySectionsExpanded}
          on:click={toggleCommunitySection}
        >
          <span class="community-aside-title">
            Community Firmware Behaviour Change
          </span>
          <span class="aside-toggle-indicator">{areCommunitySectionsExpanded ? "Hide" : "Show"}</span>
        </button>
        {#if areCommunitySectionsExpanded}
          <ParagraphView bind:paragraph />
        {/if}
      </aside>
    {/each}
  {/if}
  <!-- Lazy-loaded hardware preview. -->
  {#if showDetails}
    <div class="mt-4 border border-[var(--sl-color-gray-5)]">
      <DelugeUiExternal bind:steps={shortcut.steps} />
    </div>
  {/if}
</div>

<style>
  .shortcut-card {
    --dc-card-bg: rgb(70 84 104 / 0.22);
    --dc-card-border: rgb(92 107 132 / 0.45);
    --dc-strip-bg: rgb(83 98 120 / 0.3);
    --dc-strip-border: rgb(102 118 143 / 0.45);
    --dc-step-bg: rgb(13 18 30 / 0.45);
    --dc-step-border: rgb(103 118 143 / 0.5);
    --dc-aside-bg: rgb(83 98 120 / 0.24);
    --dc-aside-border: rgb(178 205 222 / 0.72);
    --dc-aside-fg: var(--sl-color-gray-2);

    border: 1px solid var(--dc-card-border);
    background: var(--dc-card-bg);
  }

  .shortcut-steps {
    border: 1px solid var(--dc-strip-border);
    background: var(--dc-strip-bg);
    display: inline-block;
    box-sizing: border-box;
    width: max-content;
    max-width: 100%;
    min-width: 0;
    inline-size: max-content;
    max-inline-size: 100%;
    min-inline-size: 0;
    align-self: flex-start;
    overflow-x: auto;
    flex: 0 0 auto;
  }

  .shortcut-steps-inner {
    display: inline-flex;
    flex-wrap: wrap;
    align-items: flex-start;
    gap: 0.25rem;
    width: max-content;
    max-width: 100%;
    min-width: 0;
    inline-size: max-content;
    max-inline-size: 100%;
    min-inline-size: 0;
  }

  .shortcut-steps-inner > :global(*) {
    flex: 0 0 auto;
  }

  :global(html[data-theme="light"]) .shortcut-card {
    --dc-card-bg: rgb(241 245 249 / 0.92);
    --dc-card-border: rgb(148 163 184 / 0.55);
    --dc-strip-bg: rgb(226 232 240 / 0.9);
    --dc-strip-border: rgb(148 163 184 / 0.7);
    --dc-step-bg: rgb(248 250 252 / 0.96);
    --dc-step-border: rgb(148 163 184 / 0.8);
    --dc-aside-bg: rgb(226 232 240 / 0.82);
    --dc-aside-border: rgb(106 148 187 / 0.85);
    --dc-aside-fg: rgb(51 65 85);
  }

  .shortcut-header {
    display: flex;
    flex-direction: column;
    gap: 0.5rem;
    width: 100%;
    min-width: 0;
  }

  .shortcut-card {
    display: flex;
    flex-direction: column;
    align-items: stretch;
    width: 100%;
    min-width: 0;
    max-width: 100%;
    content-visibility: auto;
    contain-intrinsic-size: 18rem;
  }

  .shortcut-title {
    display: block;
    width: 100%;
    max-width: 100%;
    margin: 0;
    padding: 0;
    font-size: 1.15rem !important;
    font-weight: 700 !important;
    line-height: 1.2;
    white-space: normal;
    overflow-wrap: anywhere;
    word-break: break-word;
  }

  .shortcut-steps-inner > :global(*) {
    align-self: start;
    margin-top: 0;
  }

  .shortcut-description {
    color: var(--sl-color-gray-2);
  }

  :global(html[data-theme="light"]) .shortcut-description {
    color: var(--sl-color-gray-3);
  }

  .shortcut-aside {
    display: inline-block;
    box-sizing: border-box;
    inline-size: fit-content;
    max-inline-size: 100%;
    align-self: flex-start;
    border-inline-start: 0.25rem solid var(--dc-aside-border);
    background: var(--dc-aside-bg);
    color: var(--dc-aside-fg);
  }

  .shortcut-aside[data-type="caution"] {
    border-inline-start-color: var(--sl-color-orange-high);
    background: color-mix(in srgb, var(--sl-color-orange-low) 22%, transparent);
    color: var(--sl-color-orange-text);
  }

  :global(html[data-theme="light"]) .shortcut-aside[data-type="caution"] {
    background: color-mix(in srgb, var(--sl-color-orange-low) 35%, var(--sl-color-bg));
  }

  .shortcut-aside :global(p) {
    margin-block: 0.5rem;
  }

  .shortcut-description-aside :global(.paragraph-community) {
    margin-top: 0.5rem;
  }

  .description-aside-title {
    margin: 0.35rem 0 0.2rem;
    font-size: 0.76rem;
    line-height: 1.2;
    letter-spacing: 0.01em;
    text-transform: uppercase;
    font-weight: 700;
    color: inherit;
    opacity: 0.9;
  }

  .aside-title-button {
    display: flex;
    align-items: center;
    justify-content: space-between;
    width: 100%;
    gap: 0.75rem;
    border: 0;
    background: transparent;
    color: inherit;
    padding: 0;
    margin: 0;
    text-align: left;
    cursor: pointer;
  }

  .aside-toggle-indicator {
    font-size: 0.72rem;
    line-height: 1;
    font-weight: 700;
    text-transform: uppercase;
    letter-spacing: 0.02em;
    opacity: 0.85;
  }

  .community-aside-title {
    margin: 0.35rem 0 0.2rem;
    font-size: 0.76rem;
    line-height: 1.2;
    letter-spacing: 0.01em;
    text-transform: uppercase;
    font-weight: 700;
    color: inherit;
    opacity: 0.9;
  }

  .dc-view-chip {
    display: inline-flex;
    align-items: center;
    white-space: nowrap;
    border-radius: 9999px;
    padding: 0.2rem 0.5rem;
    font-size: 0.75rem;
    line-height: 1.1;
    font-weight: 500;
    background-color: var(--dc-chip-bg);
    color: var(--dc-chip-fg);
  }

  .dc-firmware-chip {
    display: inline-flex;
    align-items: center;
    border-radius: 0.45rem;
    border: 1px solid var(--dc-badge-border);
    padding: 0.18rem 0.54rem;
    font-size: 0.75rem;
    line-height: 1.15;
    font-weight: 600;
    letter-spacing: 0;
    font-family: var(--sl-font-system-mono, ui-monospace, SFMono-Regular, Menlo, Monaco, Consolas, "Liberation Mono", "Courier New", monospace);
    background: var(--dc-badge-bg);
    color: var(--dc-badge-fg);
  }

  .dc-capability-chip {
    display: inline-flex;
    align-items: center;
    border-radius: 0.3rem;
    border: 1px solid var(--dc-capability-chip-border);
    padding: 0.2rem 0.52rem;
    font-size: 0.75rem;
    line-height: 1.1;
    font-weight: 600;
    letter-spacing: 0.01em;
    background: var(--dc-capability-chip-bg);
    color: var(--dc-capability-chip-fg);
  }

  :global(html[data-theme="dark"]) .dc-capability-chip {
    --dc-capability-chip-bg: rgb(146 115 12 / 0.35);
    --dc-capability-chip-border: rgb(234 199 72);
    --dc-capability-chip-fg: rgb(253 244 194);
  }

  :global(html[data-theme="light"]) .dc-capability-chip {
    --dc-capability-chip-bg: rgb(252 232 156 / 0.55);
    --dc-capability-chip-border: rgb(181 132 22);
    --dc-capability-chip-fg: rgb(74 49 6);
  }

  .dc-badge-note {
    --dc-badge-bg: rgb(38 67 188 / 0.22);
    --dc-badge-border: rgb(75 112 255);
    --dc-badge-fg: rgb(222 232 255);
  }

  .dc-badge-tip {
    --dc-badge-bg: rgb(106 39 152 / 0.24);
    --dc-badge-border: rgb(173 97 232);
    --dc-badge-fg: rgb(241 218 255);
  }

  :global(html[data-theme="light"]) .dc-badge-note {
    --dc-badge-bg: rgb(229 236 255);
    --dc-badge-border: rgb(76 108 220);
    --dc-badge-fg: rgb(36 55 123);
  }

  :global(html[data-theme="light"]) .dc-badge-tip {
    --dc-badge-bg: rgb(245 231 255);
    --dc-badge-border: rgb(138 77 184);
    --dc-badge-fg: rgb(84 30 119);
  }

  .dc-view-blue {
    --dc-chip-bg: rgb(178 205 222);
    --dc-chip-border: rgb(106 148 187);
    --dc-chip-fg: rgb(41 50 66);
  }

  .dc-view-gold {
    --dc-chip-bg: rgb(202 180 122);
    --dc-chip-border: rgb(171 135 71);
    --dc-chip-fg: rgb(49 31 23);
  }

  .dc-view-green {
    --dc-chip-bg: rgb(132 199 141);
    --dc-chip-border: rgb(56 145 73);
    --dc-chip-fg: rgb(12 34 19);
  }

  .dc-view-neutral {
    --dc-chip-bg: rgb(167 177 185);
    --dc-chip-border: rgb(97 111 121);
    --dc-chip-fg: rgb(34 38 42);
  }

  .dc-view-purple {
    --dc-chip-bg: rgb(226 187 236);
    --dc-chip-border: rgb(192 115 210);
    --dc-chip-fg: rgb(58 18 64);
  }

  .dc-view-red {
    --dc-chip-bg: rgb(235 182 182);
    --dc-chip-border: rgb(210 115 115);
    --dc-chip-fg: rgb(57 22 22);
  }
</style>
