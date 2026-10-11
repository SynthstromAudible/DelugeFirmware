<script lang="ts">
  import { onDestroy, onMount } from "svelte";
  import ShortcutView from "./Shortcut.svelte";
  import { filteredShortcuts } from "../stores/shortcut_store.js";

  // Incremental rendering keeps large result sets responsive.
  const INITIAL_RENDER_COUNT = 80;
  const RENDER_CHUNK_SIZE = 60;

  let renderLimit = INITIAL_RENDER_COUNT;
  let previousCount = $filteredShortcuts.length;
  let sentinelEl: HTMLDivElement | undefined;
  let observer: IntersectionObserver | undefined;
  let isDraggingScrollbarThumb = false;

  // Returns the current browser vertical scrollbar width in pixels.
  function getVerticalScrollbarWidth() {
    return Math.max(0, window.innerWidth - document.documentElement.clientWidth);
  }

  // Returns true when a pointer event starts in the vertical scrollbar gutter/track area.
  function isPointerOnVerticalScrollbar(clientX: number) {
    const scrollbarWidth = getVerticalScrollbarWidth();
    if (scrollbarWidth <= 0) {
      return false;
    }

    // Include a small tolerance so border clicks are treated as scrollbar interactions.
    const scrollbarLeftEdge = window.innerWidth - scrollbarWidth - 2;
    return clientX >= scrollbarLeftEdge;
  }

  // Adds the next result page, bounded by current filtered result size.
  // Returns void.
  function loadMore({ manual = false }: { manual?: boolean } = {}) {
    // While dragging the native scrollbar thumb, do not mutate document height.
    // This keeps the thumb anchored under the cursor.
    if (isDraggingScrollbarThumb && !manual) {
      return;
    }

    const total = $filteredShortcuts.length;
    if (renderLimit >= total) {
      return;
    }

    renderLimit = Math.min(total, renderLimit + RENDER_CHUNK_SIZE);
  }

  // Scroll fallback: loads more when user approaches document bottom.
  // Returns void.
  function handleWindowScroll() {
    const total = $filteredShortcuts.length;
    if (renderLimit >= total) {
      return;
    }

    const scrollBottom = window.scrollY + window.innerHeight;
    const pageBottom = document.documentElement.scrollHeight;
    if (pageBottom - scrollBottom <= 700) {
      loadMore();
    }
  }

  // Detects start of native scrollbar dragging and pauses auto-loading until pointer release.
  // Returns void.
  function handleWindowPointerDown(event: PointerEvent) {
    if (!isPointerOnVerticalScrollbar(event.clientX)) {
      return;
    }

    isDraggingScrollbarThumb = true;
  }

  // Ends scrollbar drag mode and resumes normal auto-loading checks.
  // Returns void.
  function handleWindowPointerUp() {
    if (!isDraggingScrollbarThumb) {
      return;
    }

    isDraggingScrollbarThumb = false;

    // Re-evaluate once after release in case user dropped near the bottom.
    handleWindowScroll();
  }

  // Reconnects observer whenever sentinel node or observer instance changes.
  // Returns void.
  function connectObserver() {
    if (!observer || !sentinelEl) {
      return;
    }

    observer.disconnect();
    observer.observe(sentinelEl);
  }

  // Resets pagination when result set changes.
  // Returns void.
  function resetRenderWindow() {
    const total = $filteredShortcuts.length;
    renderLimit = Math.min(total, INITIAL_RENDER_COUNT);

    // If initial items do not fill viewport, prefetch another chunk immediately.
    if (renderLimit < total) {
      requestAnimationFrame(() => {
        if (!sentinelEl) {
          return;
        }

        const rect = sentinelEl.getBoundingClientRect();
        if (rect.top <= window.innerHeight + 120) {
          loadMore();
        }
      });
    }
  }

  $: if ($filteredShortcuts.length !== previousCount) {
    // New query/filter set: restart incremental window from first page.
    previousCount = $filteredShortcuts.length;
    resetRenderWindow();
  }

  // Visible subset rendered at any point in time.
  $: visibleShortcuts = $filteredShortcuts.slice(0, renderLimit);

  // Keep observer attached as sentinel gets recreated.
  $: connectObserver();

  onMount(() => {
    // IO sentinel handles most pagination; scroll fallback helps on very tall cards.
    observer = new IntersectionObserver(
      (entries) => {
        if (entries.some((entry) => entry.isIntersecting)) {
          loadMore();
        }
      },
      {
        root: null,
        rootMargin: "500px 0px",
      },
    );

    resetRenderWindow();
    connectObserver();
  });

  onDestroy(() => {
    observer?.disconnect();
    observer = undefined;
  });
</script>

{#each visibleShortcuts as shortcut, visibleIndex (shortcut.originalIndex ?? visibleIndex)}
  <!-- One shortcut card. Preview id stays stable across rerenders. -->
  <ShortcutView {shortcut} shortcutPreviewId={shortcut.originalIndex ?? visibleIndex} />
{:else}
  <!-- Empty-state when no shortcuts match active search/facets. -->
  <p class="italic text-center">No matches</p>
{/each}

{#if visibleShortcuts.length < $filteredShortcuts.length}
  <!-- Sentinel + manual fallback button for incremental pagination. -->
  <div bind:this={sentinelEl} class="shortcut-list-sentinel" aria-hidden="true"></div>
  <div class="shortcut-list-more">
    <button class="shortcut-list-more-btn" on:click={() => loadMore({ manual: true })}>
      Load more shortcuts
    </button>
  </div>
{/if}

<svelte:window
  on:scroll={handleWindowScroll}
  on:pointerdown={handleWindowPointerDown}
  on:pointerup={handleWindowPointerUp}
/>

<style>
  .shortcut-list-sentinel {
    display: block;
    width: 100%;
    height: 1px;
    margin-top: 0.75rem;
  }

  .shortcut-list-more {
    margin-top: 0.75rem;
    display: flex;
    justify-content: center;
  }

  .shortcut-list-more-btn {
    border: 1px solid var(--sl-color-gray-5);
    border-radius: 9999px;
    padding: 0.45rem 0.9rem;
    font-size: 0.85rem;
    background: color-mix(in srgb, var(--sl-color-bg) 84%, var(--sl-color-gray-6));
    color: var(--sl-color-gray-2);
  }

  .shortcut-list-more-btn:hover {
    border-color: var(--sl-color-accent-high);
  }
</style>
