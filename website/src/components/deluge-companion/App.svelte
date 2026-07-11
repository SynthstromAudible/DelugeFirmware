<script lang="ts">
  import ShortcutsView from "./views/ShortcutsView.svelte";
  import EditorView from "./views/EditorView.svelte";

  let currentView: string = "";
  const updateView = () => {
    currentView = window.location.hash;
  };
  updateView();
</script>

{#if currentView === ""}
  <ShortcutsView />
{:else if currentView === "#editor"}
  <EditorView />
{/if}

<svelte:window on:hashchange={updateView} />
