<script lang="ts">
  import { actionDescriptions } from "../../data/actions";
  import type { Step } from "../../types/shortcut";

  export let step: Step;
  export let compositeAction: "none" | "press-turn" = "none";

  // Resolve action metadata and optional composite label styling.
  $: description = actionDescriptions[step.action];
  $: actionTitle =
    compositeAction === "press-turn" ? "press & turn" : description?.title;
  $: actionClasses =
    compositeAction === "press-turn" ? "text-purple-400" : description?.classes;
</script>

<!-- Branch: defensive fallback for unknown action ids. -->
{#if !description}
  <span class="action font-bold text-red-700">INVALID</span>
{:else}
  <!-- Branch: normal/composite action label rendering. -->
  <span class="{actionClasses} action font-medium leading-none"
    >{actionTitle}</span
  >
{/if}

<style>
  .action {
    grid-area: action;
    align-self: baseline;
    font-size: var(--dc-step-action-size, 1rem);
    font-weight: 500;
    line-height: 1;
  }
</style>
