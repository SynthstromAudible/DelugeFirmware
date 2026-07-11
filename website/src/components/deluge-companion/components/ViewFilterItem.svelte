<script lang="ts">
  import { activeView } from "../stores/viewStore.js";
  import type { View } from "../types/shortcut.js";
  export let view: View;

  const viewClassByColor: Record<string, string> = {
    neutral: "dc-view-neutral",
    gold: "dc-view-gold",
    red: "dc-view-red",
    green: "dc-view-green",
    blue: "dc-view-blue",
    purple: "dc-view-purple",
  };

  $: isActive = $activeView != null && $activeView === view.id;
  $: colorClass = viewClassByColor[view.color] ?? "dc-view-neutral";
  $: classes = `dc-filter-chip ${colorClass} ${isActive ? "is-active" : ""}`;

  function onClick() {
    activeView.update((oldValue) => {
      if (oldValue === view.id) {
        return null;
      }
      return view.id;
    });
  }
</script>

<button
  class={classes}
  on:click={onClick}
>
  {view.title}
</button>

<style>
  .dc-filter-chip {
    display: inline-flex;
    align-items: center;
    justify-content: center;
    margin: 0;
    white-space: nowrap;
    border-radius: 9999px;
    border: 1px solid var(--dc-chip-border);
    box-sizing: border-box;
    height: 1.875rem;
    min-height: 1.875rem;
    padding: 0 0.75rem;
    font-size: 0.875rem;
    line-height: normal;
    font-weight: 500;
    color: var(--dc-chip-border);
    background: transparent;
  }

  .dc-filter-chip.is-active {
    background-color: var(--dc-chip-bg);
    border-color: var(--dc-chip-border);
    color: var(--dc-chip-fg);
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
