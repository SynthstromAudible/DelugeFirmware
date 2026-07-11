<script lang="ts">
  import type { Span } from "../types/shortcut.js";
  import StepContainerView from "./step/StepContainer.svelte";

  export let span: Span;
</script>

{#if span.text}
  {span.text}
{/if}
{#if span.steps}
  {#each span.steps as step}
    <StepContainerView bind:step inline={true} />
  {/each}
{/if}
