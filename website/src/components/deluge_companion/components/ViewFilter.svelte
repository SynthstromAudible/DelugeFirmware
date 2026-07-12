<script lang="ts">
  import { allViews } from "../stores/view_store.js";
  import { shortcutGroups } from "../stores/group_store.js";
  import GroupFilterItem from "./GroupFilterItem.svelte";
  import ViewFilterItem from "./ViewFilterItem.svelte";
</script>

<div class="filter-panel my-4">
  <section>
    <h2 class="filter-title">Group Filter:</h2>
    <div class="flex flex-wrap items-center gap-2">
      {#each shortcutGroups as group}
        <GroupFilterItem {group} />
      {/each}
    </div>
  </section>

  <section>
    <h2 class="filter-title">Tag Filter:</h2>
    <div class="flex flex-wrap items-center gap-2">
      {#each $allViews as view}
        <ViewFilterItem {view} />
      {/each}
    </div>
  </section>
</div>

<style>
  .filter-panel {
    --dc-group-bg: rgb(83 98 120 / 0.3);
    --dc-group-border: rgb(178 205 222 / 0.72);
    --dc-group-fg: var(--sl-color-gray-1);

    display: flex;
    flex-direction: column;
    gap: 1rem;
  }

  :global(html[data-theme="light"]) .filter-panel {
    --dc-group-bg: rgb(226 232 240 / 0.9);
    --dc-group-border: rgb(106 148 187 / 0.85);
    --dc-group-fg: rgb(41 50 66);
  }

  .filter-title {
    margin: 0 0 0.75rem 0;
    font-size: 1.15rem;
    font-weight: 700;
    line-height: 1.2;
  }
</style>
