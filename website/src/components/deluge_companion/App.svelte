<script lang="ts">
  import { onMount, tick } from "svelte";
  import ShortcutsView from "./views/ShortcutsView.svelte";
  import EditorView from "./views/EditorView.svelte";
  import { ensureShortcutsLoaded } from "./stores/shortcut_store.js";

  // The app switches between user view and markdown editor view via URL hash.
  let currentView: string = "";

  // Returns void after syncing current view from window hash.
  const updateView = () => {
    if (typeof window === "undefined") {
      return;
    }

    currentView = window.location.hash;
  };

  onMount(async () => {
    updateView();

    // Preload and normalize shortcut data before the UI is considered ready.
    await ensureShortcutsLoaded();

    await tick();
    if (typeof window !== "undefined") {
      // Used by local perf scripts/probes to detect interactivity.
      window.dispatchEvent(new CustomEvent("deluge-companion-ready"));
    }
  });
</script>

{#if currentView === "#editor"}
  <EditorView />
{:else}
  <ShortcutsView />
{/if}

<svelte:window on:hashchange={updateView} />
