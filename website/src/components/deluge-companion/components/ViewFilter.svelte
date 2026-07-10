<script lang="ts">
  import { allViews } from "../stores/viewStore.js";
  import ViewFilterItem from "./ViewFilterItem.svelte";
</script>

<div class="my-4">
  <h2 class="filter-title">Filter:</h2>
  <div class="flex flex-wrap items-center gap-2">
    {#each $allViews as view}
      <ViewFilterItem {view} />
    {/each}
  </div>
</div>

<style>
  .filter-title {
    margin: 0 0 0.75rem 0;
    font-size: 1.15rem;
    font-weight: 700;
    line-height: 1.2;
  }
</style>
