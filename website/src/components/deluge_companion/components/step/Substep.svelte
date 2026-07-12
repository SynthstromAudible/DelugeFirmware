<script lang="ts">
  import StepView from "./Step.svelte";
  import { Action } from "../../data/actions";
  import { Control } from "../../data/targets";
  import type { Step } from "../../types/shortcut";
  import type { SubstepContainer } from "../../types/shortcut";

  export let step: SubstepContainer;
  export let inline: boolean;

  type RenderedSubstep = {
    step: Step;
    compositeAction: "none" | "press-turn";
  };

  const knobControls = new Set<Control>([
    Control.X,
    Control.Y,
    Control.SELECT,
    Control.TEMPO,
    Control.PARAMETER,
    Control.LOWER_PARAM,
    Control.UPPER_PARAM,
  ]);

  // Detects adjacent press+turn operations on knob-like controls and merges them.
  // Returns true when two adjacent steps should render as one composite action.
  const isPressTurnPair = (first: Step | undefined, second: Step | undefined) =>
    !!first &&
    !!second &&
    first.action === Action.PRESS &&
    second.action === Action.TURN &&
    first.control === second.control &&
    knobControls.has(first.control);

  // Measure at full width, shrink to the widest line, then right-align continuations.
  // Returns a Svelte action object with update/destroy hooks.
  function alignWrappedRows(node: HTMLDivElement) {
    let lineStartFrame: number | undefined;
    let lineStartTimeouts: ReturnType<typeof setTimeout>[] = [];

    // Clears per-child margin overrides before re-measuring wrapping.
    // Returns void.
    function resetChildren() {
      [...node.children].forEach((child) => {
        (child as HTMLElement).style.marginLeft = "";
      });
    }

    // Returns the total horizontal padding+border contribution for the card.
    function getCardWidthOffset(card: HTMLElement) {
      const styles = getComputedStyle(card);
      return (
        parseFloat(styles.paddingLeft) +
        parseFloat(styles.paddingRight) +
        parseFloat(styles.borderLeftWidth) +
        parseFloat(styles.borderRightWidth)
      );
    }

    // Captures row starts and measured line widths from child offsets.
    // Returns children, line-start indices, and measured line widths.
    function getLineStarts() {
      const children = [...node.children] as HTMLElement[];
      const lineStarts = new Set<number>();
      const lineWidths: number[] = [];
      const rowLeft = node.getBoundingClientRect().left;
      let currentLineTop = children[0]?.offsetTop ?? 0;
      let currentLineWidth = 0;

      // Branch: wrapped to a new visual row.
      children.forEach((child, idx) => {
        const childTop = child.offsetTop;
        const childRight = child.getBoundingClientRect().right - rowLeft;

        if (idx > 0 && childTop > currentLineTop + 1) {
          lineWidths.push(currentLineWidth);
          lineStarts.add(idx);
          currentLineTop = childTop;
          currentLineWidth = 0;
        }

        currentLineWidth = Math.max(currentLineWidth, childRight);
      });

      if (children.length > 0) {
        lineWidths.push(currentLineWidth);
      }

      return { children, lineStarts, lineWidths };
    }

    // Recomputes card width and continuation alignment based on wrapped rows.
    // Returns void.
    function updateLineStartIndices() {
      const card = node.closest(".dc-step-card") as HTMLElement | null;

      // Branch: action used outside expected step card context.
      if (!card) {
        return;
      }

      card.style.width = "100%";
      resetChildren();

      const availableCardWidth = card.getBoundingClientRect().width;
      const measuredLines = getLineStarts();
      const widestLineWidth = Math.max(...measuredLines.lineWidths, 0);
      const cardWidth = Math.min(
        Math.ceil(widestLineWidth + getCardWidthOffset(card)),
        availableCardWidth,
      );

      if (cardWidth > 0) {
        card.style.width = `${cardWidth}px`;
      }

      const alignedLines = getLineStarts();

      alignedLines.children.forEach((child, idx) => {
        child.style.marginLeft = alignedLines.lineStarts.has(idx) ? "auto" : "";
      });
    }

    // Defers expensive measurement to the next paint(s).
    // Returns void.
    function scheduleLineStartUpdate() {
      // Branch: SSR/non-browser fallback.
      if (typeof requestAnimationFrame === "undefined") {
        updateLineStartIndices();
        return;
      }

      if (lineStartFrame !== undefined) {
        cancelAnimationFrame(lineStartFrame);
      }

      lineStartFrame = requestAnimationFrame(() => {
        lineStartFrame = requestAnimationFrame(() => {
          lineStartFrame = undefined;
          updateLineStartIndices();
        });
      });
    }

    // Re-run alignment after layout settles (fonts/transitions/resize).
    // Returns void.
    function scheduleSettledUpdates() {
      lineStartTimeouts.forEach((timeout) => clearTimeout(timeout));
      lineStartTimeouts = [0, 50, 250, 750].map((delay) =>
        setTimeout(scheduleLineStartUpdate, delay),
      );
      document.fonts?.ready.then(scheduleLineStartUpdate);
    }

    window.addEventListener("resize", scheduleLineStartUpdate);
    scheduleSettledUpdates();

    return {
      update: scheduleSettledUpdates,
      destroy() {
        window.removeEventListener("resize", scheduleLineStartUpdate);
        lineStartTimeouts.forEach((timeout) => clearTimeout(timeout));

        if (lineStartFrame !== undefined) {
          cancelAnimationFrame(lineStartFrame);
        }
      },
    };
  }

  // Render-time normalization that fuses press+turn pairs into a single visual step.
  $: renderedSubsteps = step.substeps.reduce<RenderedSubstep[]>((acc, _, idx, all) => {
    const current = all[idx];
    const next = all[idx + 1];

    // Branch: current + next collapse into one composite action.
    if (isPressTurnPair(current, next)) {
      acc.push({ step: next, compositeAction: "press-turn" });
      return acc;
    }

    // Branch: skip current because it was consumed by previous pair.
    if (isPressTurnPair(all[idx - 1], current)) {
      return acc;
    }

    // Branch: regular substep.
    acc.push({ step: current, compositeAction: "none" });
    return acc;
  }, []);
</script>

<!-- Wrapped substep row with right-aligned continuation lines. -->
<div
  use:alignWrappedRows={renderedSubsteps.length}
  class="substeps-row flex w-full max-w-full flex-wrap items-start gap-x-2 gap-y-1"
>
  {#each renderedSubsteps as renderedSubstep, idx}
    <!-- First item renders without leading plus marker. -->
    {#if idx === 0}
      <span class="substeps-item">
        <StepView
          step={renderedSubstep.step}
          {inline}
          compositeAction={renderedSubstep.compositeAction}
        />
      </span>
    {:else}
      <!-- Continuation items prepend a visual plus separator. -->
      <span class="substeps-item substeps-break">
        <span class="substeps-plus">+</span>
        <StepView
          step={renderedSubstep.step}
          {inline}
          compositeAction={renderedSubstep.compositeAction}
        />
      </span>
    {/if}
  {/each}
</div>

<style>
  .substeps-item {
    display: inline-flex;
    max-width: 100%;
    min-width: 0;
  }

  .substeps-break {
    align-items: flex-start;
    gap: 0.5rem;
  }

  .substeps-plus {
    align-self: end;
    font-size: var(--dc-step-font-size, 0.9rem);
    line-height: 1;
    padding-bottom: 0.15rem;
  }
</style>
