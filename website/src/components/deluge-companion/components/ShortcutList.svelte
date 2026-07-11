<script lang="ts">
  import ShortcutView from "./Shortcut.svelte";
  import { filteredShortcuts } from "../stores/shortcutStore.js";
</script>

{#each $filteredShortcuts as shortcut}
  <ShortcutView {shortcut} />
{:else}
  <p class="italic text-center">No matches</p>
{/each}
