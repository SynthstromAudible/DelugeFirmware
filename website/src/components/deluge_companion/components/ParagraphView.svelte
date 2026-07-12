<script lang="ts">
  import type { Paragraph } from "../types/shortcut.js";
  import ParagraphSpan from "./ParagraphSpan.svelte";

  export let paragraph: Paragraph;
</script>

<p class="mt-4 leading-8">
  {#each paragraph.spans as span}
    <ParagraphSpan bind:span />
  {/each}
</p>
