<script lang="ts">
  import type { Span } from "../types/shortcut.js";
  import StepContainerView from "./step/StepContainer.svelte";

  export let span: Span;
</script>

<!-- Branch: plain text span. -->
{#if span.text}
  {span.text}
{/if}
<!-- Branch: structured step span. -->
{#if span.steps}
  {#each span.steps as step}
    <StepContainerView bind:step inline={true} />
  {/each}
{/if}
