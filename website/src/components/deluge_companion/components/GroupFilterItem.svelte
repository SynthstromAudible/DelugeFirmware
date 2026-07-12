<script lang="ts">
  import {
    activeShortcutGroup,
    type ShortcutGroup,
  } from "../stores/group_store.js";

  export let group: ShortcutGroup;

  $: isActive = $activeShortcutGroup === group.id;

  function onClick() {
    activeShortcutGroup.update((oldValue) => {
      if (oldValue === group.id) {
        return null;
      }
      return group.id;
    });
  }
</script>

<button class:active={isActive} class="dc-group-chip" on:click={onClick}>
  {group.title}
</button>

<style>
  .dc-group-chip {
    display: inline-flex;
    align-items: center;
    justify-content: center;
    margin: 0;
    white-space: nowrap;
    border-radius: 0.375rem;
    border: 1px solid var(--dc-group-border);
    box-sizing: border-box;
    min-height: 1.875rem;
    padding: 0 0.75rem;
    font-size: 0.875rem;
    line-height: normal;
    font-weight: 500;
    color: var(--dc-group-border);
    background: transparent;
  }

  .dc-group-chip.active {
    background: var(--dc-group-bg);
    border-color: var(--dc-group-border);
    color: var(--dc-group-fg);
  }
</style>
